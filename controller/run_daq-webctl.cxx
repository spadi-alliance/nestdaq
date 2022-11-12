#include <cstdlib>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/program_options.hpp>

#include <fairmq/FairMQLogger.h>

#include "plugins/Constants.h"
#include "plugins/tools.h"
#include "controller/DaqWebControlDefaultDocRootPath.h"
#include "controller/HttpWebSocketServer.h"
#include "controller/WebSocketHandle.h"
#include "controller/websocket_session.h"
#include "controller/WebGui.h"

namespace bpo = boost::program_options;

using namespace std::string_literals;

std::mutex wsMutex;
std::unordered_map<unsigned int, std::shared_ptr<websocket_session>> wsSessions;
std::unique_ptr<WebGui> daqControl;

//_____________________________________________________________________________
bpo::options_description MakeOption()
{
  bpo::options_description options("options");
  bpo::options_description wsOptions("websocket handler options");
  bpo::options_description redisOptions("redis options");
  wsOptions.add_options()
  // 
  // 
  ("http-uri",  bpo::value<std::string>()->default_value("http://0.0.0.0:8080"), "http server URI. (scheme://address:port)")
  //
  ("threads", bpo::value<unsigned int>()->default_value(1), "number of threads for http server")
  //
  ("doc-root", bpo::value<std::string>()->default_value(std::string(DefaultDocRootPath.data())), "Directory of the document root, which is the starting point when looking for html");

  redisOptions.add_options()
  //
  ("redis-uri", bpo::value<std::string>()->default_value("tcp://127.0.0.1:6379"), "URI of redis-server")
  //
  ("separator", bpo::value<std::string>()->default_value(":"), "namespace separator for redis keys")
  //
  ("save-command", bpo::value<std::string>()->default_value("save"), "redis rdb save command. (\"save\" or \"bgsave\")")
  //
  ("rdb-dir", bpo::value<std::string>(), "directory for redis rdb")
  //
  ("dbfilename-format", bpo::value<std::string>()->default_value("run{:06}.rdb"), "rdb file name format")
  //
  ("severity", bpo::value<std::string>()->default_value("info"), "severity level of FairLogger")
  //
  ("verbosity", bpo::value<std::string>()->default_value("medium"), "verbosity level of FairLogger");

  
  options.add_options()
  //
  ("help,h", "print this help");

  options.add(wsOptions)
         .add(redisOptions);
  return options;
}

//_____________________________________________________________________________
auto ParseHttpUri(const std::string& uri) -> const std::tuple<std::string, std::string, std::string>
{
  // pattern = (scheme)://(address):(port)
  std::regex pattern{R"(^([^:\/?#]+)://([^\/?#]+):(\d+))"};

  std::smatch matchResult;
  if (std::regex_match(uri, matchResult, pattern)) {
  // int count=0;
  //  for (const auto & s : matchResult) {
  //    std::cout << count++ << " : " << s << std::endl;
  //  }
  //std::cout << " count = " << count << std::endl;
  } else {
    std::cerr << "error: " __FILE__ << ":" << __LINE__ << " " << __func__ 
              << "\n  std::regex_match failed: uri = " << uri 
              << "\n  It should be (scheme)://(address):(port)" << std::endl;
    return {};
  }

  // std::cout << " matchResult.size() = " << matchResult.size() << std::endl;
  if (matchResult.size()!=4) {
    std::cerr << "error: " << __FILE__ << ":" << __LINE__ << " " << __func__ 
              << "\n  http server URI format is invalid. URI = " << uri
              << "\n  number of matched parts = " << matchResult.size() 
              << "\n  It should be (scheme)://(address):(port)" << std::endl;
    return {};
  }
  return {matchResult[1], matchResult[2], matchResult[3]}; 
}

