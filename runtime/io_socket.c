#include "runtime_internal.h"



#ifdef _WIN32

#define REQ_FROM_OVERLAPPED(ovl) \
    ((djinn_io_request_t*)((char*)(ovl) - offsetof(djinn_io_request_t, overlapped)))

DWORD WINAPI djinn_socket_poller_thread_func(LPVOID arg)
{
    (void)arg;
    DWORD bytes_transferred;
    ULONG_PTR completion_key;
    OVERLAPPED* ovl;

    while (djinn_rt.running)
    {
        const BOOL ok = GetQueuedCompletionStatus(
            djinn_rt.iocp,
            &bytes_transferred,
            &completion_key,
            &ovl,
            100 // 100ms timeout
        );

        if (!djinn_rt.running) break;

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
                CreateIoCompletionPort((HANDLE)req->accepted_socket, djinn_rt.iocp, 0, 0);
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
            djinn_enqueue_task(&djinn_rt.ready_queue, req->waiting_coro);
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

void* djinn_socket_poller_thread_func(void* arg)
{
    (void)arg;
    struct epoll_event events[64];

    while (djinn_rt.running)
    {
        int n = epoll_wait(djinn_rt.epoll_fd, events, 64, 100);
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
                    epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_DEL, (int)req->socket, NULL);
                    break;
                }

            case DJINN_IO_CONNECT:
                {
                    // Check if connect succeeded
                    int err = 0;
                    socklen_t len = sizeof(err);
                    getsockopt((int)req->socket, SOL_SOCKET, SO_ERROR, &err, &len);
                    req->result = (err == 0) ? 0 : -1;
                    epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_DEL, (int)req->socket, NULL);
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
                    epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_DEL, (int)req->socket, NULL);
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
                    epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_DEL, (int)req->socket, NULL);
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
                    djinn_enqueue_task(&djinn_rt.ready_queue, req->waiting_coro);

                free(req);
            }
        }
    }

    return NULL;
}

#endif // _WIN32

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
    const HANDLE handle = CreateIoCompletionPort((HANDLE)socket_fd, djinn_rt.iocp, 0, 0);
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
    const BOOL ok = djinn_pfn_accept_ex(
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
    if (epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_ADD, (int)server_sock, &ev) < 0)
    {
        if (epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_MOD, (int)server_sock, &ev) < 0)
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
    const BOOL ok = djinn_pfn_connect_ex(
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
        djinn_enqueue_task(&djinn_rt.ready_queue, coro);
        free(req);
        return 0;
    }

    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLONESHOT;
    ev.data.ptr = req;
    if (epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_ADD, (int)socket_fd, &ev) < 0)
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
    if (epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_ADD, (int)socket_fd, &ev) < 0)
    {
        if (epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_MOD, (int)socket_fd, &ev) < 0)
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
    if (epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_ADD, (int)socket_fd, &ev) < 0)
    {
        if (epoll_ctl(djinn_rt.epoll_fd, EPOLL_CTL_MOD, (int)socket_fd, &ev) < 0)
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
