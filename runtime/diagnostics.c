#include "runtime_internal.h"

// ---------------------------------------------------------------------------
// Runtime error reporting: rich traps with source location, operand values
// and a shadow call-stack trace. Mirrors the compile-time diagnostic style
// ( --> file:line:col, snippet, caret underline, = note) so both look alike.
// ---------------------------------------------------------------------------

char __djinn_last_error_report[DJINN_ERROR_REPORT_SIZE];

static struct
{
    djinn_frame_t frames[DJINN_MAX_FRAMES];
    int depth;
    int overflowed; /* frames were dropped past the cap */
} djinn_shadow_stack;

void __djinn_frame_push(const char* function_name, const char* file, const uint32_t line)
{
    if (djinn_shadow_stack.depth >= DJINN_MAX_FRAMES)
    {
        djinn_shadow_stack.overflowed = 1;
        return;
    }
    djinn_frame_t* frame = &djinn_shadow_stack.frames[djinn_shadow_stack.depth++];
    frame->function_name = function_name;
    frame->file = file;
    frame->line = line;
}

void __djinn_frame_pop(void)
{
    if (djinn_shadow_stack.depth > 0)
        djinn_shadow_stack.depth--;
}

void __djinn_frame_set_line(const uint32_t line)
{
    if (djinn_shadow_stack.depth > 0)
        djinn_shadow_stack.frames[djinn_shadow_stack.depth - 1].line = line;
}

// ── Uncaught-exception stack snapshot ──
//
// Thrown errors unwind normally (every propagation path returns and pops its
// frame), so the shadow stack is empty by the time the uncaught report
// renders. Throw sites snapshot the live stack here so the report keeps the
// trace of where the error came from.

static struct
{
    djinn_frame_t frames[DJINN_MAX_FRAMES];
    int depth;
} djinn_error_stack;

void __djinn_capture_error_stack(const uint32_t line)
{
    if (djinn_shadow_stack.depth > 0)
        djinn_shadow_stack.frames[djinn_shadow_stack.depth - 1].line = line;
    djinn_error_stack.depth = djinn_shadow_stack.depth;
    memcpy(djinn_error_stack.frames, djinn_shadow_stack.frames,
           (size_t)djinn_shadow_stack.depth * sizeof(djinn_frame_t));
}

// ── Variable assignment history ──

typedef struct
{
    const char* line_text;
    uint32_t line;
} djinn_var_event_t;

typedef struct
{
    const void* slot;
    const char* name;
    djinn_var_event_t events[DJINN_VAR_HISTORY]; /* newest last */
    int count; /* events stored (0..DJINN_VAR_HISTORY) */
    int total; /* total assignments seen (drives the "..." marker) */
} djinn_tracked_var_t;

static struct
{
    djinn_tracked_var_t vars[DJINN_MAX_TRACKED_VARS];
    int count;
} djinn_var_registry;

void __djinn_var_track(const void* slot, const char* name, const char* line_text, const uint32_t line)
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

    if (var->count == DJINN_VAR_HISTORY)
    {
        var->events[0] = var->events[1];
        var->count--;
    }
    var->events[var->count].line_text = line_text;
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

static int djinn_append_snippet(char* buf, int used, const djinn_error_info_t* info)
{
    if (info->line == 0 || info->file == NULL) return used;

    char line_num[16];
    snprintf(line_num, sizeof line_num, "%u", (unsigned)info->line);
    const size_t gutter = strlen(line_num);

    used = djinn_report_append(buf, used, "  --> %s:%u:%u\n", info->file,
                               (unsigned)info->line, (unsigned)info->column);
    used = djinn_report_append(buf, used, " %*s |\n", (int)gutter, "");

    if (info->line_text != NULL && info->line_text[0] != '\0')
    {
        // Trim trailing whitespace from the snippet so the caret stays aligned
        size_t len = strlen(info->line_text);
        while (len > 0 && (info->line_text[len - 1] == ' '
                           || info->line_text[len - 1] == '\t'
                           || info->line_text[len - 1] == '\r'))
            len--;
        used = djinn_report_append(buf, used, " %s | %.*s\n", line_num, (int)len, info->line_text);

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
        used = djinn_report_append(buf, used, " %4u | %s\n",
                                   (unsigned)var->events[i].line,
                                   var->events[i].line_text ? var->events[i].line_text : "");
    }
    return used;
}

