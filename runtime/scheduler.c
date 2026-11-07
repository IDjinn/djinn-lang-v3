#include "runtime_internal.h"

djinn_runtime_t djinn_rt;
static void* waiting_handles[DJINN_MAX_WAITING];
static int waiting_count = 0;
static djinn_continuation_t* continuations = NULL;

#ifdef _WIN32
LPFN_ACCEPTEX djinn_pfn_accept_ex = NULL;
LPFN_CONNECTEX djinn_pfn_connect_ex = NULL;
#endif

void djinn_init_queue(djinn_task_queue_t* q)
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

void djinn_destroy_queue(djinn_task_queue_t* q)
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

void djinn_enqueue_task(djinn_task_queue_t* q, void* handle)
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

djinn_task_t* djinn_dequeue_task(djinn_task_queue_t* q)
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

djinn_task_t* djinn_dequeue_task_blocking(djinn_task_queue_t* q, volatile int* shutdown)
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

int djinn_is_in_waiting_set(void* handle)
{
    for (int i = 0; i < waiting_count; i++)
    {
        if (waiting_handles[i] == handle)
            return 1;
    }
    return 0;
}

int djinn_is_waiting(void* handle)
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

void djinn_remove_from_waiting(void* handle)
{
    djinn_is_waiting(handle);
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

    if (!djinn_is_in_waiting_set(child_handle))
    {
        djinn_enqueue_task(&djinn_rt.ready_queue, child_handle);
    }
}


