#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "djinn_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "logger.h"
Logger* logger;

#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#include <io.h>
#include <intrin.h>
#include <dbghelp.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
#else
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif

// #define DJINN_ENABLE_TRACE
#ifdef DJINN_ENABLE_TRACE
#define DJINN_TRACE(message, ...) do {                         \
DJINN_ASSERT(logger, "Logger not initlizatied properly!"); \
logger_trace(logger, message, ##__VA_ARGS__);              \
} while(0)
#else
#define DJINN_TRACE(message, ...) do {} while (0)
#endif

uint64_t __djinn_hash_string(const char* data, uint32_t length)
{
    uint64_t hash = 2166136261u;
    for (uint32_t i = 0; i < length; ++i)
    {
        hash ^= (uint8_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

int64_t __djinn_unix_timestamp_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

uint32_t __djinn_compare_strings(
    char* leftData,
    char* rightData,
    size_t leftLen,
    size_t rightLen
)
{
    if (leftData == rightData) return 1;

    if (!leftData || !rightData)
        return 0;

    if (leftLen != rightLen)
        return 0;

    return memcmp(leftData, rightData, leftLen) == 0;
}

static djinn_runtime_t runtime;
static void* waiting_handles[DJINN_MAX_WAITING];
static int waiting_count = 0;
static djinn_continuation_t* continuations = NULL;

#ifdef _WIN32
static LPFN_ACCEPTEX pfnAcceptEx = NULL;
static LPFN_CONNECTEX pfnConnectEx = NULL;
#endif

DJINN_ATTR_NONNULL_1
void __djinn_free(void* pointer)
{
    DJINN_TRACE("de-allocating heap memory at %p", pointer);
    free(pointer);
}

DJINN_ATTR_REALLOC
void* __djinn_realloc(void* pointer, size_t new_size)
{
    void* chunk = realloc(pointer, new_size);
    DJINN_ASSERT(chunk, "Out of memory");
    DJINN_TRACE("re-allocated size %zu on heap at %p", new_size, chunk);
    return chunk;
}

DJINN_ATTR_MALLOC
void* __djinn_malloc(size_t size)
{
    DJINN_ASSERT(size > 0, "Size need be greater than zero!");
    void* chunk = calloc(1, size);
    DJINN_ASSERT(chunk, "Out of memory");
    DJINN_TRACE("allocated size %zu on heap at %p", size, chunk);
    return chunk;
}

static void init_queue(djinn_task_queue_t* q)
{
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
#ifdef _WIN32
    InitializeCriticalSection(&q->mutex);
    InitializeConditionVariable(&q->cond);
#else
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
#endif
}

static void destroy_queue(djinn_task_queue_t* q)
{
    djinn_task_t* t = q->head;
    while (t)
    {
        djinn_task_t* next = t->next;
        free(t);
        t = next;
    }
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
#ifdef _WIN32
    DeleteCriticalSection(&q->mutex);
#else
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
#endif
}

static void enqueue_task(djinn_task_queue_t* q, void* handle)
{
    djinn_task_t* task = (djinn_task_t*)malloc(sizeof(djinn_task_t));
    if (!task) return;
    task->handle = handle;
    task->next = NULL;
    task->priority = 0;

#ifdef _WIN32
    EnterCriticalSection(&q->mutex);
#else
    pthread_mutex_lock(&q->mutex);
#endif

    if (q->tail)
    {
        q->tail->next = task;
    }
    else
    {
        q->head = task;
    }
    q->tail = task;
    q->count++;

#ifdef _WIN32
    WakeConditionVariable(&q->cond);
    LeaveCriticalSection(&q->mutex);
#else
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
#endif
}

static djinn_task_t* dequeue_task(djinn_task_queue_t* q)
{
#ifdef _WIN32
    EnterCriticalSection(&q->mutex);
#else
    pthread_mutex_lock(&q->mutex);
#endif

    djinn_task_t* task = q->head;
    if (task)
    {
        q->head = task->next;
        if (!q->head) q->tail = NULL;
        q->count--;
        task->next = NULL;
    }

#ifdef _WIN32
    LeaveCriticalSection(&q->mutex);
#else
    pthread_mutex_unlock(&q->mutex);
#endif

    return task;
}

static djinn_task_t* dequeue_task_blocking(djinn_task_queue_t* q, volatile int* shutdown)
{
#ifdef _WIN32
    EnterCriticalSection(&q->mutex);
    while (!q->head && !(*shutdown))
    {
        SleepConditionVariableCS(&q->cond, &q->mutex, 100);
    }
#else
    pthread_mutex_lock(&q->mutex);
    while (!q->head && !(*shutdown))
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000; // 100ms
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&q->cond, &q->mutex, &ts);
    }
#endif

    djinn_task_t* task = q->head;
    if (task)
    {
        q->head = task->next;
        if (!q->head) q->tail = NULL;
        q->count--;
        task->next = NULL;
    }

#ifdef _WIN32
    LeaveCriticalSection(&q->mutex);
#else
    pthread_mutex_unlock(&q->mutex);
#endif

    return task;
}

void __djinn_mark_waiting(void* handle)
{
    if (waiting_count < DJINN_MAX_WAITING)
    {
        waiting_handles[waiting_count++] = handle;
    }
}

static int is_in_waiting_set(void* handle)
{
    for (int i = 0; i < waiting_count; i++)
    {
        if (waiting_handles[i] == handle)
            return 1;
    }
    return 0;
}

static int is_waiting(void* handle)
{
    for (int i = 0; i < waiting_count; i++)
    {
        if (waiting_handles[i] == handle)
        {
            waiting_handles[i] = waiting_handles[waiting_count - 1];
            waiting_count--;
            return 1;
        }
    }
    return 0;
}

static void remove_from_waiting(void* handle)
{
    is_waiting(handle);
}

static void* pop_continuation(void* child)
{
    djinn_continuation_t** pp = &continuations;
    while (*pp)
    {
        if ((*pp)->child == child)
        {
            void* parent = (*pp)->parent;
            djinn_continuation_t* to_free = *pp;
            *pp = (*pp)->next;
            free(to_free);
            return parent;
        }
        pp = &(*pp)->next;
    }
    return NULL;
}

void __djinn_await(void* child_handle, void* parent_handle)
{
    DJINN_ASSERT(child_handle, "child handle is NULL");
    DJINN_ASSERT(parent_handle, "parent handle is NULL");

    djinn_continuation_t* cont = (djinn_continuation_t*)malloc(sizeof(djinn_continuation_t));
    if (!cont) return;
    cont->child = child_handle;
    cont->parent = parent_handle;
    cont->next = continuations;
    continuations = cont;

    __djinn_mark_waiting(parent_handle);

    if (!is_in_waiting_set(child_handle))
    {
        enqueue_task(&runtime.ready_queue, child_handle);
    }
}

#ifdef _WIN32
static DWORD WINAPI file_io_thread_func(LPVOID arg)
{
#else
    static void* file_io_thread_func(void* arg)
    {


#endif
    (void)arg;
    while (runtime.running)
    {
#ifdef _WIN32
        EnterCriticalSection(&runtime.file_io_mutex);
#else
        pthread_mutex_lock(&runtime.file_io_mutex);
#endif

        djinn_io_request_t* req = runtime.io_pending;
        if (req)
        {
            runtime.io_pending = req->next;
        }

#ifdef _WIN32
        LeaveCriticalSection(&runtime.file_io_mutex);
#else
        pthread_mutex_unlock(&runtime.file_io_mutex);
#endif

        if (req)
        {
            if (req->type == DJINN_IO_FILE_READ)
            {
#ifdef _WIN32
                req->result = _read(req->fd, req->buffer, (unsigned int)req->count);
#else
                req->result = read(req->fd, req->buffer, (size_t)req->count);
#endif
            }
            else if (req->type == DJINN_IO_FILE_WRITE)
            {
#ifdef _WIN32
                req->result = _write(req->fd, req->buffer, (unsigned int)req->count);
#else
                req->result = write(req->fd, req->buffer, (size_t)req->count);
#endif
            }
            req->completed = 1;

            if (req->waiting_coro)
            {
                enqueue_task(&runtime.ready_queue, req->waiting_coro);
            }
            free(req);
        }
        else
        {
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif
        }
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}


#ifdef _WIN32

#define REQ_FROM_OVERLAPPED(ovl) \
    ((djinn_io_request_t*)((char*)(ovl) - offsetof(djinn_io_request_t, overlapped)))

static DWORD WINAPI socket_poller_thread_func(LPVOID arg)
{
    (void)arg;
    DWORD bytes_transferred;
    ULONG_PTR completion_key;
    OVERLAPPED* ovl;

    while (runtime.running)
    {
        const BOOL ok = GetQueuedCompletionStatus(
            runtime.iocp,
            &bytes_transferred,
            &completion_key,
            &ovl,
            100 // 100ms timeout
        );

        if (!runtime.running) break;

        if (!ovl)
        {
            continue;
        }

        djinn_io_request_t* req = REQ_FROM_OVERLAPPED(ovl);
        req->completed = 1;

        DJINN_TRACE("IOCP completion: type=%d, ok=%d, bytes=%lu", req->type, (int)ok, bytes_transferred);

        DWORD err = ok ? 0 : GetLastError();
        if (err == ERROR_NETNAME_DELETED || err == ERROR_CONNECTION_ABORTED)
        {
            req->result = 0;
        }
        else if (err != 0)
        {
            req->result = -1;
        }
        else
        {
            switch (req->type)
            {
            case DJINN_IO_ACCEPT:
                req->result = (int64_t)req->accepted_socket;
                DJINN_TRACE("accept completed: client_fd=%lld", (long long)req->result);
                // Update accepted socket context so it inherits server socket properties
                setsockopt(
                    req->accepted_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                    (char*)&req->socket, sizeof(req->socket)
                );
                // Associate accepted socket with IOCP
                CreateIoCompletionPort((HANDLE)req->accepted_socket, runtime.iocp, 0, 0);
                break;

            case DJINN_IO_CONNECT:
                req->result = 0; // success
                setsockopt(
                    (SOCKET)req->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT,
                    NULL, 0
                );
                break;

            case DJINN_IO_RECV:
                {
                    if (bytes_transferred == 0)
                    {
                        req->result = 0;
                        DJINN_TRACE("socket closed by peer: %lld", (long long)req->socket);
                    }
                    else
                    {
                        req->result = (int64_t)bytes_transferred;
                        DJINN_TRACE("recv completed: bytes=%lld socket=%lld",
                                    (long long)req->result,
                                    (long long)req->socket);
                    }
                    break;
                }

            case DJINN_IO_SEND:
                req->result = (int64_t)bytes_transferred;
                DJINN_TRACE("send completed: bytes=%lld, socket=%lld", (long long)req->result, (long long)req->socket);
                break;

            default:
                req->result = (int64_t)bytes_transferred;
                break;
            }
        }

        if (req->out_result)
        {
            *req->out_result = req->result;
            DJINN_TRACE("wrote out_result=%lld to %p", (long long)req->result, (void*)req->out_result);
        }

        if (req->waiting_coro)
        {
            DJINN_TRACE("resuming coro %p after IO type=%d", req->waiting_coro, req->type);
            enqueue_task(&runtime.ready_queue, req->waiting_coro);
        }
        else
        {
            DJINN_TRACE("WARNING: no waiting_coro for IO type=%d", req->type);
        }
        free(req);
    }

    return 0;
}

#else // Linux — epoll

static void* socket_poller_thread_func(void* arg)
{
    (void)arg;
    struct epoll_event events[64];

    while (runtime.running)
    {
        int n = epoll_wait(runtime.epoll_fd, events, 64, 100);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            DJINN_TRACE("epoll_wait error: %d", errno);
            continue;
        }

        for (int i = 0; i < n; i++)
        {
            djinn_io_request_t* req = (djinn_io_request_t*)events[i].data.ptr;
            if (!req) continue;

            req->completed = 1;

            switch (req->type)
            {
            case DJINN_IO_ACCEPT:
                {
                    struct sockaddr_in client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    int client_fd = accept4(
                        (int)req->socket, (struct sockaddr*)&client_addr,
                        &addr_len, SOCK_NONBLOCK
                    );
                    if (client_fd < 0)
                    {
                        req->result = -1;
                    }
                    else
                    {
                        req->result = (int64_t)client_fd;
                    }
                    // Remove server socket from epoll (one-shot for accept)
                    epoll_ctl(runtime.epoll_fd, EPOLL_CTL_DEL, (int)req->socket, NULL);
                    break;
                }

            case DJINN_IO_CONNECT:
                {
                    // Check if connect succeeded
                    int err = 0;
                    socklen_t len = sizeof(err);
                    getsockopt((int)req->socket, SOL_SOCKET, SO_ERROR, &err, &len);
                    req->result = (err == 0) ? 0 : -1;
                    epoll_ctl(runtime.epoll_fd, EPOLL_CTL_DEL, (int)req->socket, NULL);
                    break;
                }

            case DJINN_IO_RECV:
                {
                    ssize_t n_read = recv((int)req->socket, req->buffer, (size_t)req->count, 0);
                    if (n_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    {
                        req->completed = 0;
                        continue; // not ready yet, epoll will fire again
                    }
                    req->result = (int64_t)n_read;
                    epoll_ctl(runtime.epoll_fd, EPOLL_CTL_DEL, (int)req->socket, NULL);
                    break;
                }

            case DJINN_IO_SEND:
                {
                    ssize_t n_sent = send((int)req->socket, req->buffer, (size_t)req->count, 0);
                    if (n_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    {
                        req->completed = 0;
                        continue;
                    }
                    req->result = (int64_t)n_sent;
                    epoll_ctl(runtime.epoll_fd, EPOLL_CTL_DEL, (int)req->socket, NULL);
                    break;
                }

            default:
                break;
            }

            if (req->completed)
            {
                if (req->out_result)
                    *req->out_result = req->result;

                if (req->waiting_coro)
                    enqueue_task(&runtime.ready_queue, req->waiting_coro);

                free(req);
            }
        }
    }

    return NULL;
}

#endif // _WIN32

#ifdef _WIN32
static DWORD WINAPI worker_thread(LPVOID arg)
{
#else
    static void* worker_thread(void* arg)
    {

#endif
    djinn_thread_pool_t* pool = (djinn_thread_pool_t*)arg;

    while (!pool->shutdown)
    {
        djinn_task_t* task = dequeue_task_blocking(&pool->queue, &pool->shutdown);
        if (!task) continue;

        if (!__djinn_coro_done(task->handle))
        {
            __djinn_coro_resume(task->handle);
            if (!__djinn_coro_done(task->handle))
            {
                enqueue_task(&pool->queue, task->handle);
            }
            else
            {
                __djinn_coro_destroy(task->handle);
            }
        }
        free(task);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

// ---------------------------------------------------------------------------
// Runtime error reporting: rich traps with source location, operand values
// and a shadow call-stack trace. Mirrors the compile-time diagnostic style
// ( --> file:line:col, snippet, caret underline, = note) so both look alike.
// ---------------------------------------------------------------------------

char __djinn_last_error_report[DJINN_ERROR_REPORT_SIZE];

// Thread-local error state written by generated throw sites and read by
// propagation checks (single definition owned by the runtime — generated
// modules declare it extern, so linked libraries share one state).
DJINN_TLS djinn_errno_t __djinn_errno = {0, 0, NULL, NULL, NULL, 0, 0};

static _Thread_local struct
{
    void* frames[DJINN_MAX_TRACE_FRAMES];
    int count;
} djinn_error_trace;

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))

// Validated frame-pointer walk: JIT-compiled frames carry no unwind info, so
// CaptureStackBackTrace cannot walk through them. Every hop is bounds-checked
// against the TEB stack limits; a broken chain just truncates the trace.
static int djinn_walk_frame_pointers(void** frames, const int max)
{
    const uintptr_t stack_base = (uintptr_t)__readgsqword(0x08);
    const uintptr_t stack_limit = (uintptr_t)__readgsqword(0x10);
    if (stack_base <= stack_limit) return 0;

    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    int count = 0;
    while (count < max)
    {
        if (fp < stack_limit || fp % sizeof(void*) != 0
            || fp + 2 * sizeof(void*) > stack_base)
            break;
        frames[count++] = (void*)((uintptr_t*)fp)[1];
        const uintptr_t next = ((uintptr_t*)fp)[0];
        if (next <= fp || next >= stack_base || next % sizeof(void*) != 0)
            break;
        fp = next;
    }
    return count;
}
#endif

static int djinn_capture_frames(void** frames, const int max)
{
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    const int walked = djinn_walk_frame_pointers(frames, max);
    if (walked >= 2) return walked;
    // Optimized builds drop frame pointers — the OS unwinder reads .pdata
    return (int)CaptureStackBackTrace(1, (DWORD)max, frames, NULL);
#else
    void* scratch[DJINN_MAX_TRACE_FRAMES + 4];
    int n = backtrace(scratch, max + 4);
    if (n <= 2) return 0;
    n -= 2; /* backtrace() + this helper */
    if (n > max) n = max;
    memcpy(frames, scratch + 2, (size_t)n * sizeof(void*));
    return n;
#endif
}

void __djinn_capture_backtrace(void)
{
    djinn_error_trace.count =
        djinn_capture_frames(djinn_error_trace.frames, DJINN_MAX_TRACE_FRAMES);
}

// Shared with the native-exceptions shim: captures into caller storage so
// the thrown error object carries its own raise-site trace.
int __djinn_capture_backtrace_into(void** frames, const int max)
{
    return djinn_capture_frames(frames, max);
}

// ── JIT symbol registry ──

#define DJINN_MAX_JIT_SYMBOLS 4096

typedef struct
{
    const void* address;
    char* name;
} djinn_jit_symbol_t;

static struct
{
    djinn_jit_symbol_t items[DJINN_MAX_JIT_SYMBOLS];
    int count;
} djinn_jit_symbols;

static int djinn_jit_symbol_cmp(const void* a, const void* b)
{
    const djinn_jit_symbol_t* left = (const djinn_jit_symbol_t*)a;
    const djinn_jit_symbol_t* right = (const djinn_jit_symbol_t*)b;
    if (left->address < right->address) return -1;
    if (left->address > right->address) return 1;
    return 0;
}

void __djinn_jit_register_symbols(const char* const* names, const void* const* addresses,
                                  const int count)
{
    for (int i = 0; i < count; i++)
    {
        if (djinn_jit_symbols.count >= DJINN_MAX_JIT_SYMBOLS) break;
        char* copy = strdup(names[i]);
        if (!copy) break;
        djinn_jit_symbols.items[djinn_jit_symbols.count].address = addresses[i];
        djinn_jit_symbols.items[djinn_jit_symbols.count].name = copy;
        djinn_jit_symbols.count++;
    }
    qsort(djinn_jit_symbols.items, (size_t)djinn_jit_symbols.count,
          sizeof(djinn_jit_symbol_t), djinn_jit_symbol_cmp);
}

// Nearest registered symbol at or below addr; a function owns everything up
// to the next registered address.
static const char* djinn_jit_lookup(const void* addr)
{
    int lo = 0;
    int hi = djinn_jit_symbols.count - 1;
    int found = -1;
    while (lo <= hi)
    {
        const int mid = lo + (hi - lo) / 2;
        if (djinn_jit_symbols.items[mid].address <= addr)
        {
            found = mid;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    if (found < 0) return NULL;
    return djinn_jit_symbols.items[found].name;
}

// ── Symbolization ──

#ifdef _WIN32

typedef BOOL(WINAPI* djinn_SymInitialize_fn)(HANDLE, PCSTR, BOOL);
typedef BOOL(WINAPI* djinn_SymFromAddr_fn)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
typedef BOOL(WINAPI* djinn_SymGetLineFromAddr64_fn)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);

static struct
{
    HMODULE module;
    djinn_SymInitialize_fn initialize;
    djinn_SymFromAddr_fn from_addr;
    djinn_SymGetLineFromAddr64_fn get_line;
    int failed;
} djinn_dbghelp;

// Loaded lazily on first symbolization (error paths only): the happy path
// never touches dbghelp, and JIT hosts need no link-time dependency on it.
static void djinn_dbghelp_init(void)
{
    if (djinn_dbghelp.module || djinn_dbghelp.failed) return;

    djinn_dbghelp.module = LoadLibraryA("dbghelp.dll");
    if (!djinn_dbghelp.module)
    {
        djinn_dbghelp.failed = 1;
        return;
    }

    djinn_dbghelp.initialize = (djinn_SymInitialize_fn)(void(*)(void))
    GetProcAddress(djinn_dbghelp.module, "SymInitialize");
    djinn_dbghelp.from_addr = (djinn_SymFromAddr_fn)(void(*)(void))
    GetProcAddress(djinn_dbghelp.module, "SymFromAddr");
    djinn_dbghelp.get_line = (djinn_SymGetLineFromAddr64_fn)(void(*)(void))
    GetProcAddress(djinn_dbghelp.module, "SymGetLineFromAddr64");

    if (!djinn_dbghelp.initialize || !djinn_dbghelp.from_addr || !djinn_dbghelp.get_line
        || !djinn_dbghelp.initialize(GetCurrentProcess(), NULL, TRUE))
    {
        djinn_dbghelp.failed = 1;
    }
}

static int djinn_symbolize_frame(const void* addr, char* name, const size_t name_size,
                                 char* file, const size_t file_size, uint32_t* line)
{
    djinn_dbghelp_init();
    if (djinn_dbghelp.failed) return 0;

    char buffer[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 displacement = 0;
    if (!djinn_dbghelp.from_addr(GetCurrentProcess(), (DWORD64)(uintptr_t)addr,
                                 &displacement, symbol))
        return 0;
    snprintf(name, name_size, "%s", symbol->Name);

    IMAGEHLP_LINE64 line_info;
    memset(&line_info, 0, sizeof line_info);
    line_info.SizeOfStruct = sizeof line_info;
    DWORD line_displacement = 0;
    if (djinn_dbghelp.get_line(GetCurrentProcess(), (DWORD64)(uintptr_t)addr,
                               &line_displacement, &line_info)
        && line_info.LineNumber != 0 && line_info.FileName != NULL)
    {
        snprintf(file, file_size, "%s", line_info.FileName);
        *line = (uint32_t)line_info.LineNumber;
    }
    return 1;
}

void __djinn_symbolizer_set_path(const char* path)
{
    (void)path; /* line info comes from dbghelp/PDB on Windows */
}

#else

#include <spawn.h>

extern char** environ;

#ifdef DJINN_SYMBOLIZER_PATH
static char djinn_symbolizer_path[512] = DJINN_SYMBOLIZER_PATH;
#else
static char djinn_symbolizer_path[512] = "llvm-symbolizer";
#endif
static const char* djinn_self_path;

void __djinn_symbolizer_set_path(const char* path)
{
    if (path != NULL && path[0] != '\0')
        snprintf(djinn_symbolizer_path, sizeof djinn_symbolizer_path, "%s", path);
}

static int djinn_symbolize_frame(const void* addr, char* name, const size_t name_size,
                                 char* file, const size_t file_size, uint32_t* line)
{
    (void)file;
    (void)file_size;
    (void)line;
    Dl_info info;
    if (!dladdr(addr, &info) || info.dli_sname == NULL) return 0;
    snprintf(name, name_size, "%s", info.dli_sname);
    return 1;
}

// Asks llvm-symbolizer — one batched process per report, spawned with a plain
// argv and no shell — for the file:line of every frame. Entries are left
// untouched when the tool is unavailable, so traces degrade gracefully to
// dladdr names.
static void djinn_fill_frame_lines(void* const* frames, const int count,
                                   char (*files)[192], uint32_t* lines)
{
    if (count <= 0 || djinn_symbolizer_path[0] == '\0') return;

    if (djinn_self_path == NULL)
    {
        Dl_info info;
        if (dladdr((const void*)&__djinn_runtime_init, &info) && info.dli_fname != NULL)
            djinn_self_path = info.dli_fname;
    }
    if (djinn_self_path == NULL) return;

    static const char* const flags[] = {"--functions=none", "--inlines=false"};
    char addr_text[DJINN_MAX_TRACE_FRAMES][24];
    char* argv[DJINN_MAX_TRACE_FRAMES + 5];
    int argc = 0;
    argv[argc++] = djinn_symbolizer_path;
    static const char obj_prefix[] = "--obj=";
    static char obj_arg[1024];
    snprintf(obj_arg, sizeof obj_arg, "%s%s", obj_prefix, djinn_self_path);
    argv[argc++] = obj_arg;
    argv[argc++] = (char*)flags[0];
    argv[argc++] = (char*)flags[1];
    for (int i = 0; i < count; i++)
    {
        snprintf(addr_text[i], sizeof addr_text[i], "0x%llx",
                 (unsigned long long)(uintptr_t)frames[i]);
        argv[argc++] = addr_text[i];
    }
    argv[argc] = NULL;

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) return;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);

    pid_t child;
    if (posix_spawnp(&child, djinn_symbolizer_path, &actions, NULL, argv, environ) != 0)
    {
        posix_spawn_file_actions_destroy(&actions);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return;
    }
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fds[1]);

    FILE* out = fdopen(pipe_fds[0], "r");
    if (out == NULL)
    {
        close(pipe_fds[0]);
        waitpid(child, NULL, 0);
        return;
    }

    char output[512];
    for (int i = 0; i < count; i++)
    {
        if (fgets(output, sizeof output, out) == NULL) break;
        output[strcspn(output, "\n")] = '\0';

        // "file:line:column" ("??:0:0" when unknown)
        char* colon = strrchr(output, ':');
        if (colon == NULL) continue;
        *colon = '\0';
        colon = strrchr(output, ':');
        if (colon == NULL) continue;
        *colon = '\0';
        const unsigned long parsed = strtoul(colon + 1, NULL, 10);
        if (parsed == 0 || output[0] == '\0' || strcmp(output, "??") == 0) continue;
        snprintf(files[i], 192, "%s", output);
        lines[i] = (uint32_t)parsed;
    }

    fclose(out);
    waitpid(child, NULL, 0);
}
#endif

// ── Variable assignment history ──

typedef struct
{
    const char* file;
    uint32_t line;
} djinn_var_event_t;

typedef struct
{
    const void* slot;
    const char* name;
    const char* source_root;
    djinn_var_event_t events[DJINN_VAR_HISTORY]; /* newest last */
    int count; /* events stored (0..DJINN_VAR_HISTORY) */
    int total; /* total assignments seen (drives the "..." marker) */
} djinn_tracked_var_t;

static struct
{
    djinn_tracked_var_t vars[DJINN_MAX_TRACKED_VARS];
    int count;
} djinn_var_registry;

void __djinn_var_track(const void* slot, const char* name, const char* file,
                       const char* source_root, const uint32_t line)
{
    if (slot == NULL) return;

    djinn_tracked_var_t* var = NULL;
    for (int i = djinn_var_registry.count - 1; i >= 0; i--)
    {
        if (djinn_var_registry.vars[i].slot == slot)
        {
            var = &djinn_var_registry.vars[i];
            break;
        }
    }
    if (var == NULL)
    {
        if (djinn_var_registry.count >= DJINN_MAX_TRACKED_VARS) return;
        var = &djinn_var_registry.vars[djinn_var_registry.count++];
        memset(var, 0, sizeof *var);
        var->slot = slot;
        var->name = name;
    }
    var->source_root = source_root;

    if (var->count == DJINN_VAR_HISTORY)
    {
        var->events[0] = var->events[1];
        var->count--;
    }
    var->events[var->count].file = file;
    var->events[var->count].line = line;
    var->count++;
    var->total++;
}

static const djinn_tracked_var_t* djinn_find_tracked(const void* slot);

static int djinn_report_append(char* buf, int used, const char* fmt, ...)
{
    if (used >= DJINN_ERROR_REPORT_SIZE - 1) return used;
    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(buf + used, (size_t)(DJINN_ERROR_REPORT_SIZE - 1 - used), fmt, args);
    va_end(args);
    if (written < 0) return used;
    const int room = DJINN_ERROR_REPORT_SIZE - 1 - used;
    return used + (written > room ? room : written);
}

// Groups an integer string with '.' every three digits (2.000.000.000) so
// large values stay readable in the report.
static void djinn_group_digits(const char* digits, char* out, const size_t size)
{
    const char* p = digits;
    size_t out_idx = 0;
    if (*p == '-' && out_idx + 1 < size)
        out[out_idx++] = *p++;
    const size_t num_len = strlen(p);
    for (size_t i = 0; p[i] != '\0'; i++)
    {
        if (i > 0 && (num_len - i) % 3 == 0 && out_idx + 1 < size)
            out[out_idx++] = '.';
        if (out_idx + 1 < size)
            out[out_idx++] = p[i];
    }
    out[out_idx] = '\0';
}

static void djinn_format_value(const djinn_error_info_t* info, const uint64_t raw, char* out, const size_t size)
{
    char plain[32];
    if (info->is_signed)
        snprintf(plain, sizeof plain, "%lld", (long long)(int64_t)raw);
    else
        snprintf(plain, sizeof plain, "%llu", (unsigned long long)raw);
    djinn_group_digits(plain, out, size);
}

// Max/min of the operand type, returned as raw two's-complement bits.
static uint64_t djinn_type_max(const uint8_t bits, const uint8_t is_signed)
{
    if (!is_signed) return (bits >= 64) ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    return (bits >= 64) ? (uint64_t)INT64_MAX : (UINT64_C(1) << (bits - 1)) - 1;
}

static uint64_t djinn_type_min(const uint8_t bits)
{
    return (bits >= 64) ? (uint64_t)INT64_MIN : UINT64_C(1) << (bits - 1);
}

static int djinn_value_negative(const djinn_error_info_t* info, const uint64_t raw)
{
    return info->is_signed && (int64_t)raw < 0;
}

// The "= note:" line with the operand values and why they overflow.
// The computed result is only shown when it is representable in 64-bit math.
static int djinn_append_note(char* buf, int used, const djinn_error_info_t* info)
{
    if (!info->has_operands || info->op == 0 || info->bits == 0) return used;

    char left[32], right[32], type_name[8], bound[32];
    djinn_format_value(info, info->left, left, sizeof left);
    djinn_format_value(info, info->right, right, sizeof right);
    snprintf(type_name, sizeof type_name, "%s%u", info->is_signed ? "i" : "u", (unsigned)info->bits);

    const int wide = info->bits >= 64; /* exact 64-bit results may wrap */
    const uint8_t op = info->op;

    if (op == 'n')
    {
        djinn_format_value(info, djinn_type_max(info->bits, info->is_signed), bound, sizeof bound);
        return djinn_report_append(buf, used, "   = note: negate %s exceeds %s max (%s)\n",
                                   left, type_name, bound);
    }

    if (op == '/' && info->right == 0)
    {
        return djinn_report_append(buf, used, "   = note: division by zero: %s / 0\n", left);
    }

    used = djinn_report_append(buf, used, "   = note: %s %c %s", left, op, right);

    // Result of the overflowing operation, when it fits in 64-bit math.
    const int neg_left = djinn_value_negative(info, info->left);
    const int neg_right = djinn_value_negative(info, info->right);
    uint64_t result = 0;
    int have_result = 0;
    if (op == '/' && !wide)
    {
        // INT_MIN / -1 is the only division overflow; the quotient is |INT_MIN|
        result = neg_left ? (~info->left + 1) : info->left;
        have_result = 1;
    }
    else if (op == '+' && !wide)
    {
        result = info->left + info->right;
        have_result = 1;
    }
    else if (op == '-' && !wide)
    {
        result = info->left - info->right;
        have_result = 1;
    }
    else if (op == '*' && info->left != 0 && info->right != 0
             && info->left <= UINT64_MAX / info->right) /* fits in u64 */
    {
        const uint64_t product = info->left * info->right;
        if (!info->is_signed || (int64_t)product >= 0 || product == (uint64_t)INT64_MIN)
        {
            result = product;
            have_result = 1;
        }
    }

    if (have_result)
    {
        char result_text[32];
        djinn_format_value(info, result, result_text, sizeof result_text);
        used = djinn_report_append(buf, used, " = %s", result_text);
    }

    // Which bound was crossed. For signed +: equal signs decide; for -: the
    // operands' signs; for *: differing signs mean a negative product.
    const char* direction = NULL;
    if (info->is_signed)
    {
        const int exceeds_max =
            (op == '+' && !neg_left && !neg_right) ||
            (op == '-' && !neg_left && neg_right) ||
            (op == '*' && neg_left == neg_right) ||
            (op == '/' && neg_left);
        direction = exceeds_max ? "max" : "min";
    }
    else
    {
        direction = (op == '-') ? "min" : "max";
    }

    if (strcmp(direction, "max") == 0)
        djinn_format_value(info, djinn_type_max(info->bits, info->is_signed), bound, sizeof bound);
    else
    {
        char plain[32];
        snprintf(plain, sizeof plain, "%lld", (long long)(int64_t)djinn_type_min(info->bits));
        djinn_group_digits(plain, bound, sizeof bound);
    }

    used = djinn_report_append(buf, used, " %s %s %s (%s)\n",
                               strcmp(direction, "max") == 0 ? "exceeds" : "is below",
                               type_name, direction, bound);
    return used;
}

// ── In-memory source registry (JIT hosts) ──
//
// Sources compiled from memory have no file on disk for snippet extraction;
// the host registers their text before executing. Entries borrow the host's
// string (process lifetime) — no allocation.

#define DJINN_MAX_REGISTERED_SOURCES 8

static struct
{
    const char* file_id;
    const char* text;
} djinn_registered_sources[DJINN_MAX_REGISTERED_SOURCES];

static int djinn_registered_source_count = 0;

void __djinn_register_source_text(const char* file_id, const char* text)
{
    if (file_id == NULL || text == NULL) return;
    for (int i = 0; i < djinn_registered_source_count; i++)
    {
        if (strcmp(djinn_registered_sources[i].file_id, file_id) == 0)
        {
            djinn_registered_sources[i].text = text;
            return;
        }
    }
    if (djinn_registered_source_count < DJINN_MAX_REGISTERED_SOURCES)
    {
        djinn_registered_sources[djinn_registered_source_count].file_id = file_id;
        djinn_registered_sources[djinn_registered_source_count].text = text;
        djinn_registered_source_count++;
    }
}

static int djinn_source_line_from_memory(const char* text, const uint32_t line,
                                         char* dst, const size_t cap)
{
    if (text == NULL || line == 0) return 0;

    uint32_t current = 1;
    const char* p = text;
    while (*p != '\0')
    {
        if (current == line)
        {
            const char* end = p;
            while (*end != '\0' && *end != '\n') end++;
            size_t len = (size_t)(end - p);
            while (len > 0 && p[len - 1] == '\r') len--;
            if (len >= cap) len = cap - 1;
            memcpy(dst, p, len);
            dst[len] = '\0';
            return 1;
        }
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n')
        {
            p++;
            current++;
        }
    }
    return 0;
}

// Reads a single source line (1-based) into dst for report snippets.
// fopen/fgets into caller storage only — error reporting must not allocate.
// Tries the file path as-is first, then joined with source_root. Long lines
// are truncated to cap. Returns 0 when the file or line is unavailable.
static int djinn_read_source_line(const char* file, const char* source_root,
                                  const uint32_t line, char* dst, const size_t cap)
{
    if (file == NULL || file[0] == '\0' || line == 0) return 0;

    char path[1024];
    FILE* f = fopen(file, "rb");
    if (f == NULL && source_root != NULL && source_root[0] != '\0')
    {
        snprintf(path, sizeof path, "%s/%s", source_root, file);
        f = fopen(path, "rb");
    }
    if (f == NULL)
    {
        /* not on disk: in-memory sources registered by the host */
        for (int i = 0; i < djinn_registered_source_count; i++)
        {
            if (strcmp(djinn_registered_sources[i].file_id, file) == 0)
                return djinn_source_line_from_memory(djinn_registered_sources[i].text,
                                                     line, dst, cap);
        }
        return 0;
    }

    char scratch[512];
    uint32_t current = 1;
    int found = 0;
    while (fgets(scratch, sizeof scratch, f) != NULL)
    {
        size_t len = strlen(scratch);
        const int ends_line = len > 0 && scratch[len - 1] == '\n';
        if (current == line)
        {
            while (len > 0 && (scratch[len - 1] == '\n' || scratch[len - 1] == '\r'))
                len--;
            if (len >= cap) len = cap - 1;
            memcpy(dst, scratch, len);
            dst[len] = '\0';
            found = 1;
            break;
        }
        /* a chunk without '\n' is a fragment of the same (overlong) line */
        if (ends_line)
            current++;
    }
    fclose(f);
    return found;
}

static int djinn_append_snippet(char* buf, int used, const djinn_error_info_t* info)
{
    if (info->line == 0 || info->file == NULL) return used;

    char line_num[16];
    snprintf(line_num, sizeof line_num, "%u", (unsigned)info->line);
    const size_t gutter = strlen(line_num);

    used = djinn_report_append(buf, used, "  --> %s:%u:%u\n", info->file,
                               (unsigned)info->line, (unsigned)info->column);
    used = djinn_report_append(buf, used, " %*s |\n", (int)gutter, "");

    char line_text[DJINN_SOURCE_LINE_MAX];
    if (djinn_read_source_line(info->file, info->source_root, info->line,
                               line_text, sizeof line_text)
        && line_text[0] != '\0')
    {
        // Trim trailing whitespace from the snippet so the caret stays aligned
        size_t len = strlen(line_text);
        while (len > 0 && (line_text[len - 1] == ' '
            || line_text[len - 1] == '\t'
            || line_text[len - 1] == '\r'))
            len--;
        used = djinn_report_append(buf, used, " %s | %.*s\n", line_num, (int)len, line_text);

        const unsigned caret_len = info->length ? info->length : 1;
        const unsigned offset = info->column > 1 ? info->column - 1 : 0;
        used = djinn_report_append(buf, used, " %*s | %*s", (int)gutter, "", (int)offset, "");
        for (unsigned i = 0; i < caret_len && used < DJINN_ERROR_REPORT_SIZE - 2; i++)
            used = djinn_report_append(buf, used, "^");
        used = djinn_report_append(buf, used, "\n");
    }

    return djinn_report_append(buf, used, " %*s |\n", (int)gutter, "");
}

static const djinn_tracked_var_t* djinn_find_tracked(const void* slot)
{
    if (slot == NULL) return NULL;
    for (int i = djinn_var_registry.count - 1; i >= 0; i--)
        if (djinn_var_registry.vars[i].slot == slot)
            return &djinn_var_registry.vars[i];
    return NULL;
}

// The last assignment sites of a variable operand, e.g.:
//    = note: history of 'a':
//      ...
//    3 |     i32t a = 2t;
//    7 |     a = 2000000000t;
static int djinn_append_var_history(char* buf, int used, const char* name, const void* slot)
{
    const djinn_tracked_var_t* var = djinn_find_tracked(slot);
    if (var == NULL || var->count == 0) return used;

    used = djinn_report_append(buf, used, "   = note: history of '%s':\n", name ? name : var->name);
    if (var->total > var->count)
        used = djinn_report_append(buf, used, "     ...\n");
    for (int i = 0; i < var->count; i++)
    {
        char line_text[DJINN_SOURCE_LINE_MAX];
        const char* text = "";
        if (djinn_read_source_line(var->events[i].file, var->source_root, var->events[i].line,
                                   line_text, sizeof line_text))
            text = line_text;
        used = djinn_report_append(buf, used, " %4u | %s\n",
                                   (unsigned)var->events[i].line, text);
    }
    return used;
}

// Symbolizes (lazily — only when a report is printed) and renders the trace:
// "  at name (file:line)", "  at name" or "  at 0xADDR" per frame. Leading
// frames belonging to the runtime's capture machinery are skipped.
static int djinn_render_backtrace(char* buf, int used, void* const* frames, const int count)
{
    if (count <= 0) return used;

    char names[DJINN_MAX_TRACE_FRAMES][128];
    char files[DJINN_MAX_TRACE_FRAMES][192];
    uint32_t lines[DJINN_MAX_TRACE_FRAMES];
    int have_name[DJINN_MAX_TRACE_FRAMES];

    for (int i = 0; i < count; i++)
    {
        names[i][0] = '\0';
        files[i][0] = '\0';
        lines[i] = 0;
        have_name[i] = 0;

        if (const char* jit = djinn_jit_lookup(frames[i]))
        {
            snprintf(names[i], sizeof names[i], "%s", jit);
            have_name[i] = 1;
            continue;
        }
        if (djinn_symbolize_frame(frames[i], names[i], sizeof names[i],
                                  files[i], sizeof files[i], &lines[i]))
        {
            have_name[i] = 1;
        }
    }
#ifndef _WIN32
    djinn_fill_frame_lines(frames, count, files, lines);
#endif

    // Skip the capture machinery itself at the top of the trace: runtime
    // frames resolve to __djinn_/djinn_ names; JIT'd runtime helpers are the
    // unresolved ones. Unresolvable leading frames can't be user code in a
    // JIT (all user functions are registered) or an AOT debug build (statics
    // resolve from the PDB/DWARF).
    int start = 0;
    while (start < count
        && (!have_name[start]
            || strncmp(names[start], "__djinn_", 8) == 0
            || strncmp(names[start], "djinn_", 6) == 0))
        start++;
    if (start >= count) return used;

    used = djinn_report_append(buf, used, "stack trace:\n");
    for (int i = start; i < count; i++)
    {
        if (!have_name[i])
            used = djinn_report_append(buf, used, "  at 0x%llx\n",
                                       (unsigned long long)(uintptr_t)frames[i]);
        else if (files[i][0] != '\0')
            used = djinn_report_append(buf, used, "  at %s (%s:%u)\n",
                                       names[i], files[i], (unsigned)lines[i]);
        else
            used = djinn_report_append(buf, used, "  at %s\n", names[i]);
        // User execution ends at main; below it are host trampolines (JIT
        // runner thread, process start) that add nothing to the report.
        if (have_name[i] && strcmp(names[i], "main") == 0)
            break;
    }
    return used;
}

// Minimal (release) reports keep location + operands but no trace: the
// generator emits no throw-site captures in release builds, and traps arriving
// through __djinn_runtime_error_min raise this flag so the inline capture is
// skipped too.
static int djinn_report_minimal = 0;

void __djinn_runtime_error(const djinn_error_info_t* info)
{
    djinn_error_info_t fallback;
    if (info == NULL)
    {
        memset(&fallback, 0, sizeof fallback);
        fallback.message = "unknown";
        info = &fallback;
    }

    char* report = __djinn_last_error_report;
    int used = 0;
    used = djinn_report_append(report, used, "djinn runtime error: %s\n",
                               info->message ? info->message : "unknown");
    used = djinn_append_snippet(report, used, info);
    used = djinn_append_note(report, used, info);
    if (info->left_var_slot != NULL)
        used = djinn_append_var_history(report, used, info->left_var_name, info->left_var_slot);
    if (info->right_var_slot != NULL && info->right_var_slot != info->left_var_slot)
        used = djinn_append_var_history(report, used, info->right_var_name, info->right_var_slot);
    if (!djinn_report_minimal)
    {
        void* frames[DJINN_MAX_TRACE_FRAMES];
        const int count = djinn_capture_frames(frames, DJINN_MAX_TRACE_FRAMES);
        used = djinn_render_backtrace(report, used, frames, count);
    }

    fputs(report, stderr);
    fflush(stderr);
    abort();
}

// Legacy simple trap: keeps pre-descriptor callers failing loudly.
void __djinn_runtime_error_message(const char* message)
{
    djinn_error_info_t info;
    memset(&info, 0, sizeof info);
    info.message = message ? message : "unknown";
    __djinn_runtime_error(&info);
}

// Release-build trap: same rendering pipeline as __djinn_runtime_error, but
// built from scalar arguments (no descriptor, no snippet, no var history).
void __djinn_runtime_error_min(const char* message, const char* file, const uint32_t line,
                               const uint32_t column, const uint8_t op, const uint8_t bits,
                               const uint8_t is_signed, const uint8_t has_operands,
                               const uint64_t left, const uint64_t right)
{
    djinn_report_minimal = 1;
    djinn_error_info_t info;
    memset(&info, 0, sizeof info);
    info.message = message ? message : "unknown";
    info.file = file;
    info.line = line;
    info.column = column;
    info.op = op;
    info.bits = bits;
    info.is_signed = is_signed;
    info.has_operands = has_operands;
    info.left = left;
    info.right = right;
    __djinn_runtime_error(&info);
}

// ── Interpolated error message formatting ──

static DJINN_TLS char djinn_error_message[DJINN_ERROR_MESSAGE_MAX];

// Mirrors std::types TypeInfo { i32 id, i32 size, i8* name, u8 kind }
typedef struct
{
    int32_t id;
    int32_t size;
    const char* name;
    uint8_t kind;
} djinn_box_type_info_t;

// Mirrors std::types object { TypeInfo* type, void* data }
typedef struct
{
    const djinn_box_type_info_t* type;
    const void* data;
} djinn_boxed_object_t;

const char* __djinn_error_format(const char* fmt_data, const uint32_t fmt_len,
                                 const void* boxed_objects, const int32_t count)
{
    char* buf = djinn_error_message;
    size_t bpos = 0;
    uint32_t pos = 0;

    while (pos < fmt_len)
    {
        const char current = fmt_data[pos];
        if (current != '{')
        {
            if (bpos < DJINN_ERROR_MESSAGE_MAX - 1)
                buf[bpos++] = current;
            pos++;
            continue;
        }

        pos++;
        int32_t idx = 0;
        while (pos < fmt_len && fmt_data[pos] != '}')
        {
            idx = idx * 10 + (fmt_data[pos] - '0');
            pos++;
        }
        pos++; /* skip '}' */

        if (idx < 0 || idx >= count || boxed_objects == NULL) continue;
        const djinn_boxed_object_t* arg = &((const djinn_boxed_object_t*)boxed_objects)[idx];
        if (arg->type == NULL || arg->data == NULL) continue;

        const size_t room = DJINN_ERROR_MESSAGE_MAX - 1 - bpos;
        int written = 0;
        if (arg->type->kind == 0) /* int */
        {
            written = arg->type->size <= 4
                          ? snprintf(buf + bpos, room + 1, "%d", *(const int32_t*)arg->data)
                          : snprintf(buf + bpos, room + 1, "%lld",
                                     (long long)*(const int64_t*)arg->data);
        }
        else if (arg->type->kind == 1) /* float */
        {
            written = arg->type->size <= 4
                          ? snprintf(buf + bpos, room + 1, "%g", (double)*(const float*)arg->data)
                          : snprintf(buf + bpos, room + 1, "%g", *(const double*)arg->data);
        }
        else if (arg->type->kind == 2) /* i8* string */
        {
            size_t slen = strlen((const char*)arg->data);
            if (slen > room) slen = room;
            memcpy(buf + bpos, arg->data, slen);
            bpos += slen;
        }
        if (written > 0)
            bpos += ((size_t)written > room) ? room : (size_t)written;
    }

    buf[bpos] = '\0';
    return buf;
}

// Mirrors binder/ErrorTypes.h: tags 1..99 are reserved for builtin errors.
static const char* djinn_builtin_error_name(const int tag)
{
    switch (tag)
    {
    case 1: return "Exception";
    case 2: return "Generic";
    case 3: return "DivisionByZero";
    case 4: return "Argument";
    case 5: return "Overflow";
    case 6: return "OutOfBounds";
    case 7: return "InvalidArgument";
    case 8: return "ContractViolation";
    case 9: return "ForeignError";
    default: return NULL;
    }
}

void __djinn_uncaught_error(const int tag, const char* type_name, const char* message,
                            const char* origin_file, const uint32_t origin_line,
                            const uint32_t origin_column)
{
    char* report = __djinn_last_error_report;
    int used = 0;
    used = djinn_report_append(report, used, "djinn runtime error: uncaught exception escaped 'main'\n");

    const char* name = (type_name != NULL && type_name[0] != '\0') ? type_name : djinn_builtin_error_name(tag);
    const char* text = (message != NULL && message[0] != '\0') ? message : "<no message>";
    if (name != NULL)
        used = djinn_report_append(report, used, "  error: %s: %s\n", name, text);
    else
        used = djinn_report_append(report, used, "  error: %s (tag %d)\n", text, tag);

    if (origin_file != NULL && origin_file[0] != '\0')
        used = djinn_report_append(report, used, "  --> %s:%u:%u\n", origin_file,
                                   (unsigned)origin_line, (unsigned)origin_column);
    // The error unwound normally, so render the trace captured at the throw
    // site instead of the (already empty) live stack
    used = djinn_render_backtrace(report, used, djinn_error_trace.frames, djinn_error_trace.count);

    fputs(report, stderr);
    fflush(stderr);
    abort();
}

void __djinn_runtime_init(int num_threads)
{
    memset(&runtime, 0, sizeof(runtime));
    logger = logger_create("RUNTIME", 1, LOG_TRACE);
    logger_configure_from_properties(logger, "runtime.properties");

#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsadata;
    const int wsa_err = WSAStartup(MAKEWORD(2, 2), &wsadata);
    DJINN_ASSERT(wsa_err == 0, "WSAStartup failed");
#endif

    init_queue(&runtime.ready_queue);
    init_queue(&runtime.pool.queue);

    // File I/O mutex
#ifdef _WIN32
    InitializeCriticalSection(&runtime.file_io_mutex);
#else
    pthread_mutex_init(&runtime.file_io_mutex, NULL);
#endif

    // Socket mutex
#ifdef _WIN32
    InitializeCriticalSection(&runtime.socket_mutex);
#else
    pthread_mutex_init(&runtime.socket_mutex, NULL);
#endif

    runtime.running = 1;
    runtime.pool.shutdown = 0;

#ifdef _WIN32
    runtime.iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    DJINN_ASSERT(runtime.iocp != NULL, "CreateIoCompletionPort failed");

    {
        const SOCKET tmp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tmp != INVALID_SOCKET)
        {
            DWORD bytes;
            GUID guid_acceptex = WSAID_ACCEPTEX;
            GUID guid_connectex = WSAID_CONNECTEX;
            WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid_acceptex, sizeof(guid_acceptex),
                     &pfnAcceptEx, sizeof(pfnAcceptEx), &bytes, NULL, NULL);
            WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid_connectex, sizeof(guid_connectex),
                     &pfnConnectEx, sizeof(pfnConnectEx), &bytes, NULL, NULL);
            closesocket(tmp);
        }
        DJINN_ASSERT(pfnAcceptEx != NULL, "Failed to load AcceptEx");
        DJINN_ASSERT(pfnConnectEx != NULL, "Failed to load ConnectEx");
    }
#else
    runtime.epoll_fd = epoll_create1(0);
    DJINN_ASSERT(runtime.epoll_fd >= 0, "epoll_create1 failed");
#endif

    if (num_threads > 0)
    {
        runtime.pool.thread_count = num_threads;
#ifdef _WIN32
        runtime.pool.threads = (HANDLE*)malloc(sizeof(HANDLE) * num_threads);
        for (int i = 0; i < num_threads; i++)
        {
            runtime.pool.threads[i] = CreateThread(NULL, 0, worker_thread, &runtime.pool, 0, NULL);
        }
#else
        runtime.pool.threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
        for (int i = 0; i < num_threads; i++)
        {
            pthread_create(&runtime.pool.threads[i], NULL, worker_thread, &runtime.pool);
        }
#endif
    }

#ifdef _WIN32
    runtime.file_io_thread = CreateThread(NULL, 0, file_io_thread_func, NULL, 0, NULL);
#else
    pthread_create(&runtime.file_io_thread, NULL, file_io_thread_func, NULL);
#endif

    // ── Create socket poller thread ──
#ifdef _WIN32
    runtime.socket_poller_thread = CreateThread(NULL, 0, socket_poller_thread_func, NULL, 0, NULL);
#else
    pthread_create(&runtime.socket_poller_thread, NULL, socket_poller_thread_func, NULL);
#endif

    DJINN_TRACE("runtime initialized (threads=%d, iocp/epoll=active)", num_threads);
}

void __djinn_runtime_shutdown(void)
{
    DJINN_TRACE("runtime shutting down");
    runtime.running = 0;
    runtime.pool.shutdown = 1;

    for (int i = 0; i < runtime.pool.thread_count; i++)
    {
#ifdef _WIN32
        WakeAllConditionVariable(&runtime.pool.queue.cond);
#else
        pthread_cond_broadcast(&runtime.pool.queue.cond);
#endif
    }

    for (int i = 0; i < runtime.pool.thread_count; i++)
    {
#ifdef _WIN32
        WaitForSingleObject(runtime.pool.threads[i], 5000);
        CloseHandle(runtime.pool.threads[i]);
#else
        pthread_join(runtime.pool.threads[i], NULL);
#endif
    }
    free(runtime.pool.threads);
    runtime.pool.threads = NULL;

#ifdef _WIN32
    WaitForSingleObject(runtime.file_io_thread, 5000);
    CloseHandle(runtime.file_io_thread);
#else
    pthread_join(runtime.file_io_thread, NULL);
#endif

#ifdef _WIN32
    PostQueuedCompletionStatus(runtime.iocp, 0, 0, NULL);
    WaitForSingleObject(runtime.socket_poller_thread, 5000);
    CloseHandle(runtime.socket_poller_thread);
    CloseHandle(runtime.iocp);
#else
    pthread_join(runtime.socket_poller_thread, NULL);
    close(runtime.epoll_fd);
#endif

    destroy_queue(&runtime.ready_queue);
    destroy_queue(&runtime.pool.queue);

#ifdef _WIN32
    EnterCriticalSection(&runtime.file_io_mutex);
#else
    pthread_mutex_lock(&runtime.file_io_mutex);
#endif
    djinn_io_request_t* req = runtime.io_pending;
    while (req)
    {
        djinn_io_request_t* next = req->next;
        free(req);
        req = next;
    }
    runtime.io_pending = NULL;
#ifdef _WIN32
    LeaveCriticalSection(&runtime.file_io_mutex);
    DeleteCriticalSection(&runtime.file_io_mutex);
    DeleteCriticalSection(&runtime.socket_mutex);
#else
    pthread_mutex_unlock(&runtime.file_io_mutex);
    pthread_mutex_destroy(&runtime.file_io_mutex);
    pthread_mutex_destroy(&runtime.socket_mutex);
#endif

#ifdef _WIN32
    WSACleanup();
#endif

    DJINN_TRACE("runtime shut down");
}

void __djinn_spawn(void* coro_handle)
{
    DJINN_ASSERT(coro_handle, "coro handle is NULL");
    enqueue_task(&runtime.ready_queue, coro_handle);
}

// A completed coroutine with no continuation (fire-and-forget spawn) reports
// its promise error slot into the thread-local error state — first error
// wins. Awaited coroutines transfer through the awaiter instead; main's own
// error stays in the error state its throw already wrote.
static void djinn_collect_spawned_error(void* handle)
{
    const char* promise = (const char*)__djinn_coro_promise(handle, DJINN_PROMISE_ALIGN);
    const int32_t tag = *(const volatile int32_t*)(promise + DJINN_PROMISE_ERR_TAG_OFFSET);
    if (tag == 0 || __djinn_errno.flag) return;
    __djinn_errno.flag = 1;
    __djinn_errno.tag = tag;
    __djinn_errno.message = *(const char* const*)(promise + DJINN_PROMISE_ERR_MESSAGE_OFFSET);
    __djinn_errno.type_name = *(const char* const*)(promise + DJINN_PROMISE_ERR_TYPE_OFFSET);
    __djinn_errno.origin_file = NULL;
    __djinn_errno.origin_line = 0;
    __djinn_errno.origin_column = 0;
}

static void process_completed_coro(void* handle, void* main_handle)
{
    remove_from_waiting(handle);
    void* parent = pop_continuation(handle);
    if (parent)
    {
        remove_from_waiting(parent);
        enqueue_task(&runtime.ready_queue, parent);
    }
    else if (handle != main_handle)
    {
        djinn_collect_spawned_error(handle);
        __djinn_coro_destroy(handle);
    }
}

static void drain_ready_queue(void* main_handle)
{
    djinn_task_t* remaining;
    while ((remaining = dequeue_task(&runtime.ready_queue)) != NULL)
    {
        if (!__djinn_coro_done(remaining->handle))
        {
            __djinn_coro_resume(remaining->handle);
            if (!__djinn_coro_done(remaining->handle))
            {
                if (!is_waiting(remaining->handle))
                {
                    enqueue_task(&runtime.ready_queue, remaining->handle);
                }
                free(remaining);
                continue;
            }
        }
        if (__djinn_coro_done(remaining->handle))
        {
            process_completed_coro(remaining->handle, main_handle);
        }
        free(remaining);
    }
}

static void run_event_loop_core(void* main_handle)
{
    DJINN_ASSERT(main_handle, "main handle is NULL");

    if (!is_in_waiting_set(main_handle))
    {
        enqueue_task(&runtime.ready_queue, main_handle);
    }

    while (runtime.running)
    {
        djinn_task_t* task = dequeue_task(&runtime.ready_queue);
        if (task)
        {
            if (!__djinn_coro_done(task->handle))
            {
                __djinn_coro_resume(task->handle);
                if (!__djinn_coro_done(task->handle))
                {
                    if (is_waiting(task->handle))
                    {
                        // Waiting for I/O or child — do NOT re-enqueue
                    }
                    else
                    {
                        // Cooperative yield — re-enqueue immediately
                        enqueue_task(&runtime.ready_queue, task->handle);
                    }
                }
                else
                {
                    process_completed_coro(task->handle, main_handle);
                }
            }
            else
            {
                process_completed_coro(task->handle, main_handle);
            }
            free(task);
        }

        if (__djinn_coro_done(main_handle))
        {
            drain_ready_queue(main_handle);
            break;
        }

        if (!runtime.ready_queue.head)
        {
#ifdef _WIN32
            Sleep(0);
#else
            sched_yield();
#endif
        }
    }
}

int __djinn_event_loop(void* main_handle)
{
    run_event_loop_core(main_handle);

    const char* promise = (const char*)__djinn_coro_promise(main_handle, DJINN_PROMISE_ALIGN);
    const int result = *(const int*)(promise + DJINN_PROMISE_VALUE_OFFSET);
    __djinn_coro_destroy(main_handle);
    return result;
}

void __djinn_event_loop_run(void* main_handle)
{
    run_event_loop_core(main_handle);
}


int64_t __djinn_async_read(int fd, void* buf, int64_t count, void* coro)
{
    DJINN_ASSERT(coro, "coro handle is NULL");
    DJINN_ASSERT(buf, "buf is NULL");
    DJINN_ASSERT(count > 0, "count <= 0");

    djinn_io_request_t* req = __djinn_malloc(sizeof(djinn_io_request_t));
    if (!req) return -1;

    req->type = DJINN_IO_FILE_READ;
    req->fd = fd;
    req->buffer = buf;
    req->count = count;
    req->waiting_coro = coro;

#ifdef _WIN32
    EnterCriticalSection(&runtime.file_io_mutex);
#else
    pthread_mutex_lock(&runtime.file_io_mutex);
#endif

    req->next = runtime.io_pending;
    runtime.io_pending = req;

#ifdef _WIN32
    LeaveCriticalSection(&runtime.file_io_mutex);
#else
    pthread_mutex_unlock(&runtime.file_io_mutex);
#endif

    return 0;
}

int64_t __djinn_async_write(int fd, void* buf, int64_t count, void* coro)
{
    DJINN_ASSERT(coro, "coro handle is NULL");
    DJINN_ASSERT(buf, "buf is NULL");
    DJINN_ASSERT(count > 0, "count <= 0");

    djinn_io_request_t* req = __djinn_malloc(sizeof(djinn_io_request_t));
    if (!req) return -1;

    req->type = DJINN_IO_FILE_WRITE;
    req->fd = fd;
    req->buffer = buf;
    req->count = count;
    req->waiting_coro = coro;

#ifdef _WIN32
    EnterCriticalSection(&runtime.file_io_mutex);
#else
    pthread_mutex_lock(&runtime.file_io_mutex);
#endif

    req->next = runtime.io_pending;
    runtime.io_pending = req;

#ifdef _WIN32
    LeaveCriticalSection(&runtime.file_io_mutex);
#else
    pthread_mutex_unlock(&runtime.file_io_mutex);
#endif

    return 0;
}

int64_t __djinn_socket_create(void)
{
#ifdef _WIN32
    const SOCKET socket_fd = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket_fd == INVALID_SOCKET)
    {
        DJINN_TRACE("WSASocket failed: %d", WSAGetLastError());
        return -1;
    }
    // Associate with IOCP
    const HANDLE handle = CreateIoCompletionPort((HANDLE)socket_fd, runtime.iocp, 0, 0);
    if (!handle)
    {
        DJINN_TRACE("CreateIoCompletionPort for socket failed: %lu", GetLastError());
        closesocket(socket_fd);
        return -1;
    }
    return (int64_t)socket_fd;
#else
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0)
    {
        DJINN_TRACE("socket() failed: %d", errno);
        return -1;
    }
    return (int64_t)fd;
