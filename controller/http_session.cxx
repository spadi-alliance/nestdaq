/** @file
 *  @brief Implements HTTP request handling for the DAQ web controller.
 */

#include <fairlogger/Logger.h>

#include "controller/websocket_session.h"
#include "controller/http_session.h"

static constexpr std::uint64_t kHttpBodyLimit{10000};
static constexpr int kHttpSessionTimeoutSeconds{30};

HttpSession::Queue::Queue(HttpSession& self)
    : fSelf(self)
{
    static_assert(kLimit > 0, "queue limit must be positive");
    fItems.reserve(kLimit);
}

bool HttpSession::Queue::onWrite()
{
    BOOST_ASSERT(! fItems.empty());
    auto const kWasFull = isFull();
    fItems.erase(fItems.begin());
    if(! fItems.empty()) {
        (*fItems.front())();
    }
    return kWasFull;
}

//=============================================================================

HttpSession::HttpSession(tcp::socket&& socket, std::shared_ptr<std::string const> const& doc_root)
    : fStream(std::move(socket))
    , fDocRoot(doc_root)
    , fQueue(*this)
{
}

void HttpSession::doRead()
{
    // Construct a new parser for each message
    fParser.emplace();

    // Apply a reasonable limit to the allowed size
    // of the body in bytes to prevent abuse.
    fParser->body_limit(kHttpBodyLimit);

    // Set the timeout.
    fStream.expires_after(std::chrono::seconds(kHttpSessionTimeoutSeconds));

    // Read a request using the parser-oriented interface
    http::async_read(fStream, fBuffer, *fParser,
                     beast::bind_front_handler(&HttpSession::onRead, shared_from_this())
                    );
}

void HttpSession::onRead(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if(ec == http::error::end_of_stream) {
        LOG(warn)  << "boost::beast http session: what = " << ec.what() << '\n';
        doClose();
        return;
    }

    if(ec) {
        fail(ec, "http read");
        return;
    }

    LOG(debug) << " fParser->get() " << fParser->get();
    // See if it is a WebSocket Upgrade
    if(websocket::is_upgrade(fParser->get())) {
        // Create a websocket session, transferring ownership
        // of both the socket and the HTTP request.
        std::make_shared<WebSocketSession>(fStream.release_socket())->doAccept(fParser->release());
        return;
    }

    // Send the response
    handleRequest(*fDocRoot, fParser->release(), fQueue);

    // If we aren't at the queue limit, try to pipeline another request
    if(! fQueue.isFull()) {
        doRead();
    }
}

void HttpSession::onWrite(bool close, beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec) {
        fail(ec, "http write");
        return;
    }

    if(close) {
        // This means we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        doClose();
        return;
    }

    // Inform the queue that a write completed
    if(fQueue.onWrite()) {
        // Read another request
        doRead();
    }
}

void HttpSession::doClose()
{
    // Send a TCP shutdown
    beast::error_code ec;
    LOG(debug) << "boost::beast http session: Send a TCP shutdown";
    const auto kShutdownResult = fStream.socket().shutdown(tcp::socket::shutdown_send, ec);
    boost::ignore_unused(kShutdownResult);

    // At this point the connection is closed gracefully
}
