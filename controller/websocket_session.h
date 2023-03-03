#ifndef WebSocket_Session_h
#define WebSocket_Session_h

#include "controller/beast_tools.h"

// Echoes back all received WebSocket messages
class websocket_session : public std::enable_shared_from_this<websocket_session>
{

public:
    // Take ownership of the socket
    explicit websocket_session(tcp::socket&& socket);

    // Start the asynchronous accept operation
    template<class Body, class Allocator>
    void do_accept(http::request<Body, http::basic_fields<Allocator>> req)
    {
        // Set suggested timeout settings for the websocket
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " advanced-server");
        }));

        // Accept the websocket handshake
        ws_.async_accept(req,
                         beast::bind_front_handler(&websocket_session::on_accept, shared_from_this())
                        );
    }

    unsigned int id() const {
        return id_;
    }
    void write(const std::string &message);

private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    unsigned int id_{0};

    void on_accept(beast::error_code ec);
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
};

#endif