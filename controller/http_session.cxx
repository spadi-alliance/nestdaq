#include <fairmq/FairMQLogger.h>

#include "controller/websocket_session.h"
#include "controller/http_session.h"


//_____________________________________________________________________________
http_session::queue::queue(http_session& self) 
  : self_(self)
{
  static_assert(limit > 0, "queue limit must be positive");
  items_.reserve(limit);
}

//_____________________________________________________________________________
bool http_session::queue::on_write() 
{
  BOOST_ASSERT(! items_.empty());
  auto const was_full = is_full();
  items_.erase(items_.begin());
  if(! items_.empty())
      (*items_.front())();
  return was_full;
}

//=============================================================================

//_____________________________________________________________________________
http_session::http_session(tcp::socket&& socket, std::shared_ptr<std::string const> const& doc_root)
  : stream_(std::move(socket))
  , doc_root_(doc_root)
  , queue_(*this)
{
}

//_____________________________________________________________________________
void http_session::do_read()
{
  // Construct a new parser for each message
  parser_.emplace();

  // Apply a reasonable limit to the allowed size
  // of the body in bytes to prevent abuse.
  parser_->body_limit(10000);

  // Set the timeout.
  stream_.expires_after(std::chrono::seconds(30));

  // Read a request using the parser-oriented interface
  http::async_read(stream_, buffer_, *parser_,
    beast::bind_front_handler(&http_session::on_read, shared_from_this())
  );
}

//_____________________________________________________________________________
void http_session::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
  boost::ignore_unused(bytes_transferred);

  // This means they closed the connection
  if(ec == http::error::end_of_stream) {
    LOG(warn)  << "boost::beast http session: what = " << ec.what() << std::endl;
    return do_close();
  }

  if(ec) {
    return fail(ec, "http read");
  }

  LOG(debug) << " parser_->get() " << parser_->get();
  // See if it is a WebSocket Upgrade
  if(websocket::is_upgrade(parser_->get())) {
    // Create a websocket session, transferring ownership
    // of both the socket and the HTTP request.
    std::make_shared<websocket_session>(stream_.release_socket())->do_accept(parser_->release());
    return;
  }

  // Send the response
  handle_request(*doc_root_, parser_->release(), queue_);

  // If we aren't at the queue limit, try to pipeline another request
  if(! queue_.is_full()) {
    do_read();
  }
}

//_____________________________________________________________________________
void http_session::on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
{
  boost::ignore_unused(bytes_transferred);

  if(ec) {
    return fail(ec, "http write");
  }

  if(close) {
    // This means we should close the connection, usually because
    // the response indicated the "Connection: close" semantic.
    return do_close();
  }

  // Inform the queue that a write completed
  if(queue_.on_write()) {
    // Read another request
    do_read();
  }
}

//_____________________________________________________________________________
void http_session::do_close()
{
  // Send a TCP shutdown
  beast::error_code ec;
  LOG(debug) << "boost::beast http session: Send a TCP shutdown";
  stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

  // At this point the connection is closed gracefully
}