static int djinn_render_frames(char* buf, int used, const djinn_frame_t* frames, const int depth)
{
    if (depth == 0) return used;

    used = djinn_report_append(buf, used, "stack trace:\n");
    for (int i = depth - 1; i >= 0; i--)
    {
        const djinn_frame_t* frame = &frames[i];
        used = djinn_report_append(buf, used, "  at %s (%s:%u)\n",
                                   frame->function_name ? frame->function_name : "<unknown>",
                                   frame->file ? frame->file : "?",
                                   (unsigned)frame->line);
    }
    return used;
}

static void djinn_native_stack(int skip);

static int djinn_append_stack_trace(char* buf, int used)
{
    used = djinn_render_frames(buf, used, djinn_shadow_stack.frames, djinn_shadow_stack.depth);
    if (djinn_shadow_stack.overflowed)
        used = djinn_report_append(buf, used, "  ... (more than %d frames, older frames omitted)\n",
                                   DJINN_MAX_FRAMES);
    return used;
}

void __djinn_runtime_error(const djinn_error_info_t* info)
{
    djinn_error_info_t fallback;
    if (info == NULL)
    {
        memset(&fallback, 0, sizeof fallback);
        fallback.message = "unknown";
        info = &fallback;
    }

    // The innermost frame should point at the failing operation, not at the
    // function definition line.
    __djinn_frame_set_line(info->line);

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
    used = djinn_append_stack_trace(report, used);

    fputs(report, stderr);
    fflush(stderr);
    if (djinn_shadow_stack.depth == 0) djinn_native_stack(2);
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
    // The live shadow stack is empty by now (the error unwound normally), so
    // render the snapshot taken at the throw site instead
    used = djinn_render_frames(report, used, djinn_error_stack.frames, djinn_error_stack.depth);

    fputs(report, stderr);
    fflush(stderr);
    abort();
}

const char* __djinn_error_stack_text(void)
{
    char buffer[DJINN_ERROR_REPORT_SIZE];
    const int used = djinn_render_frames(buffer, 0, djinn_error_stack.frames, djinn_error_stack.depth);
    char* copy = (char*)malloc((size_t)used + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, buffer, (size_t)used);
    copy[used] = '\0';
    return copy;
}

void __djinn_panic(const char* message, const char* file, const uint32_t line)
{
    char* report = __djinn_last_error_report;
    int used = djinn_report_append(report, 0, "djinn runtime error: panic\n");
    used = djinn_report_append(report, used, "  error: %s\n", (message != NULL && message[0] != '\0') ? message : "<no message>");
    if (file != NULL && file[0] != '\0')
        used = djinn_report_append(report, used, "  --> %s:%u\n", file, (unsigned)line);
    used = djinn_render_frames(report, used, djinn_shadow_stack.frames, djinn_shadow_stack.depth);
    fputs(report, stderr);
    fflush(stderr);
    if (djinn_shadow_stack.depth == 0) djinn_native_stack(2);
    abort();
}


// ── Native stack trace (symbolized through the debug info emitted by -g) ──
// Windows resolves frames with dbghelp and the PDB; POSIX with addr2line
// against the DWARF of the running executable. Without debug info the frames
// still print, just without file and line.
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

__attribute__((noinline)) static void djinn_native_stack(const int skip)
{
    void* addresses[64];
    const USHORT count = CaptureStackBackTrace((ULONG)skip, 64, addresses, NULL);
    const HANDLE process = GetCurrentProcess();
    static int initialized;
    if (!initialized)
    {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, NULL, TRUE);
        initialized = 1;
    }

    char storage[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)storage;
    fputs("native stack trace:\n", stderr);
    for (USHORT i = 0; i < count; i++)
    {
        const DWORD64 address = (DWORD64)(uintptr_t)addresses[i];
        memset(symbol, 0, sizeof(SYMBOL_INFO));
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 symbol_offset = 0;
        const char* name = SymFromAddr(process, address, &symbol_offset, symbol) ? symbol->Name : "<unknown>";
        IMAGEHLP_LINE64 line;
        memset(&line, 0, sizeof line);
        line.SizeOfStruct = sizeof line;
        DWORD line_offset = 0;
        if (SymGetLineFromAddr64(process, address, &line_offset, &line))
            fprintf(stderr, "  at %s (%s:%lu)\n", name, line.FileName, (unsigned long)line.LineNumber);
        else
            fprintf(stderr, "  at %s\n", name);
    }
    fflush(stderr);
}

