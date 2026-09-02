#pragma once

/**
 * @file websocket_session.h
 * @brief Per-connection WebSocket session for the web DAQ controller.
 */

#include <utility>

#include "controller/beast_tools.h"

/** Manages one WebSocket client connection and forwards messages to WebGui. */
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession>
{

public:
    // Take ownership of the socket
    explicit WebSocketSession(tcp::socket&& socket);

    // Start the asynchronous accept operation
    template<class Body, class Allocator>
    void doAccept(http::request<Body, http::basic_fields<Allocator>> req)
    {
        // Set suggested timeout settings for the websocket
        fWebSocket.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        fWebSocket.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " advanced-server");
        }));

        // Accept the websocket handshake
        fWebSocket.async_accept(std::move(req),
                                beast::bind_front_handler(&WebSocketSession::onAccept, shared_from_this())
                               );
    }

    unsigned int id() const {
        return fId;
    }
    void write(const std::string &message);

private:
    websocket::stream<beast::tcp_stream> fWebSocket;
    beast::flat_buffer fBuffer;
    unsigned int fId{0};

    void onAccept(beast::error_code ec);
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytes_transferred);
};
