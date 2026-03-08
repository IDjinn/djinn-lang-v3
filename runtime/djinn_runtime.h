//
// Djinn Runtime v2 — Header
// Event loop, thread pool, task queue, non-blocking I/O (IOCP/epoll),
// TCP sockets, threading primitives, console I/O
//

#ifndef DJINN_RUNTIME_H
#define DJINN_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

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

#ifdef _WIN32
#define DJINN_API __declspec(dllexport)
#else
#define DJINN_API
#endif

// ════════════════════════════════════════════════════════════════════
// Memory
// ════════════════════════════════════════════════════════════════════

DJINN_API void* __djinn_malloc(size_t size);
DJINN_API void __djinn_free(void* pointer);

// ════════════════════════════════════════════════════════════════════
// I/O Request Types
// ════════════════════════════════════════════════════════════════════

#define DJINN_IO_FILE_READ   0
#define DJINN_IO_FILE_WRITE  1
#define DJINN_IO_ACCEPT      2
#define DJINN_IO_CONNECT     3
#define DJINN_IO_RECV        4
#define DJINN_IO_SEND        5

// ════════════════════════════════════════════════════════════════════
// Task
// ════════════════════════════════════════════════════════════════════

typedef struct djinn_task
{
    void* handle; // coroutine handle
    struct djinn_task* next; // linked list
    int priority; // 0 = normal, 1 = high
} djinn_task_t;

// ════════════════════════════════════════════════════════════════════
// Task Queue (thread-safe)
// ════════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════════
// Thread Pool
// ════════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════════
// I/O Request
// ════════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════════
// Runtime State
// ════════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════════
// Continuation: when child completes, resume parent
// ════════════════════════════════════════════════════════════════════

typedef struct djinn_continuation
{
    void* child;
    void* parent;
    struct djinn_continuation* next;
} djinn_continuation_t;

#define DJINN_MAX_WAITING 256

// ════════════════════════════════════════════════════════════════════
// Lifecycle
// ════════════════════════════════════════════════════════════════════

DJINN_API void __djinn_runtime_init(int num_threads);
DJINN_API void __djinn_runtime_shutdown(void);

// ════════════════════════════════════════════════════════════════════
// Coro wrappers (defined in LLVM IR, called by runtime)
// ════════════════════════════════════════════════════════════════════

extern void __djinn_coro_resume(void* handle);
extern int __djinn_coro_done(void* handle);
extern void __djinn_coro_destroy(void* handle);
extern void* __djinn_coro_promise(void* handle, int align);

// ════════════════════════════════════════════════════════════════════
// Task Management
// ════════════════════════════════════════════════════════════════════

DJINN_API void __djinn_spawn(void* coro_handle);
DJINN_API int __djinn_event_loop(void* main_handle);
DJINN_API void __djinn_event_loop_run(void* main_handle);

// ════════════════════════════════════════════════════════════════════
// Await / Suspend
// ════════════════════════════════════════════════════════════════════

DJINN_API void __djinn_mark_waiting(void* handle);
DJINN_API void __djinn_await(void* child_handle, void* parent_handle);

// ════════════════════════════════════════════════════════════════════
// Async File I/O (blocking thread)
// ════════════════════════════════════════════════════════════════════

DJINN_API int64_t __djinn_async_read(int fd, void* buf, int64_t count, void* coro);
DJINN_API int64_t __djinn_async_write(int fd, void* buf, int64_t count, void* coro);

// ════════════════════════════════════════════════════════════════════
// Async Socket I/O (IOCP / epoll)
// ════════════════════════════════════════════════════════════════════

DJINN_API int64_t __djinn_socket_create(void);
DJINN_API int64_t __djinn_socket_close(int64_t socket_fd);
DJINN_API int64_t __djinn_socket_bind(int64_t socket_fd, const char* address, int port);
DJINN_API int64_t __djinn_socket_listen(int64_t socket_fd, int backlog);

DJINN_API int64_t __djinn_async_accept(int64_t server_sock, int64_t* out_result, void* coro);
DJINN_API int64_t __djinn_async_connect(int64_t socket_fd, const char* address, int port, int64_t* out_result,
                                        void* coro);
DJINN_API int64_t __djinn_async_send(int64_t socket_fd, void* buffer, int64_t count, int64_t* out_result, void* coro);
DJINN_API int64_t __djinn_async_recv(int64_t socket_fd, void* buffer, int64_t count, int64_t* out_result, void* coro);

// ════════════════════════════════════════════════════════════════════
// Threading (C# style)
// ════════════════════════════════════════════════════════════════════

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

DJINN_API djinn_thread_t* __djinn_thread_create(void (*func)(void*), void* arg);
DJINN_API int __djinn_thread_start(djinn_thread_t* thread);
DJINN_API void __djinn_thread_join(djinn_thread_t* thread);
DJINN_API int __djinn_thread_is_alive(djinn_thread_t* thread);
DJINN_API void __djinn_thread_sleep(int64_t milliseconds);

DJINN_API djinn_mutex_t* __djinn_mutex_create(void);
DJINN_API void __djinn_mutex_lock(djinn_mutex_t* mutex);
DJINN_API void __djinn_mutex_unlock(djinn_mutex_t* mutex);
DJINN_API int __djinn_mutex_trylock(djinn_mutex_t* mutex);
DJINN_API void __djinn_mutex_destroy(djinn_mutex_t* mutex);

// ════════════════════════════════════════════════════════════════════
// Console I/O (async wrappers)
// ════════════════════════════════════════════════════════════════════

DJINN_API int64_t __djinn_console_write(const char* str, void* coro);
DJINN_API int64_t __djinn_console_error(const char* str, void* coro);
DJINN_API int64_t __djinn_console_read_line(char* buf, int64_t max, void* coro);

#ifdef __cplusplus
}
#endif

#endif // DJINN_RUNTIME_H