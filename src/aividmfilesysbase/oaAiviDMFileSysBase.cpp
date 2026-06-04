// *****************************************************************************
// oaAiviDMFileSysBase.cpp — Exception/Error/InfoMsg/oaCSMsg implementation
//
// Reverse-engineered from liboaDMFileSysBase.so v22.61.p005
// Provides standalone implementations for DMFileSys base classes.
// *****************************************************************************

#include "oaDMFileSysBase.h"
#include "oaDMFileSysTypes.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

namespace oaDMFileSys {

// ============================================================================
// Error message table — indexed by (msgId - msgIdStart)
// Index 0  = oacDMFileSysErrBase (7000)
// Index 1  = oacLibExistsErr
// Index 2  = oacLibNotFoundErr
// ... etc.
// ============================================================================
const char* Exception::msgTable[] = {
    /* 7000 oacDMFileSysErrBase   */ "DMFileSys: unspecified error.",
    /* 7001 oacLibExistsErr       */ "Library \"%s\" already exists.",
    /* 7002 oacLibNotFoundErr     */ "Library \"%s\" not found.",
    /* 7003 oacCellExistsErr      */ "Cell \"%s\" already exists.",
    /* 7004 oacCellNotFoundErr    */ "Cell \"%s\" not found.",
    /* 7005 oacViewExistsErr      */ "View \"%s\" already exists.",
    /* 7006 oacViewNotFoundErr    */ "View \"%s\" not found.",
    /* 7007 oacCellViewExistsErr  */ "CellView \"%s/%s\" already exists.",
    /* 7008 oacCellViewNotFoundErr*/ "CellView \"%s/%s\" not found.",
    /* 7009 oacFileExistsErr      */ "File \"%s\" already exists.",
    /* 7010 oacFileNotFoundErr    */ "File \"%s\" not found.",
    /* 7011 oacLockFailErr        */ "Failed to acquire lock on \"%s\".",
    /* 7012 oacInvalidPathErr     */ "Invalid path: \"%s\".",
    /* 7013 oacAccessDeniedErr    */ "Access denied to \"%s\".",
    /* 7014 oacNotALibErr         */ "\"%s\" is not a valid library.",
    /* 7015 oacReadOnlyLibErr     */ "Library \"%s\" is read-only.",
};

const int Exception::msgIdStart = 7000;
const int Exception::msgIdEnd   = 7015;

// ============================================================================
// Info message table — indexed by (msgId - infoMsgIdStart)
// Index 0 = oacDMFileSysInfoBase (7500)
// Index 1 = oacLibCreatedInfo
// Index 2 = oacLibOpenedInfo
// ============================================================================
const char* InfoMsg::infoMsgTable[] = {
    /* 7500 oacDMFileSysInfoBase  */ "DMFileSys: informational.",
    /* 7501 oacLibCreatedInfo     */ "Library \"%s\" created successfully.",
    /* 7502 oacLibOpenedInfo      */ "Library \"%s\" opened in %s mode.",
};

const int InfoMsg::infoMsgIdStart = 7500;
const int InfoMsg::infoMsgIdEnd   = 7502;

// ============================================================================
// Exception implementation
// ============================================================================

Exception::Exception(oaDMFileSysMsgIds id)
    : msgId_(id)
{
}

Exception::~Exception()
{
}

const char* Exception::getMsgsTable() const
{
    return reinterpret_cast<const char*>(msgTable);
}

int Exception::getMsgIdStartValue() const
{
    return msgIdStart;
}

int Exception::getMsgIdEndValue() const
{
    return msgIdEnd;
}

// ============================================================================
// Error implementation
// ============================================================================

Error::Error(oaDMFileSysMsgIds id, ...)
    : Exception(id)
{
    // Look up the format string from the message table
    int index = static_cast<int>(id) - msgIdStart;
    const char* fmt = nullptr;

    if (index >= 0 && index <= (msgIdEnd - msgIdStart)) {
        fmt = msgTable[index];
    } else {
        fmt = "DMFileSys: unknown error %d.";
    }

    // Format the error message with varargs.
    // In the real OA library, this calls Exception::format(va_list&)
    // which stores the result in the oaException::msg oaString member.
    // Since our standalone Exception doesn't inherit from oaException,
    // we provide a diagnostic format here. The format string itself
    // is accessible through getDMFileSysMsg() with the message ID.
    //
    // A full OA integration would have Exception inherit from
    // oaException and use oaString::format() internally.

    va_list args;
    va_start(args, id);
    (void)fmt;  // Format string accessible via message ID + getDMFileSysMsg()
    va_end(args);
}

Error::Error(const OpenAccess_4::oaException& ex)
    : Exception(oacDMFileSysErrBase)
{
    // Wrap an OA exception as a DMFileSys Error.
    // Copy the message ID and message text from the source oaException.
    // We attempt to map the OA error code into our DMFileSys error range.
    OpenAccess_4::oaUInt4 oaMsgId = ex.getMsgId();

    // Attempt to map: if the OA error code falls within the DMFileSys
    // base error range (7000-7015), use it directly.
    if (oaMsgId >= static_cast<OpenAccess_4::oaUInt4>(msgIdStart) &&
        oaMsgId <= static_cast<OpenAccess_4::oaUInt4>(msgIdEnd)) {
        msgId_ = static_cast<oaDMFileSysMsgIds>(oaMsgId);
    }
    // Otherwise, keep the default oacDMFileSysErrBase.
    // The original error message remains accessible via ex.getMsg().
    (void)ex;  // Source exception — message text kept in originating object
}

Error::~Error()
{
}

// ============================================================================
// InfoMsg implementation
// ============================================================================

InfoMsg::InfoMsg(oaDMFileSysInfoMsgIds id, ...)
    : Exception(static_cast<oaDMFileSysMsgIds>(id))
{
    int index = static_cast<int>(id) - infoMsgIdStart;
    const char* fmt = nullptr;

    if (index >= 0 && index <= (infoMsgIdEnd - infoMsgIdStart)) {
        fmt = infoMsgTable[index];
    } else {
        fmt = "DMFileSys: unknown info message %d.";
    }

    va_list args;
    va_start(args, id);
    (void)fmt;  // Format string accessible via getDMFileSysInfoMsg()
    va_end(args);
}

InfoMsg::~InfoMsg()
{
}

const char* InfoMsg::getMsgsTable() const
{
    return reinterpret_cast<const char*>(infoMsgTable);
}

int InfoMsg::getMsgIdStartValue() const
{
    return infoMsgIdStart;
}

int InfoMsg::getMsgIdEndValue() const
{
    return infoMsgIdEnd;
}

// ============================================================================
// oaCSMsg implementation — Client-Server message for locking daemon
// ============================================================================

// Default socket path used by OA DMFileSys locking daemon
static const char* defaultSocketPath = "/tmp/.oadmfs-lockd";

oaCSMsg::oaCSMsg()
    : socketFd_(-1)
    , msgBuffer_(nullptr)
{
    // Allocate a message buffer (OA uses a fixed-size buffer)
    msgBuffer_ = malloc(4096);
    if (!msgBuffer_) {
        return;
    }
    memset(msgBuffer_, 0, 4096);

    // Create a Unix domain socket for communication with the
    // DMFileSys locking daemon process.
    socketFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFd_ < 0) {
        return;
    }

