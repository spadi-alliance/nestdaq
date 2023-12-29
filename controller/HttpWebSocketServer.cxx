#include <iostream>
#include <string>

#include <fairmq/FairMQLogger.h>

#include "controller/listener.h"
#include "controller/HttpWebSocketServer.h"

//_____________________________________________________________________________
HttpWebSocketServer::HttpWebSocketServer(int nThreads)
    : fContext(std::make_shared<net::io_context>(nThreads)) // The io_context is required for all I/O
    , fNThreads(nThreads)
{
    fThreads.reserve(nThreads-1);
}

//_____________________________________________________________________________
HttpWebSocketServer::~HttpWebSocketServer()
{
    // Block until all the threads exist
    for (auto& t: fThreads) {
        t.join();
    }
}

//_____________________________________________________________________________
void HttpWebSocketServer::Run(std::string_view scheme, std::string_view address, std::string_view port, std::string_view doc_root)
{
    const auto docRoot = std::make_shared<std::string>(doc_root.data());
    const auto ipAddr = net::ip::make_address(address);

    // Create and launch a listening port
    fListener = std::make_shared<listener>(fContext, tcp::endpoint(ipAddr, std::stoi(port.data())), docRoot);
    if (fListener->get_status()!=listener::StatusGood) {
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