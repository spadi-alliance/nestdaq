#ifndef HTTP_WebSocketServer_h
#define HTTP_WebSocketServer_h

// HTTP and WebSocket server using boost::beast

#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include "controller/beast_tools.h" 

class listener;

class HttpWebSocketServer 
{
public:
  explicit HttpWebSocketServer(int nThreads);
  HttpWebSocketServer(const HttpWebSocketServer&) = delete;
  HttpWebSocketServer& operator=(const HttpWebSocketServer&) = delete;
  ~HttpWebSocketServer();

  void Run(std::string_view scheme, std::string_view address, std::string_view port, std::string_view doc_root);

private:
  std::shared_ptr<net::io_context> fContext; 
  int fNThreads{0};
  std::shared_ptr<listener> fListener; 
  std::shared_ptr<net::signal_set> fSignals;
  std::vector<std::thread> fThreads;
};

#endif
