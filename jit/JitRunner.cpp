//
// In-process execution of generated modules via ORC LLJIT.
//

#include "JitRunner.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#endif

#include <atomic>
#include <chrono>
#include <thread>

#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"

#ifndef DJINN_CLANG_PATH
#define DJINN_CLANG_PATH "clang"
#endif

#ifndef DJINN_SOURCE_DIR
#define DJINN_SOURCE_DIR "."
#endif

namespace djinn
{
    namespace
    {
        namespace fs = std::filesystem;

        // Exit code produced by a terminated program (abort/exit). Read by the
        // runner thread after the JIT thread ends or parks.
        std::atomic<int> terminatedExitCode{0};

#ifdef _WIN32
        // Runs main under SEH so hardware faults in JIT code (div-by-zero,
        // access violations) become exit codes instead of killing the process.
        __declspec(noinline) int runMainSeh(int (*mainFn)())
        {
            __try
            {
                return mainFn();
            }
            __except (terminatedExitCode.store(static_cast<int>(GetExceptionCode()),
                                               std::memory_order_release),
                     EXCEPTION_EXECUTE_HANDLER)
            {
                return -2;
            }
        }

        int runMainProtected(int (*mainFn)()) { return runMainSeh(mainFn); }
#else
        // POSIX: siglongjmp from fatal-signal handlers back into the runner.
        // The buffer lives in the runner's frame; only that thread jumps.
        sigjmp_buf fatalJmp;
        std::atomic<bool> fatalPending{false};

        void fatalSignalHandler(const int sig)
        {
            if (fatalPending.exchange(true))
                _exit(128 + sig);
            terminatedExitCode.store(128 + sig, std::memory_order_release);
            siglongjmp(fatalJmp, 1);
        }

        int runMainProtected(int (*mainFn)())
        {
            constexpr int fatalSignals[] = {SIGFPE, SIGSEGV, SIGBUS, SIGILL, SIGABRT};
            struct sigaction oldActions[5] = {};
            struct sigaction action{};
            action.sa_handler = &fatalSignalHandler;
            sigemptyset(&action.sa_mask);
            for (size_t i = 0; i < std::size(fatalSignals); ++i)
                sigaction(fatalSignals[i], &action, &oldActions[i]);

            fatalPending.store(false);
            if (sigsetjmp(fatalJmp, 1) != 0)
            {
                for (size_t i = 0; i < std::size(fatalSignals); ++i)
                    sigaction(fatalSignals[i], &oldActions[i], nullptr);
                return -2;
            }
            const int code = mainFn();
            for (size_t i = 0; i < std::size(fatalSignals); ++i)
                sigaction(fatalSignals[i], &oldActions[i], nullptr);
            return code;
        }
#endif

        // Terminates the JIT program. Neither C++ exceptions nor MSVC longjmp
        // can unwind through JIT frames (no unwind info), and killing the
        // thread (_endthreadex/pthread_exit) bypasses sanitizer thread cleanup
        // and corrupts the process at exit. So: publish the exit code and park
        // this thread forever — the runner detaches it and reclaims nothing.
        [[noreturn]] void terminateJitMain(const int code)
        {
            terminatedExitCode.store(code, std::memory_order_release);
#ifdef _WIN32
            for (;;)
                Sleep(INFINITE);
#else
            for (;;)
            {
                timespec ts{3600, 0};
                nanosleep(&ts, nullptr);
            }
#endif
        }

        void djinnJitExit(const int code)
        {
            terminateJitMain(code);
        }

        void djinnJitAbort()
        {
#ifdef _WIN32
            terminateJitMain(3); // MSVC abort() exit code
#else
            terminateJitMain(134); // 128 + SIGABRT as seen through system()
#endif
        }