#endif
}

int64_t __djinn_socket_close(int64_t socket_fd)
{
    DJINN_TRACE("closing socket: %d", socket_fd);
#ifdef _WIN32
    return closesocket((SOCKET)socket_fd) == 0 ? 0 : -1;
#else
    return close((int)socket_fd) == 0 ? 0 : -1;
#endif
}

int64_t __djinn_socket_bind(int64_t socket_fd, const char* address, int port)
{
    struct sockaddr_in socket_address;
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sin_family = AF_INET;
    socket_address.sin_port = htons((uint16_t)port);

    if (address && address[0] != '\0')
    {
        inet_pton(AF_INET, address, &socket_address.sin_addr);
    }
    else
    {
        socket_address.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    // Enable SO_REUSEADDR
    const int optval = 1;
#ifdef _WIN32
    setsockopt((SOCKET)socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));
#else
    setsockopt((int)socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

#ifdef _WIN32
    if (bind((SOCKET)socket_fd, (struct sockaddr*)&socket_address, sizeof(socket_address)) == SOCKET_ERROR)
    {
        DJINN_TRACE("bind failed: %d", WSAGetLastError());
        return -1;
    }
#else
    if (bind((int)socket_fd, (struct sockaddr*)&socket_address, sizeof(socket_address)) < 0)
    {
        DJINN_TRACE("bind failed: %d", errno);
        return -1;
    }
#endif
    return 0;
}

int64_t __djinn_socket_listen(int64_t socket_fd, int backlog)
{
#ifdef _WIN32
    if (listen((SOCKET)socket_fd, backlog) == SOCKET_ERROR)
    {
        DJINN_TRACE("listen failed: %d", WSAGetLastError());
        return -1;
    }
#else
    if (listen((int)socket_fd, backlog) < 0)
    {
        DJINN_TRACE("listen failed: %d", errno);
        return -1;
    }
#endif
    return 0;
}

int64_t __djinn_async_accept(int64_t server_sock, int64_t* out_result, void* coro)
{
    DJINN_ASSERT(coro, "coro handle is NULL");

    djinn_io_request_t* req = __djinn_malloc(sizeof(djinn_io_request_t));
    if (!req) return -1;

    req->type = DJINN_IO_ACCEPT;
    req->socket = server_sock;
    req->out_result = out_result;
    req->waiting_coro = coro;

#ifdef _WIN32
    // Create the accepted socket
    req->accepted_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (req->accepted_socket == INVALID_SOCKET)
    {
        DJINN_TRACE("WSASocket for accept failed: %d", WSAGetLastError());
        free(req);
        return -1;
    }

    memset(&req->overlapped, 0, sizeof(req->overlapped));
    DWORD bytes_received;
    const BOOL ok = pfnAcceptEx(
        (SOCKET)server_sock,
        req->accepted_socket,
        req->accept_buf,
        0, // no data read with accept
        sizeof(struct sockaddr_in) + 16,
        sizeof(struct sockaddr_in) + 16,
        &bytes_received,
        &req->overlapped
    );

    if (!ok && WSAGetLastError() != ERROR_IO_PENDING)
    {
        DJINN_TRACE("AcceptEx failed: %d", WSAGetLastError());
        closesocket(req->accepted_socket);
        free(req);
        return -1;
    }
#else
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.ptr = req;
    if (epoll_ctl(runtime.epoll_fd, EPOLL_CTL_ADD, (int)server_sock, &ev) < 0)
    {
        if (epoll_ctl(runtime.epoll_fd, EPOLL_CTL_MOD, (int)server_sock, &ev) < 0)
        {
            DJINN_TRACE("epoll_ctl for accept failed: %d", errno);
            free(req);
            return -1;
        }
    }
#endif

    DJINN_TRACE("async_accept submitted for server socket: %lld", (long long)server_sock);
    return 0;
}

int64_t __djinn_async_connect(int64_t socket_fd, const char* address, int port, int64_t* out_result, void* coro)
{
    DJINN_ASSERT(coro, "coro handle is NULL");
    DJINN_ASSERT(address, "addr is NULL");

    djinn_io_request_t* req = __djinn_malloc(sizeof(djinn_io_request_t));
    if (!req) return -1;

    const size_t addr_len = strlen(address);

    req->type = DJINN_IO_CONNECT;
    req->socket = socket_fd;
    req->out_result = out_result;
    req->waiting_coro = coro;
    req->port = port;
    memcpy(req->addr, address, addr_len < sizeof(req->addr) ? addr_len : sizeof(req->addr) - 1);
    req->addr[sizeof(req->addr) - 1] = '\0';

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, address, &sa.sin_addr);

#ifdef _WIN32
    // ConnectEx requires the socket to be bound first
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = 0;
    bind((SOCKET)socket_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr));

    memset(&req->overlapped, 0, sizeof(req->overlapped));
    const BOOL ok = pfnConnectEx(
        (SOCKET)socket_fd,
        (struct sockaddr*)&sa,
        sizeof(sa),
        NULL, 0, NULL,
        &req->overlapped
    );

    if (!ok && WSAGetLastError() != ERROR_IO_PENDING)
    {
        DJINN_TRACE("ConnectEx failed: %d", WSAGetLastError());
        free(req);
        return -1;
    }
