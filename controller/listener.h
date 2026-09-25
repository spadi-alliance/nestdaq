#pragma once

/**
 * @file listener.h
 * @brief TCP acceptor that launches HTTP/WebSocket sessions.
 */

#include <memory>
#include <string>
#include <string_view>

#include "controller/beast_tools.h"

class Listener : public std::enable_shared_from_this<Listener>
{
public:
    static constexpr std::string_view kStatusGood {"good"};
    Listener(const std::shared_ptr<net::io_context> &ioc, const tcp::endpoint& endpoint, std::shared_ptr<std::string const> const& doc_root);

    std::string getStatus() const {
        return fStatus;
    }

    // Start accepting incoming connections
    void run() {
        doAccept();
    }

private:
    std::shared_ptr<net::io_context> fContext;
    tcp::acceptor fAcceptor;
    std::shared_ptr<std::string const> fDocRoot;
    std::string fStatus;

    void doAccept();
    void onAccept(beast::error_code ec, tcp::socket socket);
};
