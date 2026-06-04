// *****************************************************************************
// oaAiviDMTurboServerImpl.cpp — Server-side message handler implementation
//
// Receives CSMsg from PlugIn, operates on DMLib, returns results.
// *****************************************************************************

#include "oaDMTurbo.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace oaDMTurbo {

// ============================================================================
// ServerXml — Read/write server.xml for port/host discovery
// ============================================================================


// ============================================================================
// ServerState — Global server state
// ============================================================================

struct ServerState {
    DMLib* dbLib;
    StringTbl* strTbl;
    bool running;
    int listenFd;
    int clientFd;
    
    ServerState() : dbLib(nullptr), strTbl(nullptr), running(false),
                    listenFd(-1), clientFd(-1) {}
    
    ~ServerState() {
        delete dbLib;
        delete strTbl;
        if (listenFd >= 0) close(listenFd);
        if (clientFd >= 0) close(clientFd);
    }
};

// ============================================================================
// Message handlers
// ============================================================================

static void handleCreateLib(ServerState& state, CSMsg& msg) {
    std::string name = msg.readString();
    std::string path = msg.readString();
    
    if (state.dbLib) {
        delete state.dbLib;
    }
    
    state.dbLib = new DMLib(name, path);
    
    // Create directory structure
    std::string cmd = "mkdir -p " + path;
    system(cmd.c_str());
    
    // Write .oalib marker
    std::string oalibPath = path + "/.oalib";
    std::ofstream oalib(oalibPath);
    oalib << "AiviDMTurbo Library\n";
    oalib.close();
    
    msg.set(kMsgOK);
    msg.send();
}

static void handleOpenLib(ServerState& state, CSMsg& msg) {
    std::string name = msg.readString();
    std::string path = msg.readString();
    
    if (!state.dbLib) {
        state.dbLib = new DMLib(name, path);
    }
    
    msg.set(kMsgOK);
    msg.send();
}

static void handleCloseLib(ServerState& state, CSMsg& msg) {
    // Persist database to lib.xml
    if (state.dbLib) {
        std::string path;
        state.dbLib->getPath(path);
        std::string xmlPath = path + "/lib.xml";
        
        DMOut out(state.dbLib);
        Stream s(4096);
        out.doLib(s);
        out.doCells(s);
        out.doViews(s);
        out.doCellViews(s);
        out.doFiles(s);
        
        // Write stream to file
        std::ofstream f(xmlPath, std::ios::binary);
        if (f) {
            char* buf = new char[s.getSize()];
            s.outTo(buf, s.getSize());
            f.write(buf, s.getSize());
            delete[] buf;
            f.close();
        }
    }
    
    msg.set(kMsgOK);
    msg.send();
}

static void handleLibExists(ServerState& state, CSMsg& msg) {
    std::string path = msg.readString();
    
    std::string oalibPath = path + "/.oalib";
    bool exists = (access(oalibPath.c_str(), F_OK) == 0);
    
    msg.set(kMsgOK);
    msg.writeInt(exists ? 1 : 0);
    msg.send();
}

static void handleCreateCell(ServerState& state, CSMsg& msg) {
    std::string cellName = msg.readString();
    
    if (!state.dbLib) {
        msg.set(kMsgError);
        msg.writeString("Library not open");
        msg.send();
        return;
    }
    
    unsigned int cellId = state.strTbl->addString(cellName);
    state.dbLib->addCell(cellId);
    
    // Create cell directory
    std::string path;
    state.dbLib->getPath(path);
    std::string cellPath = path + "/" + cellName;
    std::string cmd = "mkdir -p " + cellPath;
    system(cmd.c_str());
    
    msg.set(kMsgOK);
    msg.writeInt(cellId);
    msg.send();
}

