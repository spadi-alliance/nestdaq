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
  ("poll-interval", bpo::value<uint64_t>()->default_value(500), "state polling interval in millisecond")
  //
  ("dbfilename-format", bpo::value<std::string>()->default_value("run{:06}.rdb"), "rdb file name format")
  //
  ("log-to-file", bpo::value<std::string>()->default_value(""), "FairLogger Log output to a file")
  //
  ("file-severity", bpo::value<std::string>()->default_value("info"), "FairLogger Log severity level (file) : trace, debug, info, state, warn, error, fatal, nolog")
  //
  ("severity", bpo::value<std::string>()->default_value("info"), "FairLogger Log severity level (console): trace, debug, info, state, warn, error, fatal, nolog")
  //
  ("verbosity", bpo::value<std::string>()->default_value("medium"), "FairLogger Log verbosity level: veryhigh, high, medium, low")
  //
  ("color", bpo::value<bool>()->default_value(true), "FairLogger Log color (true/false)");

  
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
    LOG(error) << "error: " << __FILE__ << ":" << __LINE__ << " " << __func__ 
               << "\n  std::regex_match failed: uri = " << uri 
               << "\n  It should be (scheme)://(address):(port)";
    return {};
  }

  // LOG(debug) << " matchResult.size() = " << matchResult.size();
  if (matchResult.size()!=4) {
    LOG(error) << "error: " << __FILE__ << ":" << __LINE__ << " " << __func__ 
               << "\n  http server URI format is invalid. URI = " << uri
               << "\n  number of matched parts = " << matchResult.size() 
               << "\n  It should be (scheme)://(address):(port)";
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
    const auto logFile = vm["log-to-file"].as<std::string>();
    const auto verbosity = vm["verbosity"].as<std::string>();
    fair::Logger::SetVerbosity(verbosity);
    if (logFile.empty()) {
      fair::Logger::SetConsoleColor(vm["color"].as<bool>());
      fair::Logger::SetConsoleSeverity(vm["severity"].as<std::string>());
    } else {
      fair::Logger::InitFileSink(vm["file-severity"].as<std::string>(), logFile);
      fair::Logger::SetConsoleSeverity("nolog");
    }
  }

  // ============================================
  // redis client setup
  const auto redisUri  = vm["redis-uri"].as<std::string>();
  const auto channel   = daq::service::CommandChannelName.data(); 
  const auto sep       = vm["separator"].as<std::string>();
  LOG(info) << "redis-server URI  = " << redisUri;
  LOG(info) << "command-channel   = " << channel;
  LOG(info) << "separator         = " << sep;

  daqControl = std::make_unique<WebGui>();

  if (vm.count("rdb-dir")>0) {
    daqControl->SetDBDir(vm["rdb-dir"].as<std::string>());
    daqControl->SetDBFileNameFormat(vm["dbfilename-format"].as<std::string>());
    daqControl->SetSaveCommand(vm["save-command"].as<std::string>());
  }
  daqControl->SetPollIntervalMS(vm["poll-interval"].as<uint64_t>());
  if (!daqControl->ConnectToRedis(redisUri, channel, sep)) {
    return EXIT_FAILURE;
  }
  // ============================================
  daqControl->SetSendFunction([](auto connid, const auto& arg){ 
    const auto &websocketIdList = daqControl->GetWebSocketIdList(); 
    if (websocketIdList.empty()) {
      LOG(info) << " no websocket clients";
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      return;
    }
    if (connid==0) { // broadcast message to registered clients 
      for (const auto& [i, t] : websocketIdList) {
        LOG(debug) << "Send message to websocket client id = " << i << ", msg = " << arg;
        Write(i, arg);
      }
    } else {
      Write(connid, arg); 
    }
  }); 
  daqControl->SetTerminateFunction([](){ 
    LOG(info) << " Termination is requested.";
  });


  // ============================================
  // http server setup
  const auto httpUri = vm["http-uri"].as<std::string>();
  LOG(info) << "http serve URI = " << httpUri;
  const auto &[httpScheme, httpAddress, httpPort] = ParseHttpUri(httpUri);
  LOG(info) << "http server scheme  = " << httpScheme;
  LOG(info) << "http server address = " << httpAddress;
  LOG(info) << "http server port    = " << httpPort;
  const auto nThreads = vm["threads"].as<unsigned int>();
  LOG(info) << "http threads = " << nThreads;
  const auto docRoot = vm["doc-root"].as<std::string>();
  LOG(info) << "doc-root = " << docRoot;

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
  LOG(debug) << __func__ << " websocket id = " << id << " done";
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
  LOG(trace) << __func__ << " websocket id = " << id << " done";
}

//_____________________________________________________________________________
void OnRead(unsigned int id, const std::string& message)
{
  daqControl->ProcessData(id, message);
  LOG(trace) << __func__ << " websocket id = " << id << " done";
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
  LOG(trace) << __func__ << " websocket id = " << id << " done";
}