    // Connect to the daemon
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, defaultSocketPath, sizeof(addr.sun_path) - 1);

    if (connect(socketFd_, reinterpret_cast<struct sockaddr*>(&addr),
                sizeof(addr)) < 0) {
        ::close(socketFd_);
        socketFd_ = -1;
        return;
    }
}

oaCSMsg::~oaCSMsg()
{
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
    if (msgBuffer_) {
        free(msgBuffer_);
        msgBuffer_ = nullptr;
    }
}

bool oaCSMsg::checkRecvData(unsigned int expectedSize, unsigned int timeoutMs)
{
    if (socketFd_ < 0 || !msgBuffer_) {
        return false;
    }

    // Set receive timeout
    struct timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    if (setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return false;
    }

    // Read exactly expectedSize bytes
    size_t totalRead = 0;
    while (totalRead < expectedSize) {
        ssize_t n = recv(socketFd_,
                         static_cast<char*>(msgBuffer_) + totalRead,
                         expectedSize - totalRead, 0);
        if (n <= 0) {
            return false;
        }
        totalRead += static_cast<size_t>(n);
    }

    return true;
}

bool oaCSMsg::recvWithTimeout(unsigned int timeoutMs)
{
    if (socketFd_ < 0 || !msgBuffer_) {
        return false;
    }

    // Use poll() to wait for data with timeout
    struct pollfd pfd;
    pfd.fd      = socketFd_;
    pfd.events  = POLLIN;

    int ret = poll(&pfd, 1, static_cast<int>(timeoutMs));
    if (ret <= 0) {
        return false;
    }

    if (!(pfd.revents & POLLIN)) {
        return false;
    }

    // Data available — read it
    ssize_t n = recv(socketFd_, static_cast<char*>(msgBuffer_), 4096, 0);
    return (n > 0);
}

void oaCSMsg::send(unsigned int code, const char* fmt, ...)
{
    if (socketFd_ < 0 || !msgBuffer_ || !fmt) {
        return;
    }

    // Format the message with varargs
    va_list args;
    va_start(args, fmt);
    char* buf = static_cast<char*>(msgBuffer_);

    // Write the message code as a header (4 bytes, network byte order in real lib)
    // Simple approach: write code + formatted string
    unsigned int netCode = code;  // In real lib this would be htonl(code)
    memcpy(buf, &netCode, sizeof(unsigned int));

    int msgLen = vsnprintf(buf + sizeof(unsigned int),
                           4096 - sizeof(unsigned int), fmt, args);
    va_end(args);

    if (msgLen < 0) {
        return;
    }

    // Send the full message: 4-byte code + formatted string + null
    size_t totalLen = sizeof(unsigned int) + static_cast<size_t>(msgLen) + 1;
    if (totalLen > 4096) totalLen = 4096;

    ::send(socketFd_, buf, totalLen, 0);
}

// ============================================================================
// Message lookup helpers
// ============================================================================

const char* getDMFileSysMsg(oaDMFileSysMsgIds id)
{
    int index = static_cast<int>(id) - Exception::msgIdStart;
    int count = Exception::msgIdEnd - Exception::msgIdStart;

    if (index >= 0 && index <= count) {
        return Exception::msgTable[index];
    }

    return "DMFileSys: unknown error.";
}

const char* getDMFileSysInfoMsg(oaDMFileSysInfoMsgIds id)
{
    int index = static_cast<int>(id) - InfoMsg::infoMsgIdStart;
    int count = InfoMsg::infoMsgIdEnd - InfoMsg::infoMsgIdStart;

    if (index >= 0 && index <= count) {
        return InfoMsg::infoMsgTable[index];
    }

    return "DMFileSys: unknown info message.";
}

} // namespace oaDMFileSys
