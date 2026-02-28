//
// Djinn Async Runtime — Header
// Event loop, thread pool, task queue, async I/O
//

#ifndef DJINN_RUNTIME_H
#define DJINN_RUNTIME_H

#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define DJINN_API __declspec(dllexport)
#else
#define DJINN_API
#endif

DJINN_API void* __djinn_malloc(size_t size);

// ── Task ──
typedef struct djinn_task
{
    void* handle; // coroutine handle
    struct djinn_task* next; // linked list
    int priority; // 0 = normal, 1 = high
} djinn_task_t;

// ── Task Queue (thread-safe) ──
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

// ── Thread Pool ──
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

// ── I/O Request ──
typedef struct djinn_io_request
{
    int fd;
    void* buffer;
    int64_t count;
    int64_t result; // bytes read/written
    int completed;
    void* waiting_coro; // coroutine to resume when done
    int type; // 0=read, 1=write
    struct djinn_io_request* next;
} djinn_io_request_t;

// ── Runtime State ──
typedef struct
{
    djinn_thread_pool_t pool;
    djinn_task_queue_t ready_queue; // tasks ready to execute
    djinn_io_request_t* io_pending; // pending I/O requests
#ifdef _WIN32
    HANDLE io_thread;
    CRITICAL_SECTION io_mutex;
#else
    pthread_t io_thread;
    pthread_mutex_t io_mutex;
#endif
    volatile int running;
} djinn_runtime_t;

// ── Continuation: when child completes, resume parent ──
typedef struct djinn_continuation
{
    void* child;
    void* parent;
    struct djinn_continuation* next;
} djinn_continuation_t;

#define DJINN_MAX_WAITING 256

// ── Lifecycle ──
void __djinn_runtime_init(int num_threads);
void __djinn_runtime_shutdown(void);

// ── Coro wrappers (defined in LLVM IR, called by runtime) ──
extern void __djinn_coro_resume(void* handle);
extern int __djinn_coro_done(void* handle);
extern void __djinn_coro_destroy(void* handle);
extern void* __djinn_coro_promise(void* handle, int align);

// ── Task management ──
void __djinn_spawn(void* coro_handle);
int __djinn_event_loop(void* main_handle);
void __djinn_event_loop_run(void* main_handle);

// ── Await / Suspend ──
void __djinn_mark_waiting(void* handle);
void __djinn_await(void* child_handle, void* parent_handle);

// ── Async I/O ──
int64_t __djinn_async_read(int fd, void* buf, int64_t count, void* coro);
int64_t __djinn_async_write(int fd, void* buf, int64_t count, void* coro);

#ifdef __cplusplus
}
#endif

#endif // DJINN_RUNTIME_H
