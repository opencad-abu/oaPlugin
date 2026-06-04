// *****************************************************************************
// oaAiviDMTurboServerMain.cpp — Server entry point
//
// Usage: oaAiviDMTurboServer <port> <libPath>
// *****************************************************************************

#include "oaDMTurbo.h"

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <sys/types.h>
#include <sys/stat.h>

namespace oaDMTurbo {
    int runServer(int port, const char* libPath);
}

static void signalHandler(int sig) {
    fprintf(stderr, "oaAiviDMTurboServer: received signal %d\n", sig);
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <libPath>\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    const char* libPath = argv[2];
    
    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGPIPE, SIG_IGN);
    
    // Ensure libPath exists
    mkdir(libPath, 0755);
    
    // Run server
    return oaDMTurbo::runServer(port, libPath);
}
