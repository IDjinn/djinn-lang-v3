//
// Djinn Async Runtime — Implementation
// Event loop, thread pool, task queue, async I/O
//

#include "djinn_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#define DJINN_ASSERT(condition, message)                            \
do {                                                                \
    if (!(condition)) {                                             \
        fprintf(stderr,                                             \
            "[RUNTIME] ASSERTION ERROR: %s\nFile: %s\nLine: %d\n",  \
            message, __FILE__, __LINE__                             \
        );                                                          \
        abort();                                                    \
    }                                                               \
} while (0)

#define DJINN_ENABLE_TRACE

#ifdef DJINN_ENABLE_TRACE
#define DJINN_TRACE(message, ...) fprintf(stdout, "[RUNTIME] " message "\n", ##__VA_ARGS__)
#else
#define DJINN_TRACE(message, ...) do {} while (0)
#endif

// ── Global runtime state ──
static djinn_runtime_t runtime;

// ── Waiting set: coroutines suspended for I/O or child completion ──
static void* waiting_handles[DJINN_MAX_WAITING];
static int waiting_count = 0;

// ── Continuation list: child→parent mappings ──
static djinn_continuation_t* continuations = NULL;

// ── Queue operations ──

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

DJINN_API void __djinn_free(void* pointer)
{
    DJINN_TRACE("de-alocating heap memory at %p\n", pointer);
    free(pointer);
    DJINN_TRACE("memory freed\n");
}

DJINN_API void* __djinn_malloc(size_t size)
{
    DJINN_TRACE("allocating size %zu on heap\n", size);
    void* chunk = calloc(1, size);
    DJINN_ASSERT(chunk, "Out of memory");
    DJINN_TRACE("allocated successed and zeroed at %p\n", chunk);
    return chunk;
}

