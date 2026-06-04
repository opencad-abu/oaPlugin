// *****************************************************************************
// oaDMFileSysBase.h — Exception/Message classes (replaces liboaDMFileSysBase.so)
//
// Reverse-engineered from liboaDMFileSysBase.so v22.61.p005
// *****************************************************************************

#ifndef OADMFILESYS_BASE_H
#define OADMFILESYS_BASE_H

#include "oaDMFileSysTypes.h"
#include <oa/oaException.h>
#include <oa/oaException.inl>
#include <cstdarg>
#include <exception>

// These are the base classes that OA uses internally.
// We provide stub wrappers that delegate to the real OA headers
// when available, or provide standalone implementations.

namespace oaDMFileSys {

// ============================================================================
// Exception — base DMFileSys exception
// ============================================================================
class Exception {
public:
    explicit Exception(oaDMFileSysMsgIds id);
    virtual ~Exception();
    
    virtual const char* getMsgsTable() const;
    virtual int getMsgIdStartValue() const;
    virtual int getMsgIdEndValue() const;
    
    oaDMFileSysMsgIds getMsgId() const { return msgId_; }
    
    oaDMFileSysMsgIds msgId_;
    static const char* msgTable[];
    static const int msgIdStart;
    static const int msgIdEnd;
};

// ============================================================================
// Error — error with format string
// ============================================================================
class Error : public Exception {
public:
    Error(oaDMFileSysMsgIds id, ...);
    Error(const OpenAccess_4::oaException& ex);
    virtual ~Error();
};

// ============================================================================
// InfoMsg — informational message
// ============================================================================
class InfoMsg : public Exception {
public:
    InfoMsg(oaDMFileSysInfoMsgIds id, ...);
    virtual ~InfoMsg();
    
    virtual const char* getMsgsTable() const override;
    virtual int getMsgIdStartValue() const override;
    virtual int getMsgIdEndValue() const override;
    
    static const char* infoMsgTable[];
    static const int infoMsgIdStart;
    static const int infoMsgIdEnd;
};

// ============================================================================
// oaCSMsg — Client-Server message (for locking daemon communication)
// ============================================================================
class oaCSMsg {
public:
    oaCSMsg();
    ~oaCSMsg();
    
    bool checkRecvData(unsigned int expectedSize, unsigned int timeoutMs);
    bool recvWithTimeout(unsigned int timeoutMs);
    void send(unsigned int code, const char* fmt, ...);
    
private:
    int socketFd_;
    void* msgBuffer_;
};

// ============================================================================
// Helper: format message string
// ============================================================================
const char* getDMFileSysMsg(oaDMFileSysMsgIds id);
const char* getDMFileSysInfoMsg(oaDMFileSysInfoMsgIds id);

} // namespace oaDMFileSys

#endif // OADMFILESYS_BASE_H