static void handleFindCell(ServerState& state, CSMsg& msg) {
    std::string cellName = msg.readString();
    
    if (!state.dbLib || !state.strTbl) {
        msg.set(kMsgError);
        msg.writeString("Library not open");
        msg.send();
        return;
    }
    
    unsigned int cellId = state.strTbl->findKey(cellName);
    bool found = (cellId != (unsigned int)-1);
    
    if (found) {
        const auto& cells = state.dbLib->getCells();
        found = (cells.find(cellId) != cells.end());
    }
    
    msg.set(kMsgOK);
    msg.writeInt(found ? 1 : 0);
    if (found) msg.writeInt(cellId);
    msg.send();
}

static void handleCreateCellView(ServerState& state, CSMsg& msg) {
    std::string cellName = msg.readString();
    std::string viewName = msg.readString();
    std::string vtName = msg.readString();
    
    if (!state.dbLib) {
        msg.set(kMsgError);
        msg.writeString("Library not open");
        msg.send();
        return;
    }
    
    unsigned int cellId = state.strTbl->addString(cellName);
    unsigned int viewId = state.strTbl->addString(viewName);
    unsigned int vtId = state.strTbl->addString(vtName);
    
    state.dbLib->addCellView(cellId, viewId, vtId);
    
    // Create cellView directory
    std::string path;
    state.dbLib->getPath(path);
    std::string cvPath = path + "/" + cellName + "/" + viewName;
    std::string cmd = "mkdir -p " + cvPath;
    system(cmd.c_str());
    
    // Write master.tag
    std::string tagPath = cvPath + "/master.tag";
    std::ofstream tag(tagPath);
    tag << "masterTag\n";
    tag.close();
    
    // Write primary file (empty for now)
    std::string primaryPath = cvPath + "/netlist.oa";
    std::ofstream primary(primaryPath, std::ios::binary);
    primary.close();
    
    msg.set(kMsgOK);
    msg.send();
}

static void handleFindCellView(ServerState& state, CSMsg& msg) {
    std::string cellName = msg.readString();
    std::string viewName = msg.readString();
    std::string vtName = msg.readString();
    
    if (!state.dbLib || !state.strTbl) {
        msg.set(kMsgError);
        msg.writeString("Library not open");
        msg.send();
        return;
    }
    
    unsigned int cellId = state.strTbl->findKey(cellName);
    unsigned int viewId = state.strTbl->findKey(viewName);
    unsigned int vtId = state.strTbl->findKey(vtName);
    
    bool found = (cellId != (unsigned int)-1 && 
                  viewId != (unsigned int)-1 && 
                  vtId != (unsigned int)-1);
    
    msg.set(kMsgOK);
    msg.writeInt(found ? 1 : 0);
    msg.send();
}

static void handleGetPath(ServerState& state, CSMsg& msg) {
    std::string cellName = msg.readString();
    std::string viewName = msg.readString();
    std::string fileName = msg.readString();
    
    if (!state.dbLib) {
        msg.set(kMsgError);
        msg.writeString("Library not open");
        msg.send();
        return;
    }
    
    std::string libPath;
    state.dbLib->getPath(libPath);
    
    std::string fullPath = libPath;
    if (!cellName.empty()) {
        fullPath += "/" + cellName;
        if (!viewName.empty()) {
            fullPath += "/" + viewName;
            if (!fileName.empty()) {
                fullPath += "/" + fileName;
            }
        }
    }
    
    msg.set(kMsgOK);
    msg.writeString(fullPath);
    msg.send();
}

static void handlePing(ServerState& state, CSMsg& msg) {
    msg.set(kMsgPong);
    msg.send();
}

static void handleShutdown(ServerState& state, CSMsg& msg) {
    msg.set(kMsgOK);
    msg.send();
    state.running = false;
}

// ============================================================================
// Message dispatcher
// ============================================================================