        // printf-family and POSIX names are inline-only in the Windows UCRT (or
        // legacy "oldnames"), so the process-symbol generator can't find them.
        // These host wrappers are registered under the plain C names below.
        int vformat(FILE* stream, const char* format, va_list args)
        {
            char stackBuf[1024];
            va_list copy;
            va_copy(copy, args);
            const int needed = vsnprintf(stackBuf, sizeof stackBuf, format, copy);
            va_end(copy);
            if (needed < 0) return needed;

            const char* buf = stackBuf;
            std::vector<char> heap;
            if (static_cast<size_t>(needed) >= sizeof stackBuf)
            {
                heap.resize(static_cast<size_t>(needed) + 1);
                vsnprintf(heap.data(), heap.size(), format, args);
                buf = heap.data();
            }
            fwrite(buf, 1, static_cast<size_t>(needed), stream);
            return needed;
        }

        extern "C" int djinnJitPrintf(const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            const int r = vformat(stdout, format, args);
            va_end(args);
            return r;
        }

        extern "C" int djinnJitFprintf(FILE* stream, const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            const int r = vformat(stream, format, args);
            va_end(args);
            return r;
        }

        extern "C" int djinnJitSnprintf(char* dest, size_t size, const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            const int r = vsnprintf(dest, size, format, args);
            va_end(args);
            return r;
        }

        extern "C" int djinnJitSprintf(char* dest, const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            const int r = vsnprintf(dest, SIZE_MAX, format, args);
            va_end(args);
            return r;
        }

        extern "C" int djinnJitPuts(const char* s)
        {
            const auto len = std::strlen(s);
            fwrite(s, 1, len, stdout);
            fputc('\n', stdout);
            return static_cast<int>(len) + 1;
        }

        extern "C" int djinnJitPutchar(int c)
        {
            return fputc(c, stdout);
        }

        extern "C" int djinnJitWrite(int fd, const void* buffer, size_t count)
        {
#ifdef _WIN32
            return _write(fd, buffer, static_cast<unsigned>(count));
#else
            return static_cast<int>(::write(fd, buffer, count));
#endif
        }

        // CRT-inline helper referenced by the clang-compiled runtime bitcode
        // (0 = default printf behavior).
        extern "C" unsigned long long djinnJitStdioPrintfOptions() { return 0; }

        std::string hashFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            std::string data((std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
            uint64_t hash = 1469598103934665603ull;
            for (const char c : data)
            {
                hash ^= static_cast<unsigned char>(c);
                hash *= 1099511628211ull;
            }
            return std::to_string(hash);
        }

        const std::filesystem::path& runtimeCacheDir()
        {
            static const std::filesystem::path dir =
                std::filesystem::temp_directory_path() / "djinn_build" / "runtime_cache";
            return dir;
        }

        // Compiles one runtime C source to bitcode via clang, cached on disk by
        // content hash so repeated runs (and parallel test processes) pay once.
        bool ensureRuntimeBitcode(const fs::path& source)
        {
            if (!fs::exists(source)) return false;

            const auto bc = runtimeCacheDir() / (source.stem().string() + "_" + hashFile(source) + ".bc");
            if (fs::exists(bc)) return true;

            fs::create_directories(runtimeCacheDir());
            auto tmp = bc;
            tmp += ".tmp";
            // Outer quotes force cmd.exe's /S /C "command" form, which keeps the
            // inner quotes around paths with spaces (e.g. C:/Program Files/...)
            const auto cmd = std::string("\"\"") + DJINN_CLANG_PATH "\" -emit-llvm -c -O1 \""
                + source.string() + "\" -I\"" + source.parent_path().string() + "\" -o \"" + tmp.string() + "\"\"";
            if (system(cmd.c_str()) != 0 || !fs::exists(tmp))
                return false;
            fs::rename(tmp, bc); // atomic-ish publish for parallel readers
            return true;
        }

        const std::vector<std::filesystem::path>& runtimeBitcodePaths()
        {
        static std::vector<std::filesystem::path> paths;
        static std::once_flag once;
        std::call_once(once, []
        {
            const fs::path runtimeDir = fs::path(DJINN_SOURCE_DIR) / "runtime";
            for (const char* name : {"djinn_runtime", "logger"})
            {
                const auto source = runtimeDir / (std::string(name) + ".c");
                if (ensureRuntimeBitcode(source))
                    paths.push_back(runtimeCacheDir() / (std::string(name) + "_" + hashFile(source) + ".bc"));
            }
        });
        return paths;
        }

        bool initializeLlvmNative()
        {
            static std::once_flag once;
            bool ok = true;
            std::call_once(once, [&ok]
            {
                if (llvm::InitializeNativeTarget() || llvm::InitializeNativeTargetAsmPrinter()
                    || llvm::InitializeNativeTargetAsmParser())
                    ok = false;
            });
            return ok;
        }
    }