#else
    int ret = connect((int)socket_fd, (struct sockaddr*)&sa, sizeof(sa));
    if (ret < 0 && errno != EINPROGRESS)
    {
        DJINN_TRACE("connect failed: %d", errno);
        free(req);
        return -1;
    }

    if (ret == 0)
    {
        req->result = 0;
        req->completed = 1;
        enqueue_task(&runtime.ready_queue, coro);
        free(req);
        return 0;
    }

    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLONESHOT;
    ev.data.ptr = req;
    if (epoll_ctl(runtime.epoll_fd, EPOLL_CTL_ADD, (int)socket_fd, &ev) < 0)
    {
        DJINN_TRACE("epoll_ctl for connect failed: %d", errno);
        free(req);
        return -1;
    }
#endif

    return 0;
}

int64_t __djinn_async_send(int64_t socket_fd, void* buffer, int64_t count, int64_t* out_result, void* coro)
{
    DJINN_ASSERT(coro, "coro handle is NULL");
    DJINN_ASSERT(buffer, "buf is NULL");
    DJINN_ASSERT(count > 0, "count <= 0");

    djinn_io_request_t* req = __djinn_malloc(sizeof(djinn_io_request_t));
    if (!req) return -1;

    req->type = DJINN_IO_SEND;
    req->socket = socket_fd;
    req->buffer = buffer;
    req->count = count;
    req->out_result = out_result;
    req->waiting_coro = coro;

#ifdef _WIN32
    memset(&req->overlapped, 0, sizeof(req->overlapped));
    req->wsabuf.buf = (char*)buffer;
    req->wsabuf.len = (ULONG)count;

    DWORD bytes_sent;
    const int ret = WSASend((SOCKET)socket_fd, &req->wsabuf, 1, &bytes_sent, 0, &req->overlapped, NULL);
    if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        DJINN_TRACE("WSASend failed: %d", WSAGetLastError());
        free(req);
        return -1;
    }
