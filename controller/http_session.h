#ifndef HTTP_Session_h
#define HTTP_Session_h

#include <memory>
#include <vector>

#include "controller/beast_tools.h"

// Handles an HTTP server connection
class http_session : public std::enable_shared_from_this<http_session>
{
    // This queue is used for HTTP pipelining.
    class queue
    {
        enum
        {
            // Maximum number of responses we will queue
            limit = 8
        };

        // The type-erased, saved work item
        struct work
        {
            virtual ~work() = default;
            virtual void operator()() = 0;
        };

        http_session& self_;
        std::vector<std::unique_ptr<work>> items_;

    public:
        explicit queue(http_session& self);

        // Returns `true` if we have reached the queue limit
        bool is_full() const {
            return items_.size() >= limit;
        }

        // Called when a message finishes sending
        // Returns `true` if the caller should initiate a read
        bool on_write();

        // Called by the HTTP handler to send a response.
        template<bool isRequest, class Body, class Fields>
        void operator()(http::message<isRequest, Body, Fields>&& msg)
        {
            // This holds a work item
            struct work_impl : work
            {
                http_session& self_;
                http::message<isRequest, Body, Fields> msg_;

                work_impl(http_session& self, http::message<isRequest, Body, Fields>&& msg)
                    : self_(self)
                    , msg_(std::move(msg))
                {}

                void operator()()
                {
                    http::async_write(self_.stream_, msg_,
                                      beast::bind_front_handler(&http_session::on_write, self_.shared_from_this(), msg_.need_eof())
                                     );
                }
            };

            // Allocate and store the work
            items_.push_back(boost::make_unique<work_impl>(self_, std::move(msg)));

            // If there was no previous work, start this one
            if(items_.size() == 1) {
                (*items_.front())();
            }
        }
    };

public:
    // Take ownership of the socket
    http_session(tcp::socket&& socket, std::shared_ptr<std::string const> const& doc_root);

    // Start the session
    void run() {
        do_read();
    }

private:
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    std::shared_ptr<std::string const> doc_root_;
    queue queue_;

    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<http::request_parser<http::string_body>> parser_;

    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred);
    void do_close();
};

#endif