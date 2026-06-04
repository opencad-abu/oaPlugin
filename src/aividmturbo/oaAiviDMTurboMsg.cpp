// *****************************************************************************
// oaAiviDMTurboMsg.cpp — CSMsg protocol implementation
//
// Defines the client-server message protocol for DMTurbo.
// Wire format: [4-byte length][payload]
// Payload:     [1-byte msgId][Stream data]
// *****************************************************************************

#include "oaDMTurbo.h"

#include <cstring>
#include <cstdio>

namespace oaDMTurbo {

// ============================================================================
// CSMsg — Client-server message
// ============================================================================

CSMsg::CSMsg(IChannel* channel)
    : channel_(channel), id_(kMsgNone)
{
    stream_ = new Stream(4096);
}

CSMsg::~CSMsg() {
    delete stream_;
    
}

void CSMsg::set(CSMsgId id) {
    id_ = id;
    stream_->clear();
    // Write msgId as first byte of stream
    char c = static_cast<char>(id);
    *stream_ << c;
}

CSMsgId CSMsg::getId() const {
    return id_;
}

int CSMsg::getPortNumber() {
    return channel_ ? channel_->getPortNumber() : 0;
}

Stream& CSMsg::getStream() {
    return *stream_;
}

// Serialize: write stream to channel
bool CSMsg::send() {
    if (!channel_ || !stream_) return false;

    unsigned int sz = stream_->getSize();
    if (sz == 0) return false;

    char* buf = new char[sz];
    stream_->outTo(buf, sz);
    bool ok = channel_->send(buf, static_cast<int>(sz));
    delete[] buf;
    return ok;
}

// Deserialize: read from channel into stream
bool CSMsg::recv() {
    if (!channel_) return false;

    char buf[65536];  // max message size
    int n = channel_->recv(buf, sizeof(buf));
    if (n <= 0) return false;

    stream_->clear();
    stream_->reserve(n);
    stream_->setSize(n);
    memcpy(stream_->getBuf(), buf, n);

    // Read msgId
    char c = 0;
    stream_->setReadPos(0);
    *stream_ >> c;
    id_ = static_cast<CSMsgId>(c);

    return true;
}

// Request = set + send + recv (round-trip)
bool CSMsg::request(CSMsgId reqId) {
    set(reqId);
    if (!send()) return false;
    return recv();
}

// ============================================================================
// Helper: write string args into stream
// ============================================================================

void CSMsg::writeString(const std::string& s) {
    *stream_ << s;
}

void CSMsg::writeInt(unsigned int val) {
    *stream_ << val;
}

void CSMsg::writeLong(long val) {
    *stream_ << val;
}

void CSMsg::writeChar(char c) {
    *stream_ << c;
}

// ============================================================================
// Helper: read string args from stream
// ============================================================================

std::string CSMsg::readString() {
    std::string s;
    *stream_ >> s;
    return s;
}

unsigned int CSMsg::readInt() {
    unsigned int val = 0;
    *stream_ >> val;
    return val;
}

long CSMsg::readLong() {
    long val = 0;
    *stream_ >> val;
    return val;
}

char CSMsg::readChar() {
    char c = 0;
    *stream_ >> c;
    return c;
}

// ============================================================================
// MsgName — Message name registry for debugging
// ============================================================================

static const char* kMsgNames[] = {
    "None",
    "CreateLib", "OpenLib", "CloseLib", "LibExists",
    "CreateCell", "FindCell", "DestroyCell",
    "CreateView", "DestroyView",
    "CreateCellView", "FindCellView", "DestroyCellView",
    "CreateFile", "FindFile", "DestroyFile",
    "GetPath", "GetTempFile", "Exists",
    "Lock", "Unlock", "HasLockedFiles",
    "Save", "Load",
    "OK", "Error",
    "Ping", "Pong", "Shutdown",
};

const char* CSMsg::msgName(CSMsgId id) {
    int idx = static_cast<int>(id);
    int count = sizeof(kMsgNames) / sizeof(kMsgNames[0]);
    if (idx >= 0 && idx < count) return kMsgNames[idx];
    return "Unknown";
}

} // namespace oaDMTurbo
