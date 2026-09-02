
/** @file
 *  @brief Implements TCP Listener setup for controller HTTP sessions.
 */

#include <iostream>

#include "controller/http_session.h"
#include "controller/listener.h"

Listener::Listener(const std::shared_ptr<net::io_context> &ioc, const tcp::endpoint& endpoint, std::shared_ptr<std::string const> const& doc_root)
    : fContext(ioc)
    , fAcceptor(net::make_strand(*ioc))
    , fDocRoot(doc_root)
    , fStatus(kStatusGood)
{
    beast::error_code ec;

    // Open the acceptor
    const auto kOpenResult = fAcceptor.open(endpoint.protocol(), ec);
    boost::ignore_unused(kOpenResult);
    if(ec) {
        fail(ec, "Listener open");
        fStatus = ec.message();
        return;
    }

    // Allow address reuse
    const auto kSetOptionResult = fAcceptor.set_option(net::socket_base::reuse_address(true), ec);
    boost::ignore_unused(kSetOptionResult);
    if(ec) {
        fail(ec, "Listener set_option");
        fStatus = ec.message();
        return;
    }

    // Bind to the server address
    const auto kBindResult = fAcceptor.bind(endpoint, ec);
    boost::ignore_unused(kBindResult);
    if(ec) {
        fail(ec, "Listener bind");
        fStatus = ec.message();
        return;
    }

    // Start listening for connections
    const auto kListenResult = fAcceptor.listen(net::socket_base::max_listen_connections, ec);
    boost::ignore_unused(kListenResult);
    if(ec) {
        fail(ec, "Listener listen");
        fStatus = ec.message();
        return;
    }
}

void Listener::doAccept()
{
    // The new connection gets its own strand
    fAcceptor.async_accept(net::make_strand(*fContext),
                           beast::bind_front_handler(&Listener::onAccept, shared_from_this())
                          );
}

void Listener::onAccept(beast::error_code ec, tcp::socket socket)
{
    if(ec) {
        fail(ec, "Listener accept");
    } else {
        // Create the http session and run it
        std::make_shared<HttpSession>(std::move(socket), fDocRoot)->run();
    }

    // Accept another connection
    doAccept();
}
