/** @file
 *  @brief Implements the HTTP/WebSocket server entry point.
 */

#include <iostream>
#include <string>

#include <fairlogger/Logger.h>

#include "controller/listener.h"
#include "controller/HttpWebSocketServer.h"

HttpWebSocketServer::HttpWebSocketServer(int n_threads)
    : fContext(std::make_shared<net::io_context>(n_threads)) // The io_context is required for all I/O
    , fNThreads(n_threads)
{
    fThreads.reserve(n_threads-1);
}

HttpWebSocketServer::~HttpWebSocketServer()
{
    // Block until all the threads exist
    for (auto& t: fThreads) {
        t.join();
    }
}

void HttpWebSocketServer::run(std::string_view /*scheme*/, std::string_view address, std::string_view port, std::string_view doc_root)
{
    const auto kDocRootStorage = std::make_shared<std::string>(doc_root);
    const auto kIpAddr = net::ip::make_address(address);
    const auto kPortNumber = std::stoi(std::string{port});

    // Create and launch a listening port
    fListener = std::make_shared<Listener>(fContext, tcp::endpoint(kIpAddr, kPortNumber), kDocRootStorage);
    if (fListener->getStatus()!=Listener::kStatusGood) {
        return;
    }
    fListener->run();

    // Capture SIGINT and SIGTERM to perform a clean shutdown
    fSignals = std::make_shared<net::signal_set>(*fContext, SIGINT, SIGTERM);
    fSignals->async_wait([&ioc = *fContext](const beast::error_code &ec, int n) {
        LOG(fatal) << "Got signal : ec = " << ec.what() << ", n = " << n;
        ioc.stop();
    });

    // Run the I/O service on the requested number of threads
    for (auto i=fNThreads-1; i>0; --i) {
        fThreads.emplace_back([&ioc = *fContext] { ioc.run(); });
    }
    fContext->run();
}
