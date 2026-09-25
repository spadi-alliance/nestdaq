#pragma once

/**
 * @file http_session.h
 * @brief Per-connection HTTP session that serves files and upgrades WebSockets.
 */

#include <cstdint>
#include <memory>
#include <vector>

#include "controller/beast_tools.h"

/** Handles one HTTP server connection. */
class HttpSession : public std::enable_shared_from_this<HttpSession>
{
    // This queue is used for HTTP pipelining.
    class Queue
    {
        // Maximum number of responses we will queue
        static constexpr std::uint8_t kLimit = 8;

        // The type-erased, saved work item
        struct Work
        {
            Work() = default;
            Work(const Work&) = delete;
            Work& operator=(const Work&) = delete;
            Work(Work&&) = delete;
            Work& operator=(Work&&) = delete;
            virtual ~Work() = default;
            virtual void operator()() = 0;
        };

        HttpSession& fSelf; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        std::vector<std::unique_ptr<Work>> fItems;

    public:
        explicit Queue(HttpSession& self);

        // Returns `true` if we have reached the queue limit.
        bool isFull() const {
            return fItems.size() >= kLimit;
        }

        // Called when a message finishes sending
        // Returns `true` if the caller should initiate a read
        bool onWrite();

        // Called by the HTTP handler to send a response.
        template<bool isRequest, class Body, class Fields>
        void operator()(http::message<isRequest, Body, Fields>&& msg)
        {
            // This holds a work item
            struct WorkImpl : Work
            {
                HttpSession& fSelf; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
                http::message<isRequest, Body, Fields> fMessage;

                WorkImpl(HttpSession& self, http::message<isRequest, Body, Fields>&& msg)
                    : fSelf(self)
                    , fMessage(std::move(msg))
                {}

                void operator()() override
                {
                    http::async_write(fSelf.fStream, fMessage,
                                      beast::bind_front_handler(&HttpSession::onWrite, fSelf.shared_from_this(), fMessage.need_eof())
                                     );
                }
            };

            // Allocate and store the work
            fItems.push_back(boost::make_unique<WorkImpl>(fSelf, std::move(msg)));

            // If there was no previous work, start this one
            if(fItems.size() == 1) {
                (*fItems.front())();
            }
        }
    };

public:
    // Take ownership of the socket
    HttpSession(tcp::socket&& socket, std::shared_ptr<std::string const> const& doc_root);

    // Start the session
    void run() {
        doRead();
    }

private:
    beast::tcp_stream fStream;
    beast::flat_buffer fBuffer;
    std::shared_ptr<std::string const> fDocRoot;
    Queue fQueue;

    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<http::request_parser<http::string_body>> fParser;

    void doRead();
    void onRead(beast::error_code ec, std::size_t bytes_transferred);
    void onWrite(bool close, beast::error_code ec, std::size_t bytes_transferred);
    void doClose();
};
