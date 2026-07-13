#include "MockCQGApplication.h"

#include <quickfix/FileStore.h>
#include <quickfix/ThreadedSocketAcceptor.h>
#include <quickfix/SessionSettings.h>

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    std::cout << "\n[MockCQG] Received signal " << signum
              << ", shutting down..." << std::endl;
    g_running = false;
}

void printBanner() {
    std::cout << R"(
  ________________________________________________________
  |         Mock CQG FIX 4.2 Server                      |
  |         For Testing & Conformance                    |
  |______________________________________________________|
  |  Protocol:   FIX 4.2                                 |
  |  Port:       5001                                    |
  |  SenderComp: CQG                                     |
  |  TargetComp: CLIENT                                  |
  |______________________________________________________|
)" << std::endl;
}

int main(int argc, char** argv) {
    // Default config path
    std::string configFile = "server.cfg";

    if (argc > 1) {
        configFile = argv[1];
    }

    printBanner(); 

    // Register signal handlers for clean shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        // Load QuickFIX settings
        FIX::SessionSettings settings(configFile);

        // Create the application
        MockCQG::MockCQGApplication application;

        // Message store (persists sequence numbers)
        FIX::FileStoreFactory storeFactory(settings);

        // Message log (print to console for easier debugging)
        FIX::ScreenLogFactory logFactory(settings);

        // Create the threaded acceptor to prevent blocking heartbeats
        FIX::ThreadedSocketAcceptor acceptor(
            application, storeFactory, settings, logFactory);

        // Start accepting connections
        acceptor.start();

        std::cout << "[MockCQG] Server is running. Waiting for connections..."
                  << std::endl;
        std::cout << "[MockCQG] Press Ctrl+C to shutdown." << std::endl;
        std::cout << std::endl;

        // Wait until signaled to stop
        while (g_running) {
            FIX::process_sleep(1);
        }

        // Clean shutdown
        std::cout << "[MockCQG] Stopping acceptor..." << std::endl;
        acceptor.stop();
        std::cout << "[MockCQG] Server stopped." << std::endl;

        return 0;

    } catch (const FIX::ConfigError& e) {
        std::cerr << "[MockCQG] Configuration error: " << e.what() << std::endl;
        std::cerr << "[MockCQG] Make sure '" << configFile
                  << "' exists and is valid." << std::endl;
        return 1;
    } catch (const FIX::RuntimeError& e) {
        std::cerr << "[MockCQG] Runtime error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[MockCQG] Error: " << e.what() << std::endl;
        return 1;
    }
}