//_____________________________________________________________________________
int main(int argc, char* argv[])
{
  std::cin.tie(nullptr);
  std::ios::sync_with_stdio(false);

  bpo::variables_map vm;
  auto ret = ParseCommandLine(argc, argv, MakeOption(), vm); 
  if (ret!=EXIT_SUCCESS) {
    return ret;
  }

  {
    fair::Logger::SetConsoleColor(true);
    fair::Logger::SetConsoleSeverity(vm["severity"].as<std::string>());
    const auto verbosity = vm["verbosity"].as<std::string>();
    fair::Logger::SetVerbosity(vm["verbosity"].as<std::string>());
  }

  // ============================================
  // redis client setup
  const auto redisUri  = vm["redis-uri"].as<std::string>();
  const auto channel   = daq::service::CommandChannelName.data(); 
  const auto sep       = vm["separator"].as<std::string>();
  std::cout << " redis-server URI  = " << redisUri << std::endl;
  std::cout << " command-channel   = " << channel  << std::endl;
  std::cout << " separator         = " << sep      << std::endl;

  daqControl = std::make_unique<WebGui>();
  if (!daqControl->ConnectToRedis(redisUri, channel, sep)) {
    return EXIT_FAILURE;
  }
  // ============================================
  daqControl->SetSendFunction([](auto connid, const auto& arg){ 
    const auto &websocketIdList = daqControl->GetWebSocketIdList(); 
    if (websocketIdList.empty()) {
      //std::cout << __FILE__ << ":" << __LINE__ << " no clients" << std::endl;
      return;
    }
    if (connid==0) { // broadcast message to registered clients 
      for (const auto& [i, t] : websocketIdList) {
        //std::cout << __FILE__ << ":" << __LINE__ << " id = " << i << ", msg = " << arg << std::endl;
        Write(i, arg);
      }
    } else {
      Write(connid, arg); 
    }
  }); 
  daqControl->SetTerminateFunction([](){ 
    std::cout << __FILE__ << ":" << __LINE__ << " Termination is requested." << std::endl;
  });

  if (vm.count("rdb-dir")>0) {
    daqControl->SetDBDir(vm["rdb-dir"].as<std::string>());
    daqControl->SetDBFileNameFormat(vm["dbfilename-format"].as<std::string>());
    daqControl->SetSaveCommand(vm["save-command"].as<std::string>());
  }

  // ============================================
  // http server setup
  const auto httpUri = vm["http-uri"].as<std::string>();
  std::cout << " http serve URI = " << httpUri << std::endl;
  const auto &[httpScheme, httpAddress, httpPort] = ParseHttpUri(httpUri);
  std::cout << " http server scheme  = " << httpScheme 
            << "\n http server address = " << httpAddress 
            << "\n http server port    = " << httpPort << std::endl;
  const auto nThreads = vm["threads"].as<unsigned int>();
  std::cout << " http threads = " << nThreads << std::endl;
  const auto docRoot = vm["doc-root"].as<std::string>();
  std::cout << " doc-root = " << docRoot << std::endl;

  HttpWebSocketServer server(nThreads);
  server.Run(httpScheme, httpAddress, httpPort, docRoot);
  return ret;
}

//=============================================================================
// WebSocketHandle functions
//_____________________________________________________________________________
void OnClose(unsigned int id)
{
  {
    std::lock_guard<std::mutex> lock{wsMutex};
    wsSessions.erase(id);
  }
  daqControl->ProcessData(id, R"({ "command": "ON_CLOSED" })");
  //std::cout << __FILE__ << ":" << __LINE__ << " " << __func__ << " done" << std::endl;
}

//_____________________________________________________________________________
void OnConnect(const std::shared_ptr<websocket_session> &session)
{
  auto id = session->id();
  {
    std::lock_guard<std::mutex> lock{wsMutex};
    wsSessions.emplace(id, session);
  }
  daqControl->ProcessData(id, R"({ "command": "ON_CONNECT" })"); 
  //std::cout << __FILE__ << ":" << __LINE__ << " " << __func__ << " done" << std::endl;
}

//_____________________________________________________________________________
void OnRead(unsigned int id, const std::string& message)
{
  daqControl->ProcessData(id, message);
  //std::cout << __FILE__ << ":" << __LINE__ << " " << __func__ << " done" << std::endl;
}

//_____________________________________________________________________________
void OnRead(unsigned int id, const std::vector<char>& message)
{
}

//_____________________________________________________________________________
void Write(unsigned int id, const std::string& message)
{

  std::shared_ptr<websocket_session> session; 
  {
    std::lock_guard<std::mutex> lock{wsMutex};
    session = wsSessions[id];
  }
  if (session) {
    session->write(message);
  }
  //std::cout << __FILE__ << ":" << __LINE__ << " " << __func__ << " done" << std::endl;
}