#else
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLONESHOT;
    ev.data.ptr = req;
    if (epoll_ctl(runtime.epoll_fd, EPOLL_CTL_ADD, (int)socket_fd, &ev) < 0)
    {
        if (epoll_ctl(runtime.epoll_fd, EPOLL_CTL_MOD, (int)socket_fd, &ev) < 0)
        {
            DJINN_TRACE("epoll_ctl for send failed: %d", errno);
            free(req);
            return -1;
        }
    }
#endif

    return 0;
}

int64_t __djinn_async_recv(int64_t socket_fd, void* buffer, int64_t count, int64_t* out_result, void* coro)
{
    DJINN_ASSERT(coro, "coro handle is NULL");
    DJINN_ASSERT(buffer, "buf is NULL");
    DJINN_ASSERT(count > 0, "count <= 0");

    DJINN_TRACE("reading buffer size %d", (int)count);

    djinn_io_request_t* req = __djinn_malloc(sizeof(djinn_io_request_t));
    if (!req) return -1;

    req->type = DJINN_IO_RECV;
    req->socket = socket_fd;
    req->buffer = buffer;
    req->count = count;
    req->out_result = out_result;
    req->waiting_coro = coro;

#ifdef _WIN32
    memset(&req->overlapped, 0, sizeof(req->overlapped));
    req->wsabuf.buf = (char*)buffer;
    req->wsabuf.len = (ULONG)count;

    DWORD bytes_received;
    DWORD flags = 0;
    const int ret = WSARecv((SOCKET)socket_fd, &req->wsabuf, 1, &bytes_received, &flags, &req->overlapped, NULL);
    if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        DJINN_TRACE("WSARecv failed: %d", WSAGetLastError());
        __djinn_free(req);
        return -1;
    }
