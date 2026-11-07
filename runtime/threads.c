#include "runtime_internal.h"

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
    DJINN_TRACE("sleeping thread for %lld milliseconds", milliseconds );
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#endif
    DJINN_TRACE("sleep thread ended", milliseconds );
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
