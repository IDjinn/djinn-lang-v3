//
// Djinn Runtime v2 — Header
// Event loop, thread pool, task queue, non-blocking I/O (IOCP/epoll),
// TCP sockets, threading primitives, console I/O
//

#ifndef DJINN_RUNTIME_H
#define DJINN_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
#define likely(x)   (x)
#define unlikely(x) (x)
#define assume(x)   __assume(x)
#define djinn_trap()        __assume(0)
#define djinn_unreachable() __assume(0)
#define DJINN_ASSERT_COLD_NORETURN __declspec(noreturn)
#else
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define assume(x)   __builtin_assume((x))
#define djinn_trap()        __builtin_trap()
#define djinn_unreachable() __builtin_unreachable()
#define DJINN_ASSERT_COLD_NORETURN __attribute__((cold, __noreturn__))
#endif

DJINN_ASSERT_COLD_NORETURN
static void djinn_assert_fail(const char* msg, const char* file, const int line, const char* function_name)
{
    fprintf(stderr, "[RUNTIME] ASSERTION ERROR: %s\nFile: %s:%d\nFunction: %s\n", msg, file, line, function_name);
    abort();
}

#define DJINN_ASSERT(cond, msg)                                   \
do {                                                              \
    if (unlikely(!(cond))) {                                      \
        djinn_assert_fail(msg, __FILE__, __LINE__, __FUNCTION__); \
        djinn_trap();                                             \
        djinn_unreachable();                                      \
    }                                                             \
    assume(cond);                                                 \
} while (0)

#if defined(_MSC_VER)
#define DJINN_ATTR_NONNULL_1
#define DJINN_ATTR_REALLOC
#define DJINN_ATTR_MALLOC
#else
#define DJINN_ATTR_NONNULL_1 __attribute__((nonnull(1)))
#define DJINN_ATTR_REALLOC   __attribute__((alloc_size(2), warn_unused_result))
#define DJINN_ATTR_MALLOC    __attribute__((malloc, alloc_size(1), returns_nonnull))
#endif

#if defined(_MSC_VER)
#define DJINN_TLS __declspec(thread)
#else
#define DJINN_TLS _Thread_local
#endif

// ── Error state (errno-style propagation) ──
//
// The generator lowers `throw`/`throws`/`try` to writes/reads of this
// thread-local struct: throw sites store tag/message/type/origin and return
// a default value; call sites of throwing functions reload the flag and
// propagate. Thread-local so concurrent runtime worker threads never
// interleave errors. Tags mirror binder/ErrorTypes.h (1..99 builtin, 100+
// user error structs).

typedef struct djinn_errno
{
    int32_t flag; /* 1 = error in flight */
    int32_t tag; /* 0 = none; builtin/user tag */
    const char* message; /* heap string from the error value */
    const char* type_name; /* static per-type name; NULL for builtin tags */
    const char* origin_file;
    uint32_t origin_line;
    uint32_t origin_column;
} djinn_errno_t;

extern DJINN_TLS djinn_errno_t __djinn_errno;

// ── Coroutine promise ABI ──
//
// Every async function's promise is { i32 err_tag, ptr err_message,
// ptr err_type_name, T value } — the error slot comes first so the runtime
// can read it at fixed offsets (0/8/16) without knowing T, letting errors
// cross `await` boundaries (the resuming thread's error state may differ
// from the throwing one's). All promises are 16-byte aligned.

#define DJINN_PROMISE_ALIGN 16
#define DJINN_PROMISE_ERR_TAG_OFFSET 0
#define DJINN_PROMISE_ERR_MESSAGE_OFFSET 8
#define DJINN_PROMISE_ERR_TYPE_OFFSET 16
#define DJINN_PROMISE_VALUE_OFFSET 24

uint64_t __djinn_hash_string(const char* data, uint32_t length);

int64_t __djinn_unix_timestamp_ms(void);

uint32_t __djinn_compare_strings(
    char* leftData,
    char* rightData,
    size_t leftLen,
    size_t rightLen
);

void* __djinn_realloc(void* pointer, size_t new_size);

void __djinn_free(void* pointer);

void* __djinn_malloc(size_t size);

#define DJINN_IO_FILE_READ   0
#define DJINN_IO_FILE_WRITE  1
#define DJINN_IO_ACCEPT      2
#define DJINN_IO_CONNECT     3
#define DJINN_IO_RECV        4
#define DJINN_IO_SEND        5

typedef struct djinn_task
{
    void* handle; // coroutine handle
    struct djinn_task* next; // linked list
    int priority; // 0 = normal, 1 = high
} djinn_task_t;


typedef struct
{
    djinn_task_t* head;
    djinn_task_t* tail;
    int count;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE cond;
#else
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif
} djinn_task_queue_t;

typedef struct
{
#ifdef _WIN32
    HANDLE* threads;
#else
    pthread_t* threads;
#endif
    int thread_count;
    djinn_task_queue_t queue;
    volatile int shutdown;
} djinn_thread_pool_t;