#else
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.ptr = req;
    if (epoll_ctl(runtime.epoll_fd, EPOLL_CTL_ADD, (int)socket_fd, &ev) < 0)
    {
        if (epoll_ctl(runtime.epoll_fd, EPOLL_CTL_MOD, (int)socket_fd, &ev) < 0)
        {
            DJINN_TRACE("epoll_ctl for recv failed: %d", errno);
            free(req);
            return -1;
        }
    }
#endif

    DJINN_TRACE("recv done");
    return 0;
}

#ifdef _WIN32
static DWORD WINAPI djinn_thread_entry(LPVOID arg)
{
    djinn_thread_t* t = (djinn_thread_t*)arg;
    t->alive = 1;
    t->func(t->arg);
    t->alive = 0;
    return 0;
}
#else
static void* djinn_thread_entry(void* arg)
{
    djinn_thread_t* t = (djinn_thread_t*)arg;
    t->alive = 1;
    t->func(t->arg);
    t->alive = 0;
    return NULL;
}
#endif

djinn_thread_t* __djinn_thread_create(void (*func)(void*), void* arg)
{
    DJINN_ASSERT(func, "thread func is NULL");

    djinn_thread_t* t = __djinn_malloc(sizeof(djinn_thread_t));
    if (!t) return NULL;

    t->func = func;
    t->arg = arg;
    t->alive = 0;

    return t;
}

