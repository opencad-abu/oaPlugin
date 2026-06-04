// *****************************************************************************
// oaAiviDMTurboChannel.cpp — TCP socket IChannel implementation
//
// Implements the communication channel between PlugIn (client) and
// LibServer (server process) using TCP sockets.
// *****************************************************************************

#include "oaDMTurbo.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <poll.h>

namespace oaDMTurbo {

// ============================================================================
// TcpChannel — TCP socket IChannel implementation
// ============================================================================

class TcpChannel : public IChannel {
public:
    TcpChannel() : fd_(-1), port_(0) {}
    ~TcpChannel() override { close_(); }

    // Connect to server at host:port (client side)
    bool connect(const char* host, int port, int timeoutMs = 5000) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        // Resolve host
        struct hostent* he = gethostbyname(host);
        if (!he) {
            // Try as IP address
            addr.sin_addr.s_addr = inet_addr(host);
            if (addr.sin_addr.s_addr == INADDR_NONE) {
                return false;
            }
        } else {
            memcpy(&addr.sin_addr, he->h_addr, he->h_length);
        }

        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;

        // Set non-blocking for connect with timeout
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

        int ret = ::connect(fd_, (struct sockaddr*)&addr, sizeof(addr));
        if (ret < 0 && errno != EINPROGRESS) {
            close_();
            return false;
        }

        if (ret < 0) {
            // Wait for connection with timeout
            struct pollfd pfd;
            pfd.fd = fd_;
            pfd.events = POLLOUT;
            ret = poll(&pfd, 1, timeoutMs);
            if (ret <= 0) {
                close_();
                return false;
            }
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err) {
                close_();
                return false;
            }
        }

        // Restore blocking mode
        fcntl(fd_, F_SETFL, flags);

        // Enable TCP_NODELAY for low latency
        int one = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        port_ = port;
        return true;
    }

    // Accept a connection (server side)
    bool accept(int listenFd) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        fd_ = ::accept(listenFd, (struct sockaddr*)&addr, &len);
        if (fd_ < 0) return false;

        int one = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        port_ = ntohs(addr.sin_port);
        return true;
    }

    int getPortNumber() const override { return port_; }

    // Send exactly len bytes
    bool send(const char* data, int len) override {
        if (fd_ < 0 || !data || len <= 0) return false;

        // Send 4-byte length header first
        int header = htonl(len);
        if (!sendAll((const char*)&header, 4)) return false;

        // Send payload
        return sendAll(data, len);
    }

    // Receive a message (blocking). Returns bytes received or -1 on error.
    int recv(char* buf, int maxLen) override {
        if (fd_ < 0 || !buf || maxLen <= 0) return -1;

        // Read 4-byte length header
        int header = 0;
        if (!recvAll((char*)&header, 4)) return -1;
        int len = ntohl(header);
        if (len <= 0 || len > maxLen) return -1;

        // Read payload
        if (!recvAll(buf, len)) return -1;
        return len;
    }

    int getFd() const { return fd_; }
    bool isValid() const { return fd_ >= 0; }

private:
    int fd_;
    int port_;

    void close_() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool sendAll(const char* data, int len) {
        int sent = 0;
        while (sent < len) {
            int n = ::send(fd_, data + sent, len - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += n;
        }
        return true;
    }

    bool recvAll(char* buf, int len) {
        int got = 0;
        while (got < len) {
            int n = ::recv(fd_, buf + got, len - got, 0);
            if (n <= 0) return false;
            got += n;
        }
        return true;
    }
};


// ============================================================================
// TcpListener — TCP server listener
// ============================================================================

class TcpListener {
public:
    TcpListener() : fd_(-1), port_(0) {}
    ~TcpListener() {
        if (fd_ >= 0) ::close(fd_);
    }

    // Bind to port (0 = auto-assign), returns actual port or -1
    int bind(int port = 0) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return -1;

        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        if (::bind(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return -1;
        }

        if (listen(fd_, 5) < 0) {
            ::close(fd_);
            fd_ = -1;
            return -1;
        }

        // Get assigned port
        struct sockaddr_in bound;
        socklen_t len = sizeof(bound);
        getsockname(fd_, (struct sockaddr*)&bound, &len);
        port_ = ntohs(bound.sin_port);
        return port_;
    }

    int getPort() const { return port_; }
    int getFd() const { return fd_; }

    // Accept a new connection, returns TcpChannel* (caller owns)
    TcpChannel* accept() {
        auto* ch = new TcpChannel();
        if (!ch->accept(fd_)) {
            delete ch;
            return nullptr;
        }
        return ch;
    }

private:
    int fd_;
    int port_;
};


// ============================================================================
// IChannel factory methods
// ============================================================================

/* static */
IChannel* IChannel::connectToServer(const char* host, int port) {
    auto* ch = new TcpChannel();
    if (!ch->connect(host, port)) {
        delete ch;
        return nullptr;
    }
    return ch;
}

/* static */
int IChannel::startServer(int port) {
    auto* listener = new TcpListener();
    int actualPort = listener->bind(port);
    if (actualPort < 0) {
        delete listener;
        return -1;
    }
    // Store listener for later accept — caller takes ownership
    // Return port for communication
    // Note: caller must manage listener lifecycle
    delete listener;  // simplified for now
    return actualPort;
}

} // namespace oaDMTurbo