typedef struct djinn_io_request
{
    int type; // DJINN_IO_FILE_READ .. DJINN_IO_SEND
    void* buffer;
    int64_t count;
    int64_t result; // bytes read/written or accepted socket
    int64_t* out_result; // pointer to write result back to caller before resume
    int completed;
    void* waiting_coro; // coroutine to resume when done
    struct djinn_io_request* next;

    union
    {
        int fd; // file descriptor (file I/O)
        int64_t socket; // socket handle (socket I/O)
    };

    // Socket-specific (connect/accept)
    int port;
    char addr[64];

#ifdef _WIN32
    OVERLAPPED overlapped; // IOCP overlapped structure
    WSABUF wsabuf; // WSA buffer descriptor
    char accept_buf[2 * (sizeof(struct sockaddr_in) + 16)]; // AcceptEx buffer
    SOCKET accepted_socket; // result socket from AcceptEx
#endif
} djinn_io_request_t;


typedef struct
{
    djinn_thread_pool_t pool;
    djinn_task_queue_t ready_queue; // tasks ready to execute

    // ── File I/O (dedicated blocking thread) ──
    djinn_io_request_t* io_pending; // pending file I/O requests
#ifdef _WIN32
    HANDLE file_io_thread;
    CRITICAL_SECTION file_io_mutex;
#else
    pthread_t file_io_thread;
    pthread_mutex_t file_io_mutex;
#endif

    // ── Socket Poller (IOCP / epoll) ──
#ifdef _WIN32
    HANDLE iocp; // I/O Completion Port
    HANDLE socket_poller_thread;
#else
    int epoll_fd;
    pthread_t socket_poller_thread;
#endif

#ifdef _WIN32
    CRITICAL_SECTION socket_mutex;
#else
    pthread_mutex_t socket_mutex;
#endif

    volatile int running;
} djinn_runtime_t;

typedef struct djinn_continuation
{
    void* child;
    void* parent;
    struct djinn_continuation* next;
} djinn_continuation_t;

#define DJINN_MAX_WAITING 256

void __djinn_runtime_init(int num_threads);
void __djinn_runtime_shutdown(void);

// ── Runtime error reporting (traps: integer overflow, division by zero) ──
//
// The generator bakes a djinn_error_info_t at each trap site and calls
// __djinn_runtime_error, which renders a compile-time-style report
// ( --> file:line:col, source snippet, caret underline, note with the
// operand values) followed by the native stack trace, stores it in
// __djinn_last_error_report (so JIT hosts can capture it) and aborts.

typedef struct djinn_error_info
{
    const char* message;   /* "integer overflow" / "division by zero" */
    const char* file;      /* source file display name */
    const char* source_root; /* absolute dir joined with `file` to read the
                                snippet from disk at report time; NULL when
                                unknown (reports degrade to file:line) */
    uint32_t line;         /* 1-based */
    uint32_t column;       /* 1-based */
    uint32_t length;       /* caret span length (>= 1) */
    uint8_t op;            /* '+','-','*','/','%','n' (negate), 0 = none */
    uint8_t bits;          /* operand width: 8/16/32/64 */
    uint8_t is_signed;
    uint8_t has_operands;  /* 0 => left/right are not meaningful */
    uint64_t left;         /* raw operand bits */
    uint64_t right;        /* raw operand bits (unused for 'n') */
    const char* left_var_name; /* variable name; NULL when not a tracked variable */
    const void* left_var_slot; /* variable storage (identity for its history) */
    const char* right_var_name;
    const void* right_var_slot;
} djinn_error_info_t;

#define DJINN_ERROR_REPORT_SIZE 2048
extern char __djinn_last_error_report[DJINN_ERROR_REPORT_SIZE];

void __djinn_runtime_error(const djinn_error_info_t* info);
void __djinn_runtime_error_message(const char* message); /* legacy simple trap */

// Release-build trap: scalar-argument variant of __djinn_runtime_error. The
// generator emits one call per site with no descriptor struct and no per-site
// strings (file/message globals are deduplicated per module); the runtime
// fills a djinn_error_info_t and renders location + operand note without the
// source snippet, variable history or stack trace.
void __djinn_runtime_error_min(const char* message, const char* file, uint32_t line, uint32_t column,
                               uint8_t op, uint8_t bits, uint8_t is_signed, uint8_t has_operands,
                               uint64_t left, uint64_t right);

// Uncaught djinn exception escaping main() throws: renders a report with the
// error type name ("Type: message", falling back to the builtin tag names),
// the outermost unhandled call site (the origin rises as the error
// propagates) and — when frames exist — the stack snapshot captured at the
// throw site, into __djinn_last_error_report (so JIT hosts can capture it),
// prints it to stderr and aborts.
void __djinn_uncaught_error(int tag, const char* type_name, const char* message,
                            const char* origin_file, uint32_t origin_line, uint32_t origin_column);

// ── Variable assignment history ──
//
// The generator records each integer variable declaration/assignment with its
// source file + line; when an operand of a failing operation is a variable,
// the report shows its last DJINN_VAR_HISTORY assignment sites, reading the
// line text from disk at report time.