int __djinn_thread_start(djinn_thread_t* thread)
{
    DJINN_ASSERT(thread, "thread is NULL");

#ifdef _WIN32
    thread->handle = CreateThread(NULL, 0, djinn_thread_entry, thread, 0, &thread->id);
    if (!thread->handle)
    {
        DJINN_TRACE("CreateThread failed: %lu", GetLastError());
        return -1;
    }
#else
    int ret = pthread_create(&thread->handle, NULL, djinn_thread_entry, thread);
    if (ret != 0)
    {
        DJINN_TRACE("pthread_create failed: %d", ret);
        return -1;
    }
#endif
    return 0;
}

void __djinn_thread_join(djinn_thread_t* thread)
{
    DJINN_ASSERT(thread, "thread is NULL");

#ifdef _WIN32
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    thread->handle = NULL;
#else
    pthread_join(thread->handle, NULL);
#endif
}

int __djinn_thread_is_alive(djinn_thread_t* thread)
{
    if (!thread) return 0;
    return thread->alive;
}

void __djinn_thread_sleep(int64_t milliseconds)
{
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#endif
}

// ── Mutex ──

djinn_mutex_t* __djinn_mutex_create(void)
{
    djinn_mutex_t* m = __djinn_malloc(sizeof(djinn_mutex_t));
    if (!m) return NULL;

#ifdef _WIN32
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutex_init(&m->mtx, NULL);
#endif

    return m;
}

void __djinn_mutex_lock(djinn_mutex_t* mutex)
{
    DJINN_ASSERT(mutex, "mutex is NULL");
#ifdef _WIN32
    EnterCriticalSection(&mutex->cs);
#else
    pthread_mutex_lock(&mutex->mtx);
#endif
}

void __djinn_mutex_unlock(djinn_mutex_t* mutex)
{
    DJINN_ASSERT(mutex, "mutex is NULL");
#ifdef _WIN32
    LeaveCriticalSection(&mutex->cs);
#else
    pthread_mutex_unlock(&mutex->mtx);
#endif
}

int __djinn_mutex_trylock(djinn_mutex_t* mutex)
{
    DJINN_ASSERT(mutex, "mutex is NULL");
#ifdef _WIN32
    return TryEnterCriticalSection(&mutex->cs) ? 1 : 0;
#else
    return pthread_mutex_trylock(&mutex->mtx) == 0 ? 1 : 0;
#endif
}

void __djinn_mutex_destroy(djinn_mutex_t* mutex)
{
    if (!mutex) return;
#ifdef _WIN32
    DeleteCriticalSection(&mutex->cs);
#else
    pthread_mutex_destroy(&mutex->mtx);
#endif
    free(mutex);
}

int64_t __djinn_console_write(const char* str, void* coro)
{
    if (!str) return 0;
    const int64_t len = (int64_t)strlen(str);
    if (len <= 0) return 0;
    return __djinn_async_write(1, (void*)str, len, coro);
}