    bool jitRuntimeAvailable()
    {
#ifdef __SANITIZE_ADDRESS__
        // Sanitizer builds: the non-standard thread lifecycle of the JIT
        // exit/abort interception corrupts the ASan runtime at process
        // exit. JIT'd code is not instrumented anyway, so fall back to the
        // clang path there.
        return false;
#else
        return initializeLlvmNative() && runtimeBitcodePaths().size() == 2;
#endif
    }

    int executeModule(std::unique_ptr<llvm::Module> module, std::unique_ptr<llvm::LLVMContext> context,
                      const int optimizationLevel)
    {
        using namespace llvm;
        using namespace llvm::orc;

#define JIT_FAIL(errExpr) do { \
    auto _jitErr = (errExpr); \
    llvm::logAllUnhandledErrors(std::move(_jitErr), llvm::errs(), "[djinn-jit] "); \
    return -1; } while (false)

        if (!jitRuntimeAvailable())
        {
            fprintf(stderr, "[djinn-jit] runtime bitcode unavailable\n");
            return -1;
        }

        const auto optLevel = std::clamp(optimizationLevel, 0, 3);
        auto jtmbResult = JITTargetMachineBuilder::detectHost();
        if (!jtmbResult)
            JIT_FAIL(jtmbResult.takeError());
        jtmbResult->setCodeGenOptLevel(static_cast<CodeGenOptLevel>(optLevel));

        // Populate every std::optional field of the builder ourselves: the
        // prebuilt LLVM library's prepareForConstruction() writes to them
        // without the MSVC STL's ASan container annotations, tripping
        // use-after-poison false positives in Debug+ASan builds.
        auto dlResult = jtmbResult->getDefaultDataLayoutForTarget();
        if (!dlResult)
            JIT_FAIL(dlResult.takeError());
        auto builder = LLJITBuilder();
        builder.setJITTargetMachineBuilder(std::move(*jtmbResult));
        builder.setDataLayout(std::move(*dlResult));
        builder.setSupportConcurrentCompilation(false); // matches the 0-thread default
        auto jitResult = builder.create();
        if (!jitResult)
            JIT_FAIL(jitResult.takeError());
        auto& jit = *jitResult;

        auto& dylib = jit->getMainJITDylib();

        // Host process symbols (printf, malloc, ...) — but keep the terminating
        // ones for ourselves so a program "exit" doesn't kill the test process.
        auto procGen = DynamicLibrarySearchGenerator::GetForCurrentProcess(
            ' ', [](const SymbolStringPtr& symbol)
            {
                const auto name = (*symbol).str();
                // terminating + CRT-inline/oldnames handled by host wrappers above
                return name != "exit" && name != "abort" && name != "_exit"
                    && name != "printf" && name != "fprintf" && name != "sprintf"
                    && name != "snprintf" && name != "puts" && name != "putchar"
                    && name != "write";
            });
        if (procGen)
        {
            dylib.addGenerator(std::move(*procGen));
        }
        else
        {
            JIT_FAIL(procGen.takeError());
        }

        // The host test process may not import these (e.g. static CRT, or no
        // winsock user), so load them explicitly for the runtime bitcode.
        for (const char* dll : {"ucrtbase.dll", "ws2_32.dll"})
        {
            auto dllGen = DynamicLibrarySearchGenerator::Load(dll, ' ');
            if (dllGen)
                dylib.addGenerator(std::move(*dllGen));
            else
                consumeError(dllGen.takeError());
        }

