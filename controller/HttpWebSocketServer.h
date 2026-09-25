#pragma once

/**
 * @file HttpWebSocketServer.h
 * @brief HTTP and WebSocket server wrapper used by `daq-webctl`.
 */

#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include "controller/beast_tools.h"

class Listener;

class HttpWebSocketServer
{
public:
    explicit HttpWebSocketServer(int n_threads);
    HttpWebSocketServer(const HttpWebSocketServer&) = delete;
    HttpWebSocketServer& operator=(const HttpWebSocketServer&) = delete;
    HttpWebSocketServer(HttpWebSocketServer&&) = delete;
    HttpWebSocketServer& operator=(HttpWebSocketServer&&) = delete;
    ~HttpWebSocketServer();

    void run(std::string_view scheme, std::string_view address, std::string_view port, std::string_view doc_root);

private:
    std::shared_ptr<net::io_context> fContext;
    int fNThreads{0};
    std::shared_ptr<Listener> fListener;
    std::shared_ptr<net::signal_set> fSignals;
    std::vector<std::thread> fThreads;
};