int64_t __djinn_console_error(const char* str, void* coro)
{
    if (!str) return 0;
    const int64_t len = (int64_t)strlen(str);
    if (len <= 0) return 0;
    return __djinn_async_write(2, (void*)str, len, coro);
}

int64_t __djinn_console_read_line(char* buf, int64_t max, void* coro)
{
    if (!buf || max <= 0) return 0;
    return __djinn_async_read(0, buf, max, coro);
}

int32_t __djinn_terminal_width(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return 80;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return w.ws_col;
    return 80;
#endif
}

int32_t __djinn_terminal_height(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 24;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0)
        return w.ws_row;
    return 24;
#endif
}

#ifdef _WIN32
static DWORD __djinn_original_console_mode = 0;
static int __djinn_raw_saved = 0;
#else
static struct termios __djinn_original_termios;
static int __djinn_raw_saved = 0;
#endif

void __djinn_raw_enable(void)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(h, &__djinn_original_console_mode);
    __djinn_raw_saved = 1;
    DWORD mode = __djinn_original_console_mode;
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(h, mode);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outMode;
    GetConsoleMode(out, &outMode);
    outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(out, outMode);
#else
    tcgetattr(STDIN_FILENO, &__djinn_original_termios);
    __djinn_raw_saved = 1;
    struct termios raw = __djinn_original_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif
}

void __djinn_raw_disable(void)
{
    if (!__djinn_raw_saved) return;
#ifdef _WIN32
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), __djinn_original_console_mode);
#else
    tcflush(STDIN_FILENO, TCIFLUSH);
    tcsetattr(STDIN_FILENO, TCSANOW, &__djinn_original_termios);
#endif
    __djinn_raw_saved = 0;
}

int32_t __djinn_read_line(char* buf, int32_t maxLen)
{
    if (!buf || maxLen <= 0) return 0;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    DWORD bytesRead = 0;
    ReadConsoleA(h, buf, (DWORD)(maxLen - 1), &bytesRead, NULL);
    SetConsoleMode(h, mode);
    buf[bytesRead] = '\0';
    int32_t len = (int32_t)bytesRead;
#else
    if (!fgets(buf, maxLen, stdin)) return 0;
    int32_t len = (int32_t)strlen(buf);
#endif
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
    {
        buf[len - 1] = '\0';
        len--;
    }
    return len;
}

int32_t __djinn_read_key(void)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD rec;
    DWORD count;
    while (1)
    {
        if (!ReadConsoleInputA(h, &rec, 1, &count) || count == 0) continue;
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;
        WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
        char ch = rec.Event.KeyEvent.uChar.AsciiChar;
        DJINN_TRACE("read_key: eventType=%d vk=0x%04X ch=%d('%c') scan=0x%04X",
                    rec.EventType, vk, (int)ch, (ch >= 32 && ch < 127) ? ch : '?',
                    rec.Event.KeyEvent.wVirtualScanCode);
        if (vk == 0x26)
        {
            DJINN_TRACE("read_key: -> UP (-1)");
            return -1;
        }
        if (vk == 0x28)
        {
            DJINN_TRACE("read_key: -> DOWN (-2)");
            return -2;
        }
        if (vk == 0x25)
        {
            DJINN_TRACE("read_key: -> LEFT");
            continue;
        }
        if (vk == 0x27)
        {
            DJINN_TRACE("read_key: -> RIGHT");
            continue;
        }
        if (vk == 0x0D || ch == 13 || ch == 10)
        {
            DJINN_TRACE("read_key: -> ENTER (-3)");
            return -3;
        }
        if (vk == 0x1B || ch == 27)
        {
            DJINN_TRACE("read_key: -> ESCAPE (-5)");
            return -5;
        }
        if (vk == 0x20 || ch == 32)
        {
            DJINN_TRACE("read_key: -> SPACE (-4)");
            return -4;
        }
        if (ch == 3)
        {
            DJINN_TRACE("read_key: -> CTRL+C");
            __djinn_raw_disable();
            exit(1);
        }
        if (ch != 0)
        {
            DJINN_TRACE("read_key: -> char %d", (int)ch);
            return (int32_t)ch;
        }
        DJINN_TRACE("read_key: -> ignored (ch=0, vk=0x%04X)", vk);
    }
#else
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -5;
    if (c == 27)
    {
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return -5;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return -5;
        if (seq[0] == '[')
        {
            if (seq[1] == 'A') return -1;
            if (seq[1] == 'B') return -2;
        }
        return -5;
    }
    if (c == 3) { __djinn_raw_disable(); exit(1); }
    if (c == '\r' || c == '\n') return -3;
    if (c == ' ') return -4;
    return (int32_t)c;
#endif
}
