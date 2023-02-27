#include <algorithm>
#include <mutex>

#include <fairmq/FairMQLogger.h>

#include "controller/WebSocketHandle.h"
#include "controller/websocket_session.h"

//_____________________________________________________________________________
websocket_session::websocket_session(tcp::socket&& socket)
  : ws_(std::move(socket))
{
}

//_____________________________________________________________________________
void websocket_session::on_accept(beast::error_code ec)
{
  LOG(debug) << " websocket session : new connection" << std::endl;
  if(ec) {
    return fail(ec, "websocket accept");
  }

  static unsigned int lastId{0};
  static std::mutex mtx;
  {
    std::lock_guard<std::mutex> lock{mtx};
    id_ = ++lastId;
    OnConnect(shared_from_this());
  }

  // Read a message
  do_read();
}

//_____________________________________________________________________________
void websocket_session::do_read()
{
  // Read a message into our buffer
  ws_.async_read(buffer_,
    beast::bind_front_handler(&websocket_session::on_read, shared_from_this())
  );
}

//_____________________________________________________________________________
void websocket_session::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
  boost::ignore_unused(bytes_transferred);

  // This indicates that the websocket_session was closed
  if(ec == websocket::error::closed) {
    LOG(warn) << "websocket session : " << ec.what();
    OnClose(id_);
    return;
  }

  if(ec) {
    LOG(warn) << "websocket session : " << ec.what();
    fail(ec, "websocket read");
  }

  ws_.text(ws_.got_text());
  if (bytes_transferred>0) {
    if (ws_.got_text()) {
      const std::string m(beast::buffers_to_string(buffer_.data()));
//      std::cout << "received message: got_text() ? " << ws_.got_text()
//                << " buffer (size = " << m.size() << " bytes): " << m << std::endl;
      buffer_.consume(buffer_.size());
      OnRead(id_, m);

    } else {
      const auto bufferBegin = net::buffer_cast<const char*>(beast::buffers_front(buffer_.data()));
      const auto bufferEnd = bufferBegin + net::buffer_size(buffer_.data());
      std::vector<char> buf(bufferBegin, bufferEnd);
//      std::cout << "received message: got_text() ? " << ws_.got_text() << "\n"
//                << " buffer (" << buffer_.size() << " bytes, " 
//                << " transferred: " << bytes_transferred << " bytes)\n";
//      std::for_each(buf.begin(), buf.end(), 
//                   [](auto x) { std::cout << static_cast<uint16_t>(x) << " "; });
//      std::cout << std::endl;
      buffer_.consume(buffer_.size());
      OnRead(id_, buf);
    }
  } else {
  }

  // Do another read
  do_read();
}

//_____________________________________________________________________________
void websocket_session::write(const std::string &message)
{
  // synchronous write
  ws_.write(net::buffer(message));
}