#ifdef _WIN32
DWORD WINAPI djinn_worker_thread(LPVOID arg)
{
#else
    void* djinn_worker_thread(void* arg)
    {

#endif
    djinn_thread_pool_t* pool = (djinn_thread_pool_t*)arg;

    while (!pool->shutdown)
    {
        djinn_task_t* task = djinn_dequeue_task_blocking(&pool->queue, &pool->shutdown);
        if (!task) continue;

        if (!__djinn_coro_done(task->handle))
        {
            __djinn_coro_resume(task->handle);
            if (!__djinn_coro_done(task->handle))
            {
                djinn_enqueue_task(&pool->queue, task->handle);
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
    memset(&djinn_rt, 0, sizeof(djinn_rt));
    logger = logger_get("RUNTIME", 1, LOG_TRACE);
    logger_configure_from_properties(logger, "runtime.properties");
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Initialize Winsock
    WSADATA wsadata;
    const int wsa_err = WSAStartup(MAKEWORD(2, 2), &wsadata);
    DJINN_ASSERT(wsa_err == 0, "WSAStartup failed");
#endif

    djinn_init_queue(&djinn_rt.ready_queue);
    djinn_init_queue(&djinn_rt.pool.queue);

    // File I/O mutex
#ifdef _WIN32
    InitializeCriticalSection(&djinn_rt.file_io_mutex);
#else
    pthread_mutex_init(&djinn_rt.file_io_mutex, NULL);
#endif

    // Socket mutex
#ifdef _WIN32
    InitializeCriticalSection(&djinn_rt.socket_mutex);
#else
    pthread_mutex_init(&djinn_rt.socket_mutex, NULL);
#endif

    djinn_rt.running = 1;
    djinn_rt.pool.shutdown = 0;

#ifdef _WIN32
    djinn_rt.iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    DJINN_ASSERT(djinn_rt.iocp != NULL, "CreateIoCompletionPort failed");

    {
        const SOCKET tmp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tmp != INVALID_SOCKET)
        {
            DWORD bytes;
            GUID guid_acceptex = WSAID_ACCEPTEX;
            GUID guid_connectex = WSAID_CONNECTEX;
            WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid_acceptex, sizeof(guid_acceptex),
                     &djinn_pfn_accept_ex, sizeof(djinn_pfn_accept_ex), &bytes, NULL, NULL);
            WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid_connectex, sizeof(guid_connectex),
                     &djinn_pfn_connect_ex, sizeof(djinn_pfn_connect_ex), &bytes, NULL, NULL);
            closesocket(tmp);
        }
        DJINN_ASSERT(djinn_pfn_accept_ex != NULL, "Failed to load AcceptEx");
        DJINN_ASSERT(djinn_pfn_connect_ex != NULL, "Failed to load ConnectEx");
    }
#else
    djinn_rt.epoll_fd = epoll_create1(0);
    DJINN_ASSERT(djinn_rt.epoll_fd >= 0, "epoll_create1 failed");
#endif

    if (num_threads > 0)
    {
        djinn_rt.pool.thread_count = num_threads;
#ifdef _WIN32
        djinn_rt.pool.threads = (HANDLE*)malloc(sizeof(HANDLE) * num_threads);
        for (int i = 0; i < num_threads; i++)
        {
            djinn_rt.pool.threads[i] = CreateThread(NULL, 0, djinn_worker_thread, &djinn_rt.pool, 0, NULL);
        }
#else
        djinn_rt.pool.threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
        for (int i = 0; i < num_threads; i++)
        {
            pthread_create(&djinn_rt.pool.threads[i], NULL, djinn_worker_thread, &djinn_rt.pool);
        }
#endif
    }

#ifdef _WIN32
    djinn_rt.file_io_thread = CreateThread(NULL, 0, djinn_file_io_thread_func, NULL, 0, NULL);
#else
    pthread_create(&djinn_rt.file_io_thread, NULL, djinn_file_io_thread_func, NULL);
#endif

    // ── Create socket poller thread ──
#ifdef _WIN32
    djinn_rt.socket_poller_thread = CreateThread(NULL, 0, djinn_socket_poller_thread_func, NULL, 0, NULL);
#else
    pthread_create(&djinn_rt.socket_poller_thread, NULL, djinn_socket_poller_thread_func, NULL);
#endif

    DJINN_TRACE("runtime initialized (threads=%d, iocp/epoll=active)", num_threads);
}

void __djinn_runtime_shutdown(void)
{
    DJINN_TRACE("runtime shutting down");
    djinn_rt.running = 0;
    djinn_rt.pool.shutdown = 1;

    for (int i = 0; i < djinn_rt.pool.thread_count; i++)
    {
#ifdef _WIN32
        WakeAllConditionVariable(&djinn_rt.pool.queue.cond);
#else
        pthread_cond_broadcast(&djinn_rt.pool.queue.cond);
#endif
    }

    for (int i = 0; i < djinn_rt.pool.thread_count; i++)
    {
#ifdef _WIN32
        WaitForSingleObject(djinn_rt.pool.threads[i], 5000);
        CloseHandle(djinn_rt.pool.threads[i]);
#else
        pthread_join(djinn_rt.pool.threads[i], NULL);
#endif
    }
    free(djinn_rt.pool.threads);
    djinn_rt.pool.threads = NULL;

#ifdef _WIN32
    WaitForSingleObject(djinn_rt.file_io_thread, 5000);
    CloseHandle(djinn_rt.file_io_thread);
#else
    pthread_join(djinn_rt.file_io_thread, NULL);
#endif

#ifdef _WIN32
    PostQueuedCompletionStatus(djinn_rt.iocp, 0, 0, NULL);
    WaitForSingleObject(djinn_rt.socket_poller_thread, 5000);
    CloseHandle(djinn_rt.socket_poller_thread);
    CloseHandle(djinn_rt.iocp);
#else
    pthread_join(djinn_rt.socket_poller_thread, NULL);
    close(djinn_rt.epoll_fd);
#endif

    djinn_destroy_queue(&djinn_rt.ready_queue);
    djinn_destroy_queue(&djinn_rt.pool.queue);

#ifdef _WIN32
    EnterCriticalSection(&djinn_rt.file_io_mutex);
#else
    pthread_mutex_lock(&djinn_rt.file_io_mutex);
#endif
    djinn_io_request_t* req = djinn_rt.io_pending;
    while (req)
    {
        djinn_io_request_t* next = req->next;
        free(req);
        req = next;
    }
    djinn_rt.io_pending = NULL;
#ifdef _WIN32
    LeaveCriticalSection(&djinn_rt.file_io_mutex);
    DeleteCriticalSection(&djinn_rt.file_io_mutex);
    DeleteCriticalSection(&djinn_rt.socket_mutex);
#else
    pthread_mutex_unlock(&djinn_rt.file_io_mutex);
    pthread_mutex_destroy(&djinn_rt.file_io_mutex);
    pthread_mutex_destroy(&djinn_rt.socket_mutex);
#endif

#ifdef _WIN32
    WSACleanup();
#endif

    DJINN_TRACE("runtime shut down");
    logger_destroy_all();
    logger = NULL;
}

void __djinn_spawn(void* coro_handle)
{
    DJINN_ASSERT(coro_handle, "coro handle is NULL");
    djinn_enqueue_task(&djinn_rt.ready_queue, coro_handle);
}

static void process_completed_coro(void* handle, void* main_handle)
{
    djinn_remove_from_waiting(handle);
    void* parent = pop_continuation(handle);
    if (parent)
    {
        djinn_remove_from_waiting(parent);
        djinn_enqueue_task(&djinn_rt.ready_queue, parent);
    }
    else if (handle != main_handle)
    {
        __djinn_coro_destroy(handle);
    }
}

static void drain_ready_queue(void* main_handle)
{
    djinn_task_t* remaining;
    while ((remaining = djinn_dequeue_task(&djinn_rt.ready_queue)) != NULL)
    {
        if (!__djinn_coro_done(remaining->handle))
        {
            __djinn_coro_resume(remaining->handle);
            if (!__djinn_coro_done(remaining->handle))
            {
                if (!djinn_is_waiting(remaining->handle))
                {
                    djinn_enqueue_task(&djinn_rt.ready_queue, remaining->handle);
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

    if (!djinn_is_in_waiting_set(main_handle))
    {
        djinn_enqueue_task(&djinn_rt.ready_queue, main_handle);
    }

    while (djinn_rt.running)
    {
        djinn_task_t* task = djinn_dequeue_task(&djinn_rt.ready_queue);
        if (task)
        {
            if (!__djinn_coro_done(task->handle))
            {
                __djinn_coro_resume(task->handle);
                if (!__djinn_coro_done(task->handle))
                {
                    if (djinn_is_waiting(task->handle))
                    {
                        // Waiting for I/O or child — do NOT re-enqueue
                    }
                    else
                    {
                        // Cooperative yield — re-enqueue immediately
                        djinn_enqueue_task(&djinn_rt.ready_queue, task->handle);
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

        if (!djinn_rt.ready_queue.head)
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
