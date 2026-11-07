#include "runtime_internal.h"

// ── Child processes with captured output ──

#ifndef _WIN32
#include <spawn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
extern char** environ;
#endif

typedef struct djinn_pipe_buffer
{
    char* data;
    int32_t length;
    int32_t capacity;
} djinn_pipe_buffer_t;

struct djinn_process
{
#ifdef _WIN32
    HANDLE process;
    HANDLE stdin_write;
    HANDLE stdout_read;
    HANDLE stderr_read;
#else
    pid_t pid;
    int stdin_write;
    int stdout_read;
    int stderr_read;
#endif
    djinn_thread_t* stdin_thread;
    djinn_thread_t* stdout_thread;
    djinn_thread_t* stderr_thread;
    const char* stdin_data;
    int32_t stdin_length;
    djinn_pipe_buffer_t out;
    djinn_pipe_buffer_t err;
    int32_t exit_code;
    int32_t finished;
    int32_t killed;
};

typedef struct djinn_pipe_reader
{
    struct djinn_process* process;
    djinn_pipe_buffer_t* buffer;
#ifdef _WIN32
    HANDLE* handle;
#else
    int* handle;
#endif
} djinn_pipe_reader_t;

static void djinn_pipe_buffer_append(djinn_pipe_buffer_t* buffer, const char* data, int32_t length)
{
    if (buffer->length + length + 1 > buffer->capacity)
    {
        int32_t capacity = buffer->capacity > 0 ? buffer->capacity : 4096;
        while (capacity < buffer->length + length + 1) capacity *= 2;
        buffer->data = (char*)realloc(buffer->data, (size_t)capacity);
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, data, (size_t)length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

static void djinn_pipe_drain(void* argument)
{
    djinn_pipe_reader_t* reader = (djinn_pipe_reader_t*)argument;
    char chunk[4096];
    for (;;)
    {
#ifdef _WIN32
        DWORD count = 0;
        if (!ReadFile(*reader->handle, chunk, sizeof chunk, &count, NULL) || count == 0) break;
#else
        ssize_t count = read(*reader->handle, chunk, sizeof chunk);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
#endif
        djinn_pipe_buffer_append(reader->buffer, chunk, (int32_t)count);
    }
#ifdef _WIN32
    CloseHandle(*reader->handle);
    *reader->handle = NULL;
#else
    close(*reader->handle);
    *reader->handle = -1;
#endif
    free(reader);
}

static void djinn_pipe_feed(void* argument)
{
    djinn_pipe_reader_t* reader = (djinn_pipe_reader_t*)argument;
    struct djinn_process* process = reader->process;
    int32_t written = 0;
    while (written < process->stdin_length)
    {
#ifdef _WIN32
        DWORD count = 0;
        if (!WriteFile(*reader->handle, process->stdin_data + written, (DWORD)(process->stdin_length - written), &count, NULL) || count == 0) break;
#else
        ssize_t count = write(*reader->handle, process->stdin_data + written, (size_t)(process->stdin_length - written));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
#endif
        written += (int32_t)count;
    }
#ifdef _WIN32
    CloseHandle(*reader->handle);
    *reader->handle = NULL;
#else
    close(*reader->handle);
    *reader->handle = -1;
#endif
    free(reader);
}

#ifdef _WIN32
#define DJINN_PIPE_HANDLE HANDLE
#else
#define DJINN_PIPE_HANDLE int
#endif

static djinn_thread_t* djinn_process_pump(struct djinn_process* process, void (*body)(void*), djinn_pipe_buffer_t* buffer,
                                          DJINN_PIPE_HANDLE* handle)
{
    djinn_pipe_reader_t* reader = (djinn_pipe_reader_t*)calloc(1, sizeof *reader);
    reader->process = process;
    reader->buffer = buffer;
    reader->handle = handle;
    djinn_thread_t* thread = __djinn_thread_create(body, reader);
    __djinn_thread_start(thread);
    return thread;
}

djinn_process_t* __djinn_process_start(const char* command_line, const char* stdin_data, int32_t stdin_length)
{
    djinn_process_t* process = (djinn_process_t*)calloc(1, sizeof *process);
    process->stdin_data = stdin_data;
    process->stdin_length = stdin_data != NULL ? stdin_length : 0;

#ifdef _WIN32
    SECURITY_ATTRIBUTES inheritable = {sizeof inheritable, NULL, TRUE};
    HANDLE stdin_read = NULL, stdout_write = NULL, stderr_write = NULL;
    if (!CreatePipe(&stdin_read, &process->stdin_write, &inheritable, 0) ||
        !CreatePipe(&process->stdout_read, &stdout_write, &inheritable, 0) ||
        !CreatePipe(&process->stderr_read, &stderr_write, &inheritable, 0))
    {
        free(process);
        return NULL;
    }
    SetHandleInformation(process->stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(process->stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(process->stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;

    PROCESS_INFORMATION info;
    memset(&info, 0, sizeof info);
    char* mutable_line = _strdup(command_line);
    BOOL created = CreateProcessA(NULL, mutable_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &info);
    free(mutable_line);
    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    CloseHandle(stderr_write);
    if (!created)
    {
        CloseHandle(process->stdin_write);
        CloseHandle(process->stdout_read);
        CloseHandle(process->stderr_read);
        free(process);
        return NULL;
    }
    CloseHandle(info.hThread);
    process->process = info.hProcess;
#else
    int in_pipe[2], out_pipe[2], err_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
    {
        free(process);
        return NULL;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in_pipe[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], 1);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], 2);
    int pipes[6] = {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]};
    for (int i = 0; i < 6; i++) posix_spawn_file_actions_addclose(&actions, pipes[i]);
    char* argv[] = {(char*)"/bin/sh", (char*)"-c", (char*)command_line, NULL};
    int status = posix_spawn(&process->pid, "/bin/sh", &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);
    if (status != 0)
    {
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(err_pipe[0]);
        free(process);
        return NULL;
    }
    process->stdin_write = in_pipe[1];
    process->stdout_read = out_pipe[0];
    process->stderr_read = err_pipe[0];
#endif

    process->stdout_thread = djinn_process_pump(process, djinn_pipe_drain, &process->out, &process->stdout_read);
    process->stderr_thread = djinn_process_pump(process, djinn_pipe_drain, &process->err, &process->stderr_read);
    process->stdin_thread = djinn_process_pump(process, djinn_pipe_feed, NULL, &process->stdin_write);
    return process;
}

int32_t __djinn_process_poll(djinn_process_t* process)
{
    if (process->finished) return 1;
#ifdef _WIN32
    if (WaitForSingleObject(process->process, 0) != WAIT_OBJECT_0) return 0;
    DWORD code = 0;
    GetExitCodeProcess(process->process, &code);
    process->exit_code = (int32_t)code;
    CloseHandle(process->process);
    process->process = NULL;
#else
    int status = 0;
    pid_t done = waitpid(process->pid, &status, WNOHANG);
    if (done == 0) return 0;
    if (done < 0) process->exit_code = -1;
    else if (WIFEXITED(status)) process->exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) process->exit_code = 128 + WTERMSIG(status);
#endif
    __djinn_thread_join(process->stdout_thread);
    __djinn_thread_join(process->stderr_thread);
    __djinn_thread_join(process->stdin_thread);
    process->finished = 1;
    return 1;
}

int32_t __djinn_process_wait(djinn_process_t* process, int32_t timeout_ms)
{
    int64_t deadline = __djinn_unix_timestamp_ms() + (timeout_ms < 0 ? 0 : timeout_ms);
    for (;;)
    {
        if (__djinn_process_poll(process)) return 1;
        if (timeout_ms >= 0 && __djinn_unix_timestamp_ms() >= deadline) return 0;
        __djinn_thread_sleep(1);
    }
}

int32_t __djinn_process_wait_any(djinn_process_t** processes, int32_t count, int32_t timeout_ms)
{
    int64_t deadline = __djinn_unix_timestamp_ms() + (timeout_ms < 0 ? 0 : timeout_ms);
    for (;;)
    {
        for (int32_t i = 0; i < count; i++)
            if (processes[i] != NULL && __djinn_process_poll(processes[i])) return i;
        if (timeout_ms >= 0 && __djinn_unix_timestamp_ms() >= deadline) return -1;
        __djinn_thread_sleep(1);
    }
}

void __djinn_process_kill(djinn_process_t* process)
{
    if (process->finished) return;
    process->killed = 1;
#ifdef _WIN32
    TerminateProcess(process->process, 1);
#else
    kill(process->pid, SIGKILL);
#endif
    __djinn_process_wait(process, -1);
}

int32_t __djinn_process_exit_code(djinn_process_t* process) { return process->exit_code; }
int32_t __djinn_process_killed(djinn_process_t* process) { return process->killed; }
const char* __djinn_process_stdout_data(djinn_process_t* process) { return process->out.data != NULL ? process->out.data : ""; }
int32_t __djinn_process_stdout_length(djinn_process_t* process) { return process->out.length; }
const char* __djinn_process_stderr_data(djinn_process_t* process) { return process->err.data != NULL ? process->err.data : ""; }
int32_t __djinn_process_stderr_length(djinn_process_t* process) { return process->err.length; }

void __djinn_process_free(djinn_process_t* process)
{
    if (process == NULL) return;
    if (!process->finished) __djinn_process_kill(process);
    free(process->out.data);
    free(process->err.data);
    free(process);
}