static void dispatchMessage(ServerState& state, CSMsg& msg) {
    CSMsgId id = msg.getId();
    
    switch (id) {
        case kMsgCreateLib:
            handleCreateLib(state, msg);
            break;
        case kMsgOpenLib:
            handleOpenLib(state, msg);
            break;
        case kMsgCloseLib:
            handleCloseLib(state, msg);
            break;
        case kMsgLibExists:
            handleLibExists(state, msg);
            break;
        case kMsgCreateCell:
            handleCreateCell(state, msg);
            break;
        case kMsgFindCell:
            handleFindCell(state, msg);
            break;
        case kMsgCreateCellView:
            handleCreateCellView(state, msg);
            break;
        case kMsgFindCellView:
            handleFindCellView(state, msg);
            break;
        case kMsgGetPath:
            handleGetPath(state, msg);
            break;
        case kMsgPing:
            handlePing(state, msg);
            break;
        case kMsgShutdown:
            handleShutdown(state, msg);
            break;
        default:
            msg.set(kMsgError);
            msg.writeString("Unknown message ID");
            msg.send();
            break;
    }
}

// ============================================================================
// Server main loop
// ============================================================================

int runServer(int port, const char* libPath) {
    ServerState state;
    state.strTbl = new StringTbl();
    state.running = true;
    
    // Create listening socket
    state.listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (state.listenFd < 0) {
        perror("socket");
        return 1;
    }
    
    int one = 1;
    setsockopt(state.listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    
    if (bind(state.listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(state.listenFd, 5) < 0) {
        perror("listen");
        return 1;
    }
    
    // Get actual port (in case port was 0)
    struct sockaddr_in bound;
    socklen_t len = sizeof(bound);
    getsockname(state.listenFd, (struct sockaddr*)&bound, &len);
    int actualPort = ntohs(bound.sin_port);
    
    // Write server.xml
    std::string xmlPath = std::string(libPath) + "/server.xml";
    ServerInfo serverInfo;
    serverInfo.host = "localhost";
    serverInfo.port = actualPort;
    serverInfo.pid = getpid();
    ServerXml serverXml(serverInfo);
    serverXml.write(xmlPath);
    
    fprintf(stderr, "oaAiviDMTurboServer: listening on port %d\n", actualPort);
    
    // Accept and handle connections
    while (state.running) {
        struct pollfd pfd;
        pfd.fd = state.listenFd;
        pfd.events = POLLIN;
        
        int ret = poll(&pfd, 1, 1000);  // 1 second timeout
        if (ret <= 0) continue;
        
        // Accept connection
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        state.clientFd = accept(state.listenFd, (struct sockaddr*)&clientAddr, &clientLen);
        if (state.clientFd < 0) continue;
        
        fprintf(stderr, "oaAiviDMTurboServer: client connected\n");
        
        // Handle messages from this client
        while (state.running) {
            pfd.fd = state.clientFd;
            pfd.events = POLLIN;
            
            ret = poll(&pfd, 1, 5000);  // 5 second timeout
            if (ret <= 0) break;
            
            // Create channel wrapper
            class FdChannel : public IChannel {
            public:
                FdChannel(int fd) : fd_(fd) {}
                int getPortNumber() const override { return 0; }
                
                bool send(const char* data, int len) override {
                    if (fd_ < 0 || !data || len <= 0) return false;
                    int header = htonl(len);
                    if (!sendAll((const char*)&header, 4)) return false;
                    return sendAll(data, len);
                }
                
                int recv(char* buf, int maxLen) override {
                    if (fd_ < 0 || !buf || maxLen <= 0) return -1;
                    int header = 0;
                    if (!recvAll((char*)&header, 4)) return -1;
                    int len = ntohl(header);
                    if (len <= 0 || len > maxLen) return -1;
                    if (!recvAll(buf, len)) return -1;
                    return len;
                }
                
            private:
                int fd_;
                
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
            
            FdChannel channel(state.clientFd);
            CSMsg msg(&channel);
            
            if (!msg.recv()) {
                break;  // Client disconnected
            }
            
            dispatchMessage(state, msg);
        }
        
        close(state.clientFd);
        state.clientFd = -1;
        fprintf(stderr, "oaAiviDMTurboServer: client disconnected\n");
    }
    
    // Cleanup
    unlink(xmlPath.c_str());
    fprintf(stderr, "oaAiviDMTurboServer: shutting down\n");
    
    return 0;
}

} // namespace oaDMTurbo
