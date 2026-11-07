#include "runtime_internal.h"

#ifdef _WIN32
DWORD WINAPI djinn_file_io_thread_func(LPVOID arg)
{
#else
    void* djinn_file_io_thread_func(void* arg)
    {


#endif
    (void)arg;
    while (djinn_rt.running)
    {
#ifdef _WIN32
        EnterCriticalSection(&djinn_rt.file_io_mutex);
#else
        pthread_mutex_lock(&djinn_rt.file_io_mutex);
#endif

        djinn_io_request_t* req = djinn_rt.io_pending;
        if (req)
        {
            djinn_rt.io_pending = req->next;
        }

#ifdef _WIN32
        LeaveCriticalSection(&djinn_rt.file_io_mutex);
#else
        pthread_mutex_unlock(&djinn_rt.file_io_mutex);
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
                djinn_enqueue_task(&djinn_rt.ready_queue, req->waiting_coro);
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
    EnterCriticalSection(&djinn_rt.file_io_mutex);
#else
    pthread_mutex_lock(&djinn_rt.file_io_mutex);
#endif

    req->next = djinn_rt.io_pending;
    djinn_rt.io_pending = req;

#ifdef _WIN32
    LeaveCriticalSection(&djinn_rt.file_io_mutex);
#else
    pthread_mutex_unlock(&djinn_rt.file_io_mutex);
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
    EnterCriticalSection(&djinn_rt.file_io_mutex);
#else
    pthread_mutex_lock(&djinn_rt.file_io_mutex);
#endif

    req->next = djinn_rt.io_pending;
    djinn_rt.io_pending = req;

#ifdef _WIN32
    LeaveCriticalSection(&djinn_rt.file_io_mutex);
#else
    pthread_mutex_unlock(&djinn_rt.file_io_mutex);
#endif

    return 0;
}

