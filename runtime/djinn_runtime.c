#include "djinn_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "logger.h"
Logger* logger;

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#include <io.h>
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

__attribute__((nonnull(1)))
void __djinn_free(void* pointer)
{
    DJINN_TRACE("de-allocating heap memory at %p", pointer);
    free(pointer);
}

__attribute__((alloc_size(2), warn_unused_result)

)
void* __djinn_realloc(void* pointer, size_t new_size)
{
    void* chunk = realloc(pointer, new_size);
    DJINN_ASSERT(chunk, "Out of memory");
    DJINN_TRACE("re-allocated size %zu on heap at %p", new_size, chunk);
    return chunk;
}

__attribute__((malloc, alloc_size(1), returns_nonnull)
)
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

void __djinn_runtime_init(int num_threads)
{
    memset(&runtime, 0, sizeof(runtime));
    logger = logger_create("RUNTIME", 1, LOG_TRACE);

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

    void* promise = __djinn_coro_promise(main_handle, 4);
    const int result = *(int*)promise;
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
