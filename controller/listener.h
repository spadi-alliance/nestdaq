#ifndef TCP_Listener_h
#define TCP_Listener_h

// Accepts incoming connections and launches the sessions

#include <memory>
#include <string>
#include <string_view>

#include "controller/beast_tools.h"

class listener : public std::enable_shared_from_this<listener>
{
public:
    listener(const std::shared_ptr<net::io_context> &ioc, tcp::endpoint endpoint, std::shared_ptr<std::string const> const& doc_root);

    // Start accepting incoming connections
    void run() {
        do_accept();
    }

private:
    std::shared_ptr<net::io_context> ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<std::string const> doc_root_;

    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);
};

#endif