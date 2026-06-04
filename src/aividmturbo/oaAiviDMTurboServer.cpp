// *****************************************************************************
// oaAiviDMTurboServer.cpp — LibServer + LibServerMgr implementations
// *****************************************************************************

#include "oaDMTurbo.h"


namespace oaDMTurbo {

// ============================================================================
// LibServer
// ============================================================================

void LibServer::init(const char* path, unsigned int port)
{
    // Stub: initialise server with path and port
    active_   = false;
    channel_  = nullptr;
}

void LibServer::requestStop()
{
    active_ = false;
}

bool LibServer::isActive()
{
    return active_;
}

unsigned int LibServer::getOpenLibCount()
{
    return 0;
}

void LibServer::serveLib(const char* libName, const char* libPath)
{
    // Stub: serve a library
}

unsigned long LibServer::addRef()
{
    return ++refCount_;
}

unsigned long LibServer::release()
{
    if (refCount_ > 0) {
        --refCount_;
    }
    if (refCount_ == 0) {
        delete this;
        return 0;
    }
    return refCount_;
}

unsigned long LibServer::getRefCount()
{
    return refCount_;
}

long LibServer::queryInterface(const oaCommon::Guid& id, void** iPtr)
{
    if (iPtr == nullptr) {
        return oaCommon::IBase::cInvalidArg;
    }
    *iPtr = nullptr;
    return oaCommon::IBase::cNoInterface;
}

/* static */
void LibServer::get(const std::string& lib, ServerInfo& info,
                    const std::string& path, unsigned int flags)
{
    // Stub: return default server info
    info.reset(lib);
}


// ============================================================================
// LibServerMgr
// ============================================================================

LibServerMgr::LibServerMgr()
    : refCount_(0)
{
}

LibServerMgr::~LibServerMgr()
{
    // Stub: cleanup all servers
    for (auto& pair : servers_) {
        pair.second->release();
    }
    servers_.clear();
}

/* static */
LibServerMgr& LibServerMgr::get()
{
    static LibServerMgr instance;
    return instance;
}

void LibServerMgr::start(unsigned int maxServers, const char* logPath)
{
    // Stub: start with default port
}

void LibServerMgr::start(unsigned int maxServers, unsigned int port, const char* logPath)
{
    // Stub: start with specified port
}

void LibServerMgr::find(const char* libName, unsigned int flags, IDMLibServer*& result)
{
    result = nullptr;
    // Stub: no server found
}

unsigned long LibServerMgr::addRef()
{
    return ++refCount_;
}

unsigned long LibServerMgr::release()
{
    if (refCount_ > 0) {
        --refCount_;
    }
    return refCount_;
}

unsigned long LibServerMgr::getRefCount()
{
    return refCount_;
}

long LibServerMgr::queryInterface(const oaCommon::Guid& id, void** iPtr)
{
    if (iPtr == nullptr) {
        return oaCommon::IBase::cInvalidArg;
    }
    *iPtr = nullptr;
    return oaCommon::IBase::cNoInterface;
}

} // namespace oaDMTurbo
