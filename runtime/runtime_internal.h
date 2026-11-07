#pragma once
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "djinn_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <stdint.h>
#include "logger.h"

#ifdef _WIN32
#include <io.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
#else
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

// #define DJINN_ENABLE_TRACE
#ifdef DJINN_ENABLE_TRACE
#define DJINN_TRACE(message, ...) do {                         DJINN_ASSERT(logger, "Logger not initlizatied properly!"); logger_trace(logger, message, ##__VA_ARGS__);              } while(0)
#else
#define DJINN_TRACE(message, ...) do {} while (0)
#endif

extern Logger* logger;
extern djinn_runtime_t djinn_rt;

#ifdef _WIN32
extern LPFN_ACCEPTEX djinn_pfn_accept_ex;
extern LPFN_CONNECTEX djinn_pfn_connect_ex;
#define DJINN_THREAD_FN(name) DWORD WINAPI name(LPVOID arg)
#else
#define DJINN_THREAD_FN(name) void* name(void* arg)
#endif

DJINN_THREAD_FN(djinn_file_io_thread_func);
DJINN_THREAD_FN(djinn_socket_poller_thread_func);
DJINN_THREAD_FN(djinn_worker_thread);

void djinn_init_queue(djinn_task_queue_t* q);
void djinn_destroy_queue(djinn_task_queue_t* q);
void djinn_enqueue_task(djinn_task_queue_t* q, void* handle);
djinn_task_t* djinn_dequeue_task(djinn_task_queue_t* q);
djinn_task_t* djinn_dequeue_task_blocking(djinn_task_queue_t* q, volatile int* shutdown);
int djinn_is_waiting(void* handle);
int djinn_is_in_waiting_set(void* handle);
void djinn_remove_from_waiting(void* handle);