static LPTOP_LEVEL_EXCEPTION_FILTER djinn_previous_filter;

static LONG WINAPI djinn_crash_filter(EXCEPTION_POINTERS* info)
{
    EXCEPTION_RECORD* record = info->ExceptionRecord;
    fprintf(stderr, "djinn crash: exception 0x%08lx at %p\n", (unsigned long)record->ExceptionCode, record->ExceptionAddress);
    HMODULE module = NULL;
    char module_name[260] = "";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)record->ExceptionAddress, &module))
    {
        GetModuleFileNameA(module, module_name, sizeof(module_name));
        fprintf(stderr, "  in %s +0x%llx\n", module_name, (unsigned long long)((char*)record->ExceptionAddress - (char*)module));
    }
    if (record->NumberParameters >= 2)
        fprintf(stderr, "  access type %llu address %p\n", (unsigned long long)record->ExceptionInformation[0], (void*)record->ExceptionInformation[1]);
    djinn_native_stack(1);
    if (djinn_previous_filter != NULL) return djinn_previous_filter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

__attribute__((constructor)) static void djinn_install_crash_trace(void)
{
    djinn_previous_filter = SetUnhandledExceptionFilter(djinn_crash_filter);
}
#else
#include <execinfo.h>
#include <signal.h>
#include <dlfcn.h>
#include <elf.h>

extern const Elf64_Ehdr __ehdr_start;

__attribute__((noinline)) static void djinn_native_stack(const int skip)
{
    void* addresses[64];
    const int count = backtrace(addresses, 64);
    fputs("native stack trace:\n", stderr);

    char command[2048];
    char executable[1024];
    const ssize_t executable_length = readlink("/proc/self/exe", executable, sizeof executable - 1);
    executable[executable_length > 0 ? executable_length : 0] = ' ';
    int used = snprintf(command, sizeof command, "addr2line -f -C -p -e '%s'", executable);
    for (int i = skip; i < count && used < (int)sizeof command - 32; i++)
    {
        Dl_info info;
        uintptr_t address = (uintptr_t)addresses[i] - 1;
        if (__ehdr_start.e_type == ET_DYN && dladdr(addresses[i], &info) && info.dli_fbase != NULL)
            address -= (uintptr_t)info.dli_fbase;
        used += snprintf(command + used, sizeof command - (size_t)used, " 0x%lx", (unsigned long)address);
    }

    FILE* pipe = popen(command, "r");
    int printed = 0;
    if (pipe != NULL)
    {
        char row[512];
        while (fgets(row, sizeof row, pipe) != NULL)
        {
            fprintf(stderr, "  at %s", row);
            printed = 1;
        }
        pclose(pipe);
    }
    if (!printed) backtrace_symbols_fd(addresses + skip, count - skip, 2);
    fflush(stderr);
}

static void djinn_crash_signal(const int signal_number)
{
    fprintf(stderr, "djinn crash: signal %d\n", signal_number);
    djinn_native_stack(1);
    signal(signal_number, SIG_DFL);
    raise(signal_number);
}

__attribute__((constructor)) static void djinn_install_crash_trace(void)
{
    const int signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL};
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; i++)
    {
        struct sigaction current;
        if (sigaction(signals[i], NULL, &current) != 0 || current.sa_handler != SIG_DFL) continue;
        struct sigaction handler;
        memset(&handler, 0, sizeof handler);
        handler.sa_handler = djinn_crash_signal;
        sigemptyset(&handler.sa_mask);
        sigaction(signals[i], &handler, NULL);
    }
}
#endif

void __djinn_link_debug_support(void)
{
}