#define DJINN_MAX_TRACKED_VARS 128
#define DJINN_VAR_HISTORY 2
#define DJINN_SOURCE_LINE_MAX 256

void __djinn_var_track(const void* slot, const char* name, const char* file,
                       const char* source_root, uint32_t line);

// ── Interpolated error message formatting ──
//
// Interpolated throw messages are formatted into a fixed thread-local buffer
// instead of a heap allocation: the message pointer must outlive unwinding
// and synchronous propagation, and per-throw mallocs would defeat the
// allocation-free error path. Same "{idx}" placeholder syntax and object
// boxing as Console.format. Returns the buffer pointer, valid until the next
// throw on this thread.

#define DJINN_ERROR_MESSAGE_MAX 256

const char* __djinn_error_format(const char* fmt_data, uint32_t fmt_len,
                                 const void* boxed_objects, int32_t count);

// Registers an in-memory source text under its file id so snippet extraction
// works for sources that were never written to disk (run()/tests). Borrows
// the host's string (must stay valid while the compiled program runs);
// re-registering an id replaces its text. No allocation.
void __djinn_register_source_text(const char* file_id, const char* text);

// ── Native stack traces ──
//
// Traces are captured with the platform unwinder at throw/trap sites — a
// validated frame-pointer walk on Windows x64 (JIT frames have no unwind
// info), backtrace() elsewhere — and symbolized lazily, only when a report is
// printed: dbghelp (PDB) on Windows, dladdr + llvm-symbolizer (DWARF) on
// POSIX, plus any symbols a JIT host registers. Nothing runs on the happy
// path.

#define DJINN_MAX_TRACE_FRAMES 64

// Captures the native stack into the thread-local error trace. Called at
// throw sites so the uncaught-exception report keeps the trace after the
// error unwinds; traps capture inline instead.
void __djinn_capture_backtrace(void);

// Capture into caller storage (used by the exceptions shim so thrown error
// objects carry their own raise-site trace).
int __djinn_capture_backtrace_into(void** frames, int max);

// Points the lazy symbolizer at an llvm-symbolizer binary (POSIX file:line
// info); optional — without it traces fall back to dladdr names only.
void __djinn_symbolizer_set_path(const char* path);

// Registers address->name pairs for JIT-compiled functions; the symbolizer
// consults them before the platform symbol sources. Names are copied.
void __djinn_jit_register_symbols(const char* const* names, const void* const* addresses, int count);

extern void __djinn_coro_resume(void* handle);
extern int __djinn_coro_done(void* handle);
extern void __djinn_coro_destroy(void* handle);
extern void* __djinn_coro_promise(void* handle, int align);

void __djinn_spawn(void* coro_handle);
int __djinn_event_loop(void* main_handle);
void __djinn_event_loop_run(void* main_handle);

void __djinn_mark_waiting(void* handle);
void __djinn_await(void* child_handle, void* parent_handle);


int64_t __djinn_async_read(int fd, void* buf, int64_t count, void* coro);
int64_t __djinn_async_write(int fd, void* buf, int64_t count, void* coro);


int64_t __djinn_socket_create(void);
int64_t __djinn_socket_close(int64_t socket_fd);
int64_t __djinn_socket_bind(int64_t socket_fd, const char* address, int port);
int64_t __djinn_socket_listen(int64_t socket_fd, int backlog);

int64_t __djinn_async_accept(int64_t server_sock, int64_t* out_result, void* coro);
int64_t __djinn_async_connect(int64_t socket_fd, const char* address, int port, int64_t* out_result,
                              void* coro);
int64_t __djinn_async_send(int64_t socket_fd, void* buffer, int64_t count, int64_t* out_result,
                           void* coro);
int64_t __djinn_async_recv(int64_t socket_fd, void* buffer, int64_t count, int64_t* out_result, void* coro);

typedef struct djinn_thread
{
#ifdef _WIN32
    HANDLE handle;
    DWORD id;
#else
    pthread_t handle;
#endif
    void (*func)(void*);
    void* arg;
    volatile int alive;
} djinn_thread_t;

typedef struct djinn_mutex
{
#ifdef _WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t mtx;
#endif
} djinn_mutex_t;

djinn_thread_t* __djinn_thread_create(void (*func)(void*), void* arg);
int __djinn_thread_start(djinn_thread_t* thread);
void __djinn_thread_join(djinn_thread_t* thread);
int __djinn_thread_is_alive(djinn_thread_t* thread);
void __djinn_thread_sleep(int64_t milliseconds);

djinn_mutex_t* __djinn_mutex_create(void);
void __djinn_mutex_lock(djinn_mutex_t* mutex);
void __djinn_mutex_unlock(djinn_mutex_t* mutex);
int __djinn_mutex_trylock(djinn_mutex_t* mutex);
void __djinn_mutex_destroy(djinn_mutex_t* mutex);


int64_t __djinn_console_write(const char* str, void* coro);
int64_t __djinn_console_error(const char* str, void* coro);
int64_t __djinn_console_read_line(char* buf, int64_t max, void* coro);

#ifdef __cplusplus
}
#endif

#endif // DJINN_RUNTIME_H