static void destroy_queue(djinn_task_queue_t* q)
{
    // Free remaining tasks
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
        // Use timed wait to check shutdown periodically
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

// ── I/O thread ──

#ifdef _WIN32
static DWORD WINAPI io_thread_func(LPVOID arg)
{


#else
static void* io_thread_func(void* arg)
{
#endif
    (void)arg;
    while (runtime.running)
    {
#ifdef _WIN32
        EnterCriticalSection(&runtime.io_mutex);
#else
        pthread_mutex_lock(&runtime.io_mutex);
#endif

        djinn_io_request_t* req = runtime.io_pending;
        if (req)
        {
            runtime.io_pending = req->next;
        }

#ifdef _WIN32
        LeaveCriticalSection(&runtime.io_mutex);
#else
        pthread_mutex_unlock(&runtime.io_mutex);
#endif

        if (req)
        {
            // Perform blocking I/O
            if (req->type == 0)
            {
#ifdef _WIN32
                req->result = _read(req->fd, req->buffer, (unsigned int)req->count);
#else
                req->result = read(req->fd, req->buffer, (size_t)req->count);
#endif
            }
            else
            {
#ifdef _WIN32
                req->result = _write(req->fd, req->buffer, (unsigned int)req->count);
#else
                req->result = write(req->fd, req->buffer, (size_t)req->count);
#endif
            }
            req->completed = 1;

            // Resume waiting coroutine by putting it back in the ready queue
            if (req->waiting_coro)
            {
                enqueue_task(&runtime.ready_queue, req->waiting_coro);
            }
            free(req);
        }
        else
        {
            // No pending I/O — sleep briefly
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

// ── Worker thread ──

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
                // Re-queue if not finished
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

// ── Lifecycle ──

void __djinn_runtime_init(int num_threads)
{
    memset(&runtime, 0, sizeof(runtime));

    init_queue(&runtime.ready_queue);
    init_queue(&runtime.pool.queue);

#ifdef _WIN32
    InitializeCriticalSection(&runtime.io_mutex);
#else
    pthread_mutex_init(&runtime.io_mutex, NULL);
#endif

    runtime.running = 1;
    runtime.pool.shutdown = 0;

    // Create worker threads
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

    // Create I/O thread
#ifdef _WIN32
    runtime.io_thread = CreateThread(NULL, 0, io_thread_func, NULL, 0, NULL);
#else
    pthread_create(&runtime.io_thread, NULL, io_thread_func, NULL);
#endif
}

void __djinn_runtime_shutdown(void)
{
    runtime.running = 0;
    runtime.pool.shutdown = 1;

    // Wake all worker threads
    for (int i = 0; i < runtime.pool.thread_count; i++)
    {
#ifdef _WIN32
        WakeAllConditionVariable(&runtime.pool.queue.cond);
#else
        pthread_cond_broadcast(&runtime.pool.queue.cond);
#endif
    }

    // Join worker threads
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

    // Join I/O thread
#ifdef _WIN32
    WaitForSingleObject(runtime.io_thread, 5000);
    CloseHandle(runtime.io_thread);
#else
    pthread_join(runtime.io_thread, NULL);
#endif

    // Cleanup
    destroy_queue(&runtime.ready_queue);
    destroy_queue(&runtime.pool.queue);

    // Free pending I/O requests
#ifdef _WIN32
    EnterCriticalSection(&runtime.io_mutex);
#else
    pthread_mutex_lock(&runtime.io_mutex);
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
    LeaveCriticalSection(&runtime.io_mutex);
    DeleteCriticalSection(&runtime.io_mutex);
#else
    pthread_mutex_unlock(&runtime.io_mutex);
    pthread_mutex_destroy(&runtime.io_mutex);
#endif
}

// ── Waiting set / Continuation management ──

void __djinn_mark_waiting(void* handle)
{
    if (waiting_count < DJINN_MAX_WAITING)
    {
        waiting_handles[waiting_count++] = handle;
    }
}

// Non-consuming check: is handle in waiting set?
static int is_in_waiting_set(void* handle)
{
    for (int i = 0; i < waiting_count; i++)
    {
        if (waiting_handles[i] == handle)
            return 1;
    }
    return 0;
}

// Check if handle is in waiting set; if so, remove it and return 1
static int is_waiting(void* handle)
{
    for (int i = 0; i < waiting_count; i++)
    {
        if (waiting_handles[i] == handle)
        {
            // Remove by swapping with last
            waiting_handles[i] = waiting_handles[waiting_count - 1];
            waiting_count--;
            return 1;
        }
    }
    return 0;
}

// Remove handle from waiting set without returning status (cleanup for completed coros)
static void remove_from_waiting(void* handle)
{
    is_waiting(handle);
}

// Pop continuation for a completed child; returns parent handle or NULL
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

    // Register continuation: when child completes, resume parent
    djinn_continuation_t* cont = (djinn_continuation_t*)malloc(sizeof(djinn_continuation_t));
    if (!cont) return;
    cont->child = child_handle;
    cont->parent = parent_handle;
    cont->next = continuations;
    continuations = cont;

    // Mark parent as waiting (event loop won't re-enqueue it)
    __djinn_mark_waiting(parent_handle);

    // Only enqueue child if it's NOT already waiting for I/O (coro::suspend).
    // If child called coro::suspend(), the I/O thread will enqueue it when ready.
    if (!is_in_waiting_set(child_handle))
    {
        enqueue_task(&runtime.ready_queue, child_handle);
    }
}

// ── Task management ──

void __djinn_spawn(void* coro_handle)
{
    DJINN_ASSERT(coro_handle, "coro handle is NULL");
    enqueue_task(&runtime.ready_queue, coro_handle);
}

int __djinn_event_loop(void* main_handle)
{
    DJINN_ASSERT(main_handle, "main handle is NULL");
    // Only enqueue main if it's not already waiting (e.g., from __djinn_await during ramp)
    if (!is_in_waiting_set(main_handle))
    {
        enqueue_task(&runtime.ready_queue, main_handle);
    }

    // Main event loop
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
                        // I/O thread or child completion will enqueue it back
                    }
                    else
                    {
                        // Cooperative yield — re-enqueue immediately
                        enqueue_task(&runtime.ready_queue, task->handle);
                    }
                }
                else
                {
                    // Coroutine finished — clean up stale waiting entry + check continuation
                    remove_from_waiting(task->handle);
                    void* parent = pop_continuation(task->handle);
                    if (parent)
                    {
                        // Remove parent from waiting set so it can be resumed
                        remove_from_waiting(parent);
                        enqueue_task(&runtime.ready_queue, parent);
                    }
                    else if (task->handle != main_handle)
                    {
                        __djinn_coro_destroy(task->handle);
                    }
                }
            }
            else
            {
                // Coroutine was already done when dequeued (e.g., no suspend points —
                // ramp ran entire body to final suspend). Still need to process continuations.
                remove_from_waiting(task->handle);
                void* parent = pop_continuation(task->handle);
                if (parent)
                {
                    remove_from_waiting(parent);
                    enqueue_task(&runtime.ready_queue, parent);
                }
                else if (task->handle != main_handle)
                {
                    __djinn_coro_destroy(task->handle);
                }
            }
            free(task);
        }

        // Check if main coroutine is done
        if (__djinn_coro_done(main_handle))
        {
            // Drain remaining tasks in ready queue
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
                // Handle continuations for completed tasks (whether just finished or already done)
                if (__djinn_coro_done(remaining->handle))
                {
                    remove_from_waiting(remaining->handle);
                    void* parent = pop_continuation(remaining->handle);
                    if (parent)
                    {
                        remove_from_waiting(parent);
                        enqueue_task(&runtime.ready_queue, parent);
                    }
                }
                // Destroy completed spawned tasks (NOT main — we need its promise)
                if (remaining->handle != main_handle && __djinn_coro_done(remaining->handle))
                {
                    __djinn_coro_destroy(remaining->handle);
                }
                free(remaining);
            }
            break;
        }

        // Brief yield if no tasks available to avoid busy-spinning
        if (!runtime.ready_queue.head)
        {
#ifdef _WIN32
            Sleep(0);
#else
            sched_yield();
#endif
        }
    }

    // Extract result from main coroutine's promise
    void* promise = __djinn_coro_promise(main_handle, 4);
    int result = *(int*)promise;
    __djinn_coro_destroy(main_handle);
    return result;
}

void __djinn_event_loop_run(void* main_handle)
{
    DJINN_ASSERT(main_handle, "main handle is NULL");
    // Only enqueue main if it's not already waiting (e.g., from __djinn_await during ramp)
    if (!is_in_waiting_set(main_handle))
    {
        enqueue_task(&runtime.ready_queue, main_handle);
    }

    // Event loop — runs until main coroutine completes
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
                    // Coroutine finished — clean up stale waiting entry + check continuation
                    remove_from_waiting(task->handle);
                    void* parent = pop_continuation(task->handle);
                    if (parent)
                    {
                        // Remove parent from waiting set so it can be resumed
                        remove_from_waiting(parent);
                        enqueue_task(&runtime.ready_queue, parent);
                    }
                    else if (task->handle != main_handle)
                    {
                        __djinn_coro_destroy(task->handle);
                    }
                }
            }
            else
            {
                // Already done when dequeued (no suspend points)
                remove_from_waiting(task->handle);
                void* parent = pop_continuation(task->handle);
                if (parent)
                {
                    remove_from_waiting(parent);
                    enqueue_task(&runtime.ready_queue, parent);
                }
                else if (task->handle != main_handle)
                {
                    __djinn_coro_destroy(task->handle);
                }
            }
            free(task);
        }

        // Check if main coroutine is done
        if (__djinn_coro_done(main_handle))
        {
            // Drain remaining tasks in ready queue
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
                // Handle continuations for completed tasks
                if (__djinn_coro_done(remaining->handle))
                {
                    remove_from_waiting(remaining->handle);
                    void* parent = pop_continuation(remaining->handle);
                    if (parent)
                    {
                        remove_from_waiting(parent);
                        enqueue_task(&runtime.ready_queue, parent);
                    }
                }
                if (remaining->handle != main_handle && __djinn_coro_done(remaining->handle))
                {
                    __djinn_coro_destroy(remaining->handle);
                }
                free(remaining);
            }
            break;
        }

        // Brief yield if no tasks available to avoid busy-spinning
        if (!runtime.ready_queue.head)
        {
#ifdef _WIN32
            Sleep(0);
#else
            sched_yield();
#endif
        }
    }
    // NOTE: does NOT extract result or destroy main_handle
    // Caller (LLVM IR) does that via @llvm.coro.promise + @llvm.coro.destroy
}

// ── Async I/O ──

int64_t __djinn_async_read(int fd, void* buf, int64_t count, void* coro)
{
    DJINN_ASSERT(coro, "coro handle is NULL");
    DJINN_ASSERT(buf, "buf is NULL");
    DJINN_ASSERT(count > 0, "count <= 0");

    djinn_io_request_t* req = (djinn_io_request_t*)malloc(sizeof(djinn_io_request_t));
    if (!req) return -1;

    req->fd = fd;
    req->buffer = buf;
    req->count = count;
    req->result = 0;
    req->completed = 0;
    req->waiting_coro = coro;
    req->type = 0; // read
    req->next = NULL;

#ifdef _WIN32
    EnterCriticalSection(&runtime.io_mutex);
#else
    pthread_mutex_lock(&runtime.io_mutex);
#endif

    req->next = runtime.io_pending;
    runtime.io_pending = req;

#ifdef _WIN32
    LeaveCriticalSection(&runtime.io_mutex);
#else
    pthread_mutex_unlock(&runtime.io_mutex);
#endif

    return 0;
}

int64_t __djinn_async_write(int fd, void* buf, int64_t count, void* coro)
{
    DJINN_ASSERT(coro, "coro handle is NULL");
    DJINN_ASSERT(buf, "buf is NULL");
    DJINN_ASSERT(count > 0, "count <= 0");

    djinn_io_request_t* req = (djinn_io_request_t*)malloc(sizeof(djinn_io_request_t));
    if (!req) return -2;

    req->fd = fd;
    req->buffer = buf;
    req->count = count;
    req->result = 0;
    req->completed = 0;
    req->waiting_coro = coro;
    req->type = 1; // write
    req->next = NULL;

#ifdef _WIN32
    EnterCriticalSection(&runtime.io_mutex);
#else
    pthread_mutex_lock(&runtime.io_mutex);
#endif

    req->next = runtime.io_pending;
    runtime.io_pending = req;

#ifdef _WIN32
    LeaveCriticalSection(&runtime.io_mutex);
#else
    pthread_mutex_unlock(&runtime.io_mutex);
#endif

    return 0;
}