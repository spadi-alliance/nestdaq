#ifndef DaqService_Plugins_Plugin_h
#define DaqService_Plugins_Plugin_h

// Redis client for DAQ service discovery 


// for linux pid
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <initializer_list>
#include <memory>
//#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>

#include <fairmq/Plugin.h>
#include <fairmq/StateQueue.h>

#include "plugins/Timer.h"

// forward declaration
namespace sw::redis { class Redis; }

namespace daq::service {

struct Presence {
  std::string key;
};

struct Health {
  std::string key;
  std::string hostName;
  std::string ipAddress;
  std::chrono::system_clock::time_point createdTimeSystem;
  std::chrono::steady_clock::time_point createdTime;
//  std::chrono::system_clock::time_point updatedTime;
};

class TopologyConfig;
class RunLogger;

class Plugin : public fair::mq::Plugin
{
public:
  using DeviceState = fair::mq::Plugin::DeviceState;
  using work_guard_t = net::executor_work_guard<net::io_context::executor_type>;

  Plugin(std::string_view name, 
          const fair::mq::Plugin::Version &version, 
          std::string_view maintainer, 
          std::string_view homepage, 
          fair::mq::PluginServices *pluginServices);
  Plugin(const Plugin &) = delete;
  Plugin& operator=(const Plugin &) = delete;
  ~Plugin() override;

  std::mutex& GetMutex() { return fMutex; }
  std::shared_ptr<sw::redis::Redis> GetClient() const { return fClient; } 
  bool IsCanceled() const { return fResetDeviceRequested || fPluginShutdownRequested; } 
  bool IsResetDeviceRequested() const { return fResetDeviceRequested; }
  bool IsShutdownRequested() const { return fPluginShutdownRequested; }

private:
  void ChangeDeviceStateByMultiCommand(std::string_view cmd);
  void ChangeDeviceStateBySingleCommand(std::string_view cmd);
  void PublishDaqState(std::string_view state, std::string_view lastChecked);
  void ReadRunNumber();
  void Register();
  void ResetTtl(); 
  void RunStartupSequence();
  void RunShutdownSequence();
  void SetCurrentWorkingDirectory();
  void SetId();
  void SetProcessName();
  void SubscribeToDaqCommand();
  void Unregister();
  void WriteProgOptions();
  void WriteStartTime();
  void WriteStopTime();

  std::string fSeparator;

  pid_t fPid;
  std::string fProcessName;
  std::string fCwd;
  boost::uuids::uuid fUuid; 
  std::string fId; // instance id configured by command line option or uuid
  std::string fServiceName;
  std::shared_ptr<sw::redis::Redis> fClient;
  std::unordered_set<std::string> fRegisteredKeys;
  std::unordered_map<std::string, std::string> fRegisteredHashes;
  //std::string fSeparator;
  std::unique_ptr<Presence> fPresence;
  std::unique_ptr<Health> fHealth;
  std::string fProgOptionKeyName;
  long long fMaxTtl;
  long long fTtlUpdateInterval;

  std::string fStartupState;

  bool fStepByStep{false};
  std::shared_ptr<net::io_context> fContext;
  std::unique_ptr<Timer> fTimer;
  std::unique_ptr<work_guard_t> fWorkGuard;
  std::thread fTimerThread;
  std::thread fStateControlThread;
  std::mutex fMutex;

  std::atomic<bool> fPluginShutdownRequested{false};
  std::atomic<bool> fResetDeviceRequested{false};
  fair::mq::StateQueue fStateQueue;

  std::unique_ptr<TopologyConfig> fTopology;
  std::unique_ptr<RunLogger> fLogger;
};

//_____________________________________________________________________________
auto PluginProgramOptions() -> fair::mq::Plugin::ProgOptions; 

REGISTER_FAIRMQ_PLUGIN(
  Plugin,                                      // Class name
  daq_service,                                 // Plugin name (lowercase chars only) 
  (fair::mq::Plugin::Version{0, 0, 0}),        // Version
  "DAQService <maintainer@daq.service.net>",   // Maintainer
  "https://github.com/spadi-alliance/nestdaq", // Homepage
  daq::service::PluginProgramOptions           // Free function which declares custom program options for the plugin
                                               //    signature: () -> boost::optional<boost::program_options::options_decription>
) // end of macro: REGISTER_FAIRMQ_PLUGIN 

} // namespace daq::service

#endif