        const auto exported = JITSymbolFlags::Exported;
        SymbolMap hostWrappers;
        hostWrappers[jit->mangleAndIntern("exit")] =
            {ExecutorAddr::fromPtr(&djinnJitExit), exported};
        hostWrappers[jit->mangleAndIntern("abort")] =
            {ExecutorAddr::fromPtr(&djinnJitAbort), exported};
        hostWrappers[jit->mangleAndIntern("printf")] =
            {ExecutorAddr::fromPtr(&djinnJitPrintf), exported};
        hostWrappers[jit->mangleAndIntern("fprintf")] =
            {ExecutorAddr::fromPtr(&djinnJitFprintf), exported};
        hostWrappers[jit->mangleAndIntern("sprintf")] =
            {ExecutorAddr::fromPtr(&djinnJitSprintf), exported};
        hostWrappers[jit->mangleAndIntern("snprintf")] =
            {ExecutorAddr::fromPtr(&djinnJitSnprintf), exported};
        hostWrappers[jit->mangleAndIntern("puts")] =
            {ExecutorAddr::fromPtr(&djinnJitPuts), exported};
        hostWrappers[jit->mangleAndIntern("putchar")] =
            {ExecutorAddr::fromPtr(&djinnJitPutchar), exported};
        hostWrappers[jit->mangleAndIntern("write")] =
            {ExecutorAddr::fromPtr(&djinnJitWrite), exported};
        hostWrappers[jit->mangleAndIntern("__local_stdio_printf_options")] =
            {ExecutorAddr::fromPtr(&djinnJitStdioPrintfOptions), exported};
        // __djinn_runtime_error is defined by the runtime bitcode itself and
        // routes through abort(), which is intercepted above.
        if (auto err = dylib.define(absoluteSymbols(std::move(hostWrappers))))
            JIT_FAIL(std::move(err));

        for (const auto& bcPath : runtimeBitcodePaths())
        {
            auto buffer = MemoryBuffer::getFile(bcPath.string());
            if (!buffer)
            {
                fprintf(stderr, "[djinn-jit] cannot read %s\n", bcPath.string().c_str());
                return -1;
            }
            auto bcContext = std::make_unique<LLVMContext>();
            auto bcModule = parseBitcodeFile((*buffer)->getMemBufferRef(), *bcContext);
            if (!bcModule)
                JIT_FAIL(bcModule.takeError());
            if (auto err = jit->addIRModule(ThreadSafeModule(std::move(*bcModule), std::move(bcContext))))
                JIT_FAIL(std::move(err));
        }

        if (auto err = jit->addIRModule(ThreadSafeModule(std::move(module), std::move(context))))
            JIT_FAIL(std::move(err));

        auto mainAddr = jit->lookup("main");
        if (!mainAddr)
            JIT_FAIL(mainAddr.takeError());

        // main is generated as `i32 @main()` with no parameters. It runs on a
        // dedicated thread: on exit/abort the thread parks itself (see
        // terminateJitMain) and is detached below.
        const auto mainFn = mainAddr->toPtr<int (*)()>();
        std::atomic<int> normalExit{-2};
        terminatedExitCode.store(-2, std::memory_order_release);

        std::thread jitThread([&normalExit, mainFn]
        {
            normalExit.store(runMainProtected(mainFn), std::memory_order_release);
        });

        while (normalExit.load(std::memory_order_acquire) == -2
               && terminatedExitCode.load(std::memory_order_acquire) == -2)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (normalExit.load(std::memory_order_acquire) != -2)
        {
            jitThread.join();
            return normalExit.load(std::memory_order_acquire);
        }

        // The program terminated via abort/exit: its thread is parked forever
        // and the runtime pool threads are orphaned mid-execution, both still
        // referencing JIT'd code — leak the LLJIT instead of freeing code
        // under their feet.
        jitThread.detach();
        // True leak (heap, never destroyed): a function-local static would run
        // its destructor at process exit and tear down JIT'd code while the
        // parked thread and orphaned runtime pool threads still reference it.
        static auto* const abandonedJits = new std::vector<std::unique_ptr<llvm::orc::LLJIT>>();
        abandonedJits->push_back(std::move(*jitResult));
        return terminatedExitCode.load(std::memory_order_acquire);
    }
}