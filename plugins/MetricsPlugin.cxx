#include <sys/types.h>
#include <unistd.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <fairmq/FairMQLogger.h>

#include <sw/redis++/redis++.h>

#include "plugins/Constants.h"
#include "plugins/Functions.h"
#include "plugins/TimeUtil.h"
#include "plugins/MetricsPlugin.h"

static constexpr std::string_view MyClass{"daq::service::MetricsPlugin"};

using namespace std::string_literals;

enum ProcStat { 
  // measured in USER_HZ
  User = 1,   // Time spent in user mode.
  Nice,       // Time spend in user mode with low priority (nice)
  System,     // Time spent in system mode. 
  Idle        // Time spent in the idle task. 
};

enum ProcSelfStat { 
  // /proc/[pid]/stat 
  Pid         = 0,  // the process ID
  Comm        = 1,  // The file name of the executable. 
  Utime       = 13, // Amount of time that this process has been scheduled in user mode, measured in clock ticks. 
                    // This includes guest time. 
  Stime       = 14, // Amount of time that this process has been schedule in kernel mode, measured in clock ticks.
  Starttime   = 21, // The time the process started after system boot. 
  Vsize       = 22, // virtual set size in bytes
  Rss         = 23, // resident set size (= used real memory) in number of pages. no accurate
};

enum SocketMetricsRegexIndex {
  All, 
  Channel, 
  SubChannelIndex, 
  NumMessageIn, 
  BytesIn,
  NumMessageOut, 
  BytesOut,
  NSocketMetricsRegexIndex
};

namespace daq::service {
//_____________________________________________________________________________
ProcessStatKey  Append(const ProcessStatKey& input, std::string_view s, std::string_view separator)
{
  ProcessStatKey ret;
  ret.cpu     = join({input.cpu,     s.data()}, separator.data());
  ret.ram     = join({input.ram,     s.data()}, separator.data());
  ret.stateId = join({input.stateId, s.data()}, separator.data());
  return ret;
}

//_____________________________________________________________________________
SocketMetricsKey  Append(const SocketMetricsKey &input, std::string_view s, std::string_view separator)
{
  SocketMetricsKey ret;
  ret.msgIn    = join({input.msgIn,    s.data()}, separator.data());
  ret.msgOut   = join({input.msgOut,   s.data()}, separator.data());
  ret.bytesIn  = join({input.bytesIn,  s.data()}, separator.data());
  ret.bytesOut = join({input.bytesOut, s.data()}, separator.data());
  return ret;
}

//_____________________________________________________________________________
ProcessStatKey Prepend(const ProcessStatKey& input, std::string_view s, std::string_view separator) 
{
  ProcessStatKey ret;
  ret.cpu     = join({s.data(), input.cpu},     separator.data());
  ret.ram     = join({s.data(), input.ram},     separator.data());
  ret.stateId = join({s.data(), input.stateId}, separator.data());
  return ret;
}

//_____________________________________________________________________________
SocketMetricsKey Prepend(const SocketMetricsKey& input, std::string_view s, std::string_view separator)
{
  SocketMetricsKey ret;
  ret.msgIn    = join({s.data(), input.msgIn},    separator.data());
  ret.msgOut   = join({s.data(), input.msgOut},   separator.data());
  ret.bytesIn  = join({s.data(), input.bytesIn},  separator.data());
  ret.bytesOut = join({s.data(), input.bytesOut}, separator.data());
  return ret;
}

//_____________________________________________________________________________
ProcessStatKey ReplaceAll(const ProcessStatKey& input, std::string_view search, std::string format)
{
  ProcessStatKey ret;
  ret.cpu     = boost::replace_all_copy(input.cpu,     search.data(), format.data());
  ret.ram     = boost::replace_all_copy(input.ram,     search.data(), format.data());
  ret.stateId = boost::replace_all_copy(input.stateId, search.data(), format.data());
  return ret;
}

//_____________________________________________________________________________
SocketMetricsKey ReplaceAll(const SocketMetricsKey& input, std::string_view search, std::string format)
{
  SocketMetricsKey ret;
  ret.msgIn    = boost::replace_all_copy(input.msgIn,    search.data(), format.data());
  ret.msgOut   = boost::replace_all_copy(input.msgOut,   search.data(), format.data());
  ret.bytesIn  = boost::replace_all_copy(input.bytesIn,  search.data(), format.data());
  ret.bytesOut = boost::replace_all_copy(input.bytesOut, search.data(), format.data());
  return ret;
}
} // namespace daq::service

//_____________________________________________________________________________
auto daq::service::MetricsPluginProgramOptions() -> fair::mq::Plugin::ProgOptions
{
  namespace bpo = boost::program_options;
  using opt = daq::service::MetricsPlugin::OptionKey;
  LOG(debug) << "daq::service::MetricsPluginProgramOptions: add_options";

  auto options = bpo::options_description(MyClass.data());
  options.add_options()
  (opt::UpdateInterval.data(), bpo::value<long long>()->default_value(1000),     "update interval in milliseconds for CPU and memory usage.")
  (opt::ServerUri.data(),      bpo::value<std::string>(),                        "Redis server URI (if empty, the same URI of the service registry is used.)")
  (opt::Retention.data(),      bpo::value<std::string>()->default_value("0"),    "Retention time in msec for time series data. When set to 0, the series is not trimmed at all.")
  (opt::RecreateTS.data(),     bpo::value<std::string>()->default_value("true"), "Recreate timeseries data on state transition to Running")
  (opt::MaxTtl.data(),         bpo::value<std::string>()->default_value("3000"), "Max TTL for metrics in milliseconds. (if zero or negative, no TTL is set.)");
  return options; 
}

//_____________________________________________________________________________
daq::service::MetricsPlugin::MetricsPlugin(std::string_view name, 
                                           const fair::mq::Plugin::Version &version, 
                                           std::string_view maintainer, 
                                           std::string_view homepage,
                                           fair::mq::PluginServices *pluginServices)
  : fair::mq::Plugin(name.data(), version, maintainer.data(), homepage.data(), pluginServices)
{
  using opt = OptionKey;
  LOG(debug) << MyClass << "() hello " << GetName();

//  fPid          = getpid();
//  LOG(debug) << MyClass << " pid = " << fPid;
  fNCpuCores    = std::thread::hardware_concurrency();
  LOG(debug) << MyClass << " n cpu cores (logical) = " << fNCpuCores;
  fClockTick    = sysconf(_SC_CLK_TCK);
  fPageSize     = sysconf(_SC_PAGESIZE);
  fProcSelfStat = ReadProcSelfStat();
  fProcStat     = ReadProcStat();

  fId          = GetProperty<std::string>("id");
  fServiceName = GetProperty<std::string>(ServiceName.data());
  fSeparator   = GetProperty<std::string>(Separator.data());
  fTopPrefix   = MetricsPrefix.data();


  fRetentionMS = GetProperty<std::string>(opt::Retention.data());
  fMaxTtl      = std::stoll(GetProperty<std::string>(opt::MaxTtl.data()));

  if (PropertyExists("created-time")) {
    auto t = GetProperty<int64_t>("created-time");
    std::chrono::nanoseconds dur(t);
    fCreatedTimeSystem = std::chrono::time_point<std::chrono::system_clock>(dur);
  } else {
    fCreatedTimeSystem = std::chrono::system_clock::now();
  }
  fCreatedTime = std::chrono::steady_clock::now();

  fStateKey        = join({fTopPrefix, StatePrefix.data()},        fSeparator);
  fLastUpdateKey   = join({fTopPrefix, LastUpdatePrefix.data()},   fSeparator);
  fLastUpdateNSKey = join({fTopPrefix, LastUpdateNSPrefix.data()}, fSeparator);
  fProcKey.stateId = join({fTopPrefix, StateIdPrefix.data()},      fSeparator);
  fProcKey.cpu     = join({fTopPrefix, CpuStatPrefix.data()},      fSeparator);
  fProcKey.ram     = join({fTopPrefix, RamStatPrefix.data()},      fSeparator);

  fSockKey.msgIn    = join({fTopPrefix, MessageInPrefix.data()},  fSeparator);
  fSockKey.bytesIn  = join({fTopPrefix, BytesInPrefix.data()},    fSeparator);
  fSockKey.msgOut   = join({fTopPrefix, MessageOutPrefix.data()}, fSeparator);
  fSockKey.bytesOut = join({fTopPrefix, BytesOutPrefix.data()},   fSeparator);

  fSockSumKey       = Append(fSockKey, "sum", "-");

  fNumMessageKey    = join({fTopPrefix, NumMessagePrefix.data()},    fSeparator);
  fBytesKey         = join({fTopPrefix, BytesPrefix.data()},         fSeparator);
  fNumMessageSumKey = join({fTopPrefix, NumMessageSumPrefix.data()}, fSeparator);
  fBytesSumKey      = join({fTopPrefix, BytesSumPrefix.data()},      fSeparator);


  auto t     = ReplaceAll(fProcKey, std::string(fTopPrefix)+fSeparator.data(), "");
  fTsProcKey = Prepend(t, join({"ts", fId}, fSeparator), fSeparator);

  /* 
  LOG(debug) << " StateKey       = " << fStateKey 
             << "\n LastUpdateKey     = " << fLastUpdateKey
             << "\n LastUpdateNSKey   = " << fLastUpdateNSKey
             << "\n"
             << "\n ProcKey.stateId   = " << fProcKey.stateId
             << "\n ProcKey.cpu       = " << fProcKey.cpu
             << "\n ProcKey.ram       = " << fProcKey.ram
             << "\n"
             << "\n SockKey.msgIn     = " << fSockKey.msgIn
             << "\n SockKey.bytesIn   = " << fSockKey.bytesIn
             << "\n SockKey.msgOut    = " << fSockKey.msgOut
             << "\n SockKey.bytesOut  = " << fSockKey.bytesOut
             << "\n"
             << "\n SockSumKey.msgIn     = " << fSockSumKey.msgIn
             << "\n SockSumKey.bytesIn   = " << fSockSumKey.bytesIn
             << "\n SockSumKey.msgOut    = " << fSockSumKey.msgOut
             << "\n SockSumKey.bytesOut  = " << fSockSumKey.bytesOut
             << "\n"
             << "\n fNumMessageKey    = " << fNumMessageKey
             << "\n fBytesKey         = " << fBytesKey
             << "\n fNuMMessageSumKey = " << fNumMessageSumKey
             << "\n fBytesSumKey      = " << fBytesSumKey
             << "\n"
             << "\n TsProcKey.stateId   = " << fTsProcKey.stateId
             << "\n TsProcKey.cpu       = " << fTsProcKey.cpu
             << "\n PTsrocKey.ram       = " << fTsProcKey.ram;
  */

  std::string serverUri;
  if (PropertyExists(opt::ServerUri.data())) {
    serverUri = GetProperty<std::string>(opt::ServerUri.data());
  } else if (PropertyExists(ServiceRegistryUri.data())) {
    serverUri = GetProperty<std::string>(ServiceRegistryUri.data());
  }
  if (!serverUri.empty()) {
    fClient = std::make_shared<sw::redis::Redis>(serverUri);
  }

  const auto fCreatedTimeKey = join({fTopPrefix, CreatedTimePrefix.data()},   fSeparator);
  const auto fHostNameKey    = join({fTopPrefix, HostnamePrefix.data()},      fSeparator);
  const auto fIpAddressKey   = join({fTopPrefix, HostIpAddressPrefix.data()}, fSeparator);

  //LOG(debug) << " createdTimeKey = " << fCreatedTimeKey
  //           << "\n hostnameKey    = " << fHostnameKey
  //           << "\n ipAddresssKey  = " << fIpAddressKey;

  fStartTimeKey   = join({fTopPrefix, StartTime.data()}, fSeparator);
  fStartTimeNSKey = join({fTopPrefix, StartTimeNS.data()}, fSeparator);
  fStopTimeKey    = join({fTopPrefix, StopTime.data()}, fSeparator);
  fStopTimeNSKey  = join({fTopPrefix, StopTimeNS.data()}, fSeparator);
  fRunNumberKey   = join({fTopPrefix, RunNumber.data()}, fSeparator);

  fRegisteredKeys.insert({fStateKey, fLastUpdateKey, fLastUpdateNSKey,
                          fStartTimeKey, fStartTimeNSKey, fStopTimeKey, fStopTimeNSKey, 
                          fRunNumberKey,  
                          fProcKey.stateId, fProcKey.cpu, fProcKey.ram,
                          fCreatedTimeKey, fHostNameKey, fIpAddressKey});
  //for (auto k : fRegisteredKeys) {
  //  LOG(debug) << " key = " << k;
  //}
  fRegisteredSockKeys.insert({fSockKey.msgIn, fSockKey.bytesIn, fSockKey.msgOut, fSockKey.bytesOut, 
                             fSockSumKey.msgIn, fSockSumKey.bytesIn, fSockSumKey.msgOut, fSockSumKey.bytesOut,
                             fNumMessageKey, fBytesKey, fNumMessageSumKey, fBytesSumKey});
  
  fPipe = std::make_unique<sw::redis::Pipeline>(std::move(fClient->pipeline()));
  if (fMaxTtl>0) {
    DeleteExpiredFields();
  }

  {
    //const auto &[uptimeNSec, lastUpdate] = update_date(fCreatedTimeSystem, fCreatedTime); 
    //auto lastUpdateNS = std::chrono::duration_cast<std::chrono::nanoseconds>(lastUpdate.time_since_epoch());
    std::lock_guard<std::mutex> lock{fMutex};
    fPipe->hset(fCreatedTimeKey, fId, to_date(fCreatedTimeSystem))
          .hset(fHostNameKey,    fId, GetProperty<std::string>("hostname"))
          .hset(fIpAddressKey,   fId, GetProperty<std::string>("host-ip"))
          //.hset(fLastUpdateKey, fId, to_date(lastUpdate))
          //.hset(fLastUpdateNSKey, fId, std::to_string(lastUpdateNS.count()))
          .exec(); 
  }
  fair::Logger::AddCustomSink(MyClass.data(), "info", [this](const std::string &content, const fair::LogMetaData &metadata) {
    std::lock_guard<std::mutex> lock{fMutex};
    SendSocketMetrics(content);
  });

  SubscribeToPropertyChangeAsString([this](const std::string& key, std::string value) {
    if (
      (key==StartTime)   || 
      (key==StartTimeNS) ||
      (key==StopTime)    || 
      (key==StopTimeNS)  ||
      (key==RunNumber)) {
      //LOG(debug) << MyClass << " (subscribed callback) key = " << key << ", value = " << value;
        std::lock_guard<std::mutex> lock{fMutex};
        fClient->hset(join({fTopPrefix, key}, fSeparator), fId, value);
        
      }
  });

  SubscribeToDeviceStateChange([this](DeviceState newState) {
    auto pipelineUsed{false};
    const auto stateName = GetStateName(newState);
    LOG(debug) << MyClass << " state change: " << stateName; 
    {
      std::lock_guard<std::mutex> lock{fMutex};
      if (fPipe) {
        fPipe->discard();        
        fPipe->hset(fStateKey,        fId, stateName)
              .hset(fProcKey.stateId, {std::make_pair(fId, static_cast<int>(newState))})
              .exec();
        pipelineUsed = true;
      }
    }
    switch (newState) {
      case DeviceState::DeviceReady: 
        InitializeSocketProperties();
        break;
      case DeviceState::Ready: 
        {
          if (IsRecreateTS()) {
            DeleteTSKeys();
          }
          fSocketMetrics.clear();
          fNumChannels.clear();
          break;
        }
      case DeviceState::Running:
        if (IsRecreateTS()) {
          pipelineUsed |= CreateTimeseries(fTsProcKey.cpu,     {{DataType.data(), CpuStatPrefix.data()}});
          pipelineUsed |= CreateTimeseries(fTsProcKey.ram,     {{DataType.data(), RamStatPrefix.data()}});
          pipelineUsed |= CreateTimeseries(fTsProcKey.stateId, {{DataType.data(), StateIdPrefix.data()}});
          pipelineUsed |= CreateSocketTS();
          if (pipelineUsed) {
            fPipe->exec();
          }
        }
        break;
      default: break;
    }
  });

}

//_____________________________________________________________________________
daq::service::MetricsPlugin::~MetricsPlugin()
{

  UnsubscribeFromDeviceStateChange();
  UnsubscribeFromPropertyChangeAsString();
  fair::Logger::RemoveCustomSink(MyClass.data());
  LOG(debug) << MyClass << "UnsubscribeFromDeviceStateChange()";
  //fContext->stop();
  //if (fTimerThread.joinable()) {
  //  fTimerThread.join();
  //  LOG(debug) << MyClass << " timer thread joined.";
  //}
  if (fPipe) {
    fPipe.reset();
  }
  LOG(debug) << "~" << MyClass << "() bye";
}

//_____________________________________________________________________________
bool daq::service::MetricsPlugin::CreateSocketTS(std::string_view keyMsg,  
                                                 std::string_view keyBytes,
                                                 std::string_view labelMsg,
                                                 std::string_view labelBytes,
                                                 const std::unordered_map<std::string, std::string>& labels)
{
  //LOG(warn) << __func__ << ":" << __LINE__;
  bool pipelineUsed=false;
  
  auto keyMsgSum     = join({keyMsg.data(),     "sum"}, "-");
  auto keyBytesSum   = join({keyBytes.data(),   "sum"}, "-");
  auto labelMsgSum   = join({labelMsg.data(),   "sum"}, "-");
  auto labelBytesSum = join({labelBytes.data(), "sum"}, "-");
  
  auto labelsMsg      = labels;
  auto labelsBytes    = labels;
  auto labelsMsgSum   = labels;
  auto labelsBytesSum = labels;

  //LOG(debug) << __func__ << ":"
  //           << "\n keyMsgSum     = " << keyMsgSum
  //           << "\n keyBytesSum   = " << keyBytesSum
  //           << "\n labelMsgSum   = " << labelMsgSum
  //           << "\n labelBytesSum = " << labelBytesSum;

  labelsMsg.emplace(DataType.data(),      labelMsg);  
  labelsBytes.emplace(DataType.data(),    labelBytes);  
  labelsMsgSum.emplace(DataType.data(),   labelMsgSum);  
  labelsBytesSum.emplace(DataType.data(), labelBytesSum);  
  pipelineUsed |= CreateTimeseries(keyMsg,      labelsMsg);
  pipelineUsed |= CreateTimeseries(keyBytes,    labelsBytes);
  pipelineUsed |= CreateTimeseries(keyMsgSum,   labelsMsgSum);
  pipelineUsed |= CreateTimeseries(keyBytesSum, labelsBytesSum);
  return pipelineUsed;
}

//_____________________________________________________________________________
bool daq::service::MetricsPlugin::CreateSocketTS()
{
  //LOG(warn) << __func__ << ":" << __LINE__;
  bool pipelineUsed=false;
  for (const auto &[name, property] : fSocketProperties) {
    auto hasInput  = (property.type!="push") && (property.type!="pub");
    auto hasOutput = (property.type!="pull") && (property.type!="sub");
    if (!hasInput && !hasOutput) {
      continue;
    }
    const auto prefix = join({"ts", fId, name}, fSeparator);
    auto t = ReplaceAll(fSockKey, std::string(fTopPrefix)+fSeparator.data(), "");
    auto tsKey = Prepend(t, prefix, fSeparator);
    fTsSockKey[name]    = tsKey;
    auto sumKey = Append(tsKey, "sum", "-");
    fTsSockSumKey[name] =  sumKey;
    
    //std::string s{" socket TS keys for "};
    //s += name + "\n";
    //s += " " + tsKey.msgIn  + ", " + tsKey.bytesIn  + ", " + tsKey.msgOut  + ", " + tsKey.bytesOut + "\n";
    //s += " " + sumKey.msgIn + ", " + sumKey.bytesIn + ", " + sumKey.msgOut + ", " + sumKey.bytesOut;
    //LOG(debug) << MyClass << s;
    
    std::unordered_map<std::string, std::string> labels{{"name",     property.name}, 
                                                        {"socket",    property.type},
                                                        {"transport", property.transport}};
    if (hasInput) {
      pipelineUsed |= CreateSocketTS(tsKey.msgIn, tsKey.bytesIn, MessageInPrefix, BytesInPrefix, labels); 
    }
    if (hasOutput) {
      pipelineUsed |= CreateSocketTS(tsKey.msgOut, tsKey.bytesOut, MessageOutPrefix, BytesOutPrefix, labels);
    }
  }
  return pipelineUsed;
}

//_____________________________________________________________________________
bool daq::service::MetricsPlugin::CreateTimeseries(std::string_view key, 
                                                   const std::unordered_map<std::string, std::string> &labels)
{
  //LOG(warn) << __func__ << ":" << __LINE__;
  if (fClient->exists(key.data())>0) {
    //LOG(warn) << " TS key = " << key << " already exists in DB";
    fClient->del(key.data());
    fRegisteredTSKeys.erase(key.data());
  }
  std::vector<std::string> cmd;
  cmd.push_back("ts.create");
  cmd.push_back(key.data());
  cmd.push_back("retention");
  cmd.push_back(fRetentionMS);
  cmd.push_back("labels");
  cmd.push_back("service");
  cmd.push_back(fServiceName);
  cmd.push_back("id");
  cmd.push_back(fId);
  for (const auto& [k, v] : labels) {
    cmd.push_back(k);
    cmd.push_back(v);
  }

  //std::string s{" create time series data:\n"};
  //for (const auto& x : cmd) {
  //  s += " " + x + "\n";
  //}
  //LOG(debug) << s;

  fPipe->command(cmd.cbegin(), cmd.cend());
  fRegisteredTSKeys.emplace(key.data()); 
  return true;
}

//_____________________________________________________________________________
void daq::service::MetricsPlugin::DeleteExpiredFields()
{
  // RedLock transaction version
  sw::redis::RedMutex mtx(*fClient, "metrics");
  sw::redis::RedLock<sw::redis::RedMutex> redLock(mtx, std::defer_lock);
  while (true) {
    if (redLock.try_lock(std::chrono::milliseconds(30000))) {
      LOG(debug) << "got lock: " << MyClass << " " << fId;

      std::unordered_map<std::string, std::string> hashInstanceToLastUpdateNS;
      fClient->hgetall(fLastUpdateNSKey, std::inserter(hashInstanceToLastUpdateNS, hashInstanceToLastUpdateNS.begin()));
      std::vector<std::string> expiredInstances;
      for (const auto& [k, v] : hashInstanceToLastUpdateNS) {
        auto tNS = std::stoull(v); // nanoseconds -> milliseconds
        auto tNow = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if ((tNow - tNS)/1e6 > fMaxTtl) {
          expiredInstances.push_back(k);
        }
      }

      if (!expiredInstances.empty()) {
        for (const auto &k : fRegisteredKeys) {
          LOG(debug) << __func__ << ":" << __LINE__ << " delete " << k;
          fPipe->hdel(k, expiredInstances.begin(), expiredInstances.end());
        }
      
        std::unordered_map<std::string, std::string> sockets;
        for (const auto &k : fRegisteredSockKeys) {
          fClient->hgetall(k, std::inserter(sockets, sockets.begin()));
          std::vector<std::string> a;
          for (const auto &instName : expiredInstances) {
            for (const auto &[sockName, v] : sockets) {
              if (sockName.find(instName) == 0) {
                LOG(debug) << __func__ << ":" << __LINE__ << " delete " << k << " " << sockName;
                fPipe->hdel(k, sockName);
              }
            }
          }
        }
      }
      fPipe->exec();
      if (redLock.owns_lock()) {
        LOG(debug) << "unlock: " << MyClass << " " << fId;
        break;
      } else {
        std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }
}

//_____________________________________________________________________________
void daq::service::MetricsPlugin::DeleteTSKeys()
{
  if (!fRegisteredTSKeys.empty()) {
    auto ndeleted = fClient->del(fRegisteredTSKeys.cbegin(), fRegisteredTSKeys.cend());
    fRegisteredTSKeys.clear();  
    LOG(debug) << MyClass << " " << __FUNCTION__ << " n deleted = " << ndeleted;
  }
}

//_____________________________________________________________________________
void daq::service::MetricsPlugin::InitializeSocketProperties()
{
  // Get parameters of channel configuration as std::map<sstd::tring, std::1string>
  const auto properties = GetPropertiesAsStringStartingWith("chans.");
  fSocketProperties.clear();
  for (const auto& [k, v] : properties) {
    std::vector<std::string> c;
    boost::split(c, k, boost::is_any_of("."), boost::token_compress_on);
    // k = chans.<channel-name>.<subchannel-index>.<field>
    if (c.size()<4) {
      LOG(error) << " invalid channel property : key = " << k << ", value = " << v;
      continue;
    }
    auto name  = c[1] + "[" + c[2] + "]";
    const auto &field = c[3];
    auto &p = fSocketProperties[name];
    if (p.name.empty()) {
      p.name = name;
    } else if (field=="type") {
      p.type = v;
    } else if (field=="method") {
      p.method = v;
    } else if (field=="address") {
      p.address = v;
    } else if (field=="transport") {
      p.transport = v;
    } else if (field=="sndBufSize") {
      p.sndBufSize = std::stoi(v);
    } else if (field=="rcvBufSize") {
      p.rcvBufSize = std::stoi(v); 
    } else if (field=="sndKernelSize") {
      p.sndKernelSize = std::stoi(v);
    } else if (field=="rcvKernelSize") {
      p.rcvKernelSize = std::stoi(v);
    } else if (field=="linger") {
      p.linger = std::stoi(v);
    } else if (field=="rateLogging") {
      p.rateLogging = std::stoi(v);
    } else if (field=="portRangeMin") {
      p.portRangeMin = std::stoi(v);
    } else if (field=="portRangeMax") {
      p.portRangeMax = std::stoi(v);
    } else if (field=="autoBind") {
      p.autoBind = (v=="true") || (v=="1");
    }
  }
}

//_____________________________________________________________________________
bool daq::service::MetricsPlugin::IsRecreateTS()
{
  //LOG(warn) << __func__ << ":" << __LINE__;
  using opt = OptionKey;
  if (PropertyExists(opt::RecreateTS.data())) {
    auto f = GetProperty<std::string>(opt::RecreateTS.data());
    boost::to_lower(f);
    return (f=="true") || (f=="1");
  }
  //LOG(warn) << __func__ << ":" << __LINE__;
  return false;
}

//_____________________________________________________________________________
daq::service::ProcSelfStat_t daq::service::MetricsPlugin::ReadProcSelfStat()
{
  if (!fProcSelfStatFile.is_open()) {
    fProcSelfStatFile.open("/proc/self/stat");
  }
  std::string s;
  std::vector<std::string> v;
  std::getline(fProcSelfStatFile, s);
  // clear iostate flag and seek to the beginning of the file
  fProcSelfStatFile.clear();
  fProcSelfStatFile.seekg(0);
  boost::split(v, s, boost::is_space()); //, boost::token_compress_on); 

//  LOG(debug) << "/proc/self/stat \n" << s;
//  LOG(debug) << " v.size() = " << v.size();
//  LOG(debug) << v[Utime] << " " << v[Stime] << " " << v[Vsize] << " " << v[Rss];

  ProcSelfStat_t ret;
  ret.utime = std::stoull(v[Utime]);
  ret.stime = std::stoull(v[Stime]);
  ret.vsize = std::stoull(v[Vsize]);
  ret.rss   = std::stoull(v[Rss]);

  return ret;
}

//_____________________________________________________________________________
daq::service::ProcStat_t daq::service::MetricsPlugin::ReadProcStat()
{
  //LOG(debug) << MyClass << " " << __FUNCTION__;
  if (!fProcStatFile.is_open()) {
    fProcStatFile.open("/proc/stat");
  }
  std::string s;
  while (std::getline(fProcStatFile, s)) {
    if (s.find("cpu ")==0) {
      break;
    }
  }
  // clear iostate flag and seek to the beginning of the file
  fProcStatFile.clear(); 
  fProcStatFile.seekg(0);

  std::vector<std::string> v;
  boost::trim_if(s, boost::is_space()); 
  boost::split(v, s, boost::is_space(), boost::token_compress_on); 
//  LOG(debug) << "/proc/stat \n" << s;
//  LOG(debug) << " v.size() = " << v.size();
//  LOG(debug) << " User = " << User << ", Nice = " << Nice << ", System = " << System << ", Idle = " << Idle;
//  for (auto i=0; i<v.size(); ++i) {
//    LOG(debug) << " i = " << i << " " << v[i];
//  }
//  LOG(debug) << v[User] << " " << v[Nice] << " " << v[System] << " " << v[Idle];
  ProcStat_t ret; 
  ret.user   = std::stoull(v[User]);
  ret.nice   = std::stoull(v[Nice]); 
  ret.system = std::stoull(v[System]);
  ret.idle   = std::stoull(v[Idle]);

  return ret;
}

//_____________________________________________________________________________
void daq::service::MetricsPlugin::SendProcessMetrics()
{
  //LOG(debug) << MyClass << " " << __FUNCTION__;

  auto nowProcSelfStat = ReadProcSelfStat();
  auto nowProcStat     = ReadProcStat();

  auto diffSelf = nowProcSelfStat.sum() - fProcSelfStat.sum();
  auto diffAll  = nowProcStat.sum()     - fProcStat.sum();

  // cpu usage in percent
  auto cpuUsage = static_cast<double>(diffSelf)/diffAll * fNCpuCores * fClockTick;
  // memory usage in MiB
  auto ramUsage = static_cast<double>(nowProcSelfStat.rss) * fPageSize / 1024/1024;


//  LOG(debug) << " diff (self) = " << diffSelf
//             << ", diff (all) = " << diffAll << "\n"
//             << "cpu = " << cpuUsage 
//             << ", memory = " << ramUsage;

  fProcSelfStat = nowProcSelfStat;
  fProcStat     = nowProcStat;
  auto stateId  = static_cast<int>(GetCurrentDeviceState());

  const auto &[uptimeNSec, lastUpdate] = update_date(fCreatedTimeSystem, fCreatedTime); 
  auto lastUpdateNS = std::chrono::duration_cast<std::chrono::nanoseconds>(lastUpdate.time_since_epoch());
  try {
    if (fPipe) {
      fPipe->hset(fProcKey.cpu, {std::make_pair(fId, cpuUsage)})
            .hset(fProcKey.ram, {std::make_pair(fId, ramUsage)})
            .hset(fLastUpdateKey, fId, to_date(lastUpdate))
            .hset(fLastUpdateNSKey, fId, std::to_string(lastUpdateNS.count()))
            .command("ts.add", fTsProcKey.cpu,        "*", std::to_string(cpuUsage))
            .command("ts.add", fTsProcKey.ram,        "*", std::to_string(ramUsage))  
            .command("ts.add", fTsProcKey.stateId,    "*", std::to_string(stateId));
        //std::cout << " "   << fTsProcKey.cpu       << "\t " << cpuUsage
        //          << "\n " << fTsProcKey.ram       << "\t " << ramUsage
        //          << "\n " << fTsProcKey.stateId   << "\t " << stateId << std::endl;
    }
  } catch (const std::exception& e) {
    LOG(error) << MyClass << " " << __FUNCTION__ << " exception : what() " << e.what();
  } catch (...) {
    LOG(error) << MyClass << " " << __FUNCTION__ << " exception : unknown ";
  }
  //LOG(debug) << MyClass << " " << __FUNCTION__ << " done";
}

//_____________________________________________________________________________
void daq::service::MetricsPlugin::SendSocketMetrics(const std::string &content) 
{
  //LOG(debug) << MyClass << " " << __FUNCTION__;
  //return;
  //std::cout << MyClass << " content = \n" << content << "\n length = " << content.size() << std::endl;
  if ((content.find(" in: ")==std::string::npos)  ||
      (content.find(" out: ")==std::string::npos) || 
      (content.find("[")==std::string::npos)      || 
      (content.find("]:")==std::string::npos)) {
    return;
  }
  //std::cout << MyClass << " " << __FUNCTION__ << " (passed) content = \n" << content << std::endl;

  // pattern :  _channel_[_index_]: in: _msg-in_ (_bytes-in_ MB) out: _msg-out_ (_bytes-out_ MB)
  // targets are surrounded by "()"
  // search results:
  //               (1 )  (2  )        (3          )   (4          )           (5          )   (6          )
  std::regex  r{R"((.*)\[(\d+)\]: in: ([\d.eE\-+]+) \(([\d.eE\-+]+) MB\) out: ([\d.eE\-+]+) \(([\d.eE\-+]+) MB\))"};

  std::smatch m;
  std::regex_search(content, m, r);
  //std::cout << " m.size() = " << m.size() << std::endl;
  if (m.size() < NSocketMetricsRegexIndex) {
    std::cout << " too few number of metrics" << m.size() << std::endl;
    return;
  }

  auto channelName     = m[Channel].str();
  boost::trim_if(channelName, boost::is_space());
  auto subChannelIndex = m[SubChannelIndex].str();
  boost::trim_if(subChannelIndex, boost::is_space());
  auto subChannelName  = channelName + "[" + subChannelIndex + "]";
  auto channelId       = join({fId, subChannelName}, fSeparator);
  auto i=0;
  //for (auto itr = m.begin(); itr!=m.end(); ++itr) {
  //  std::cout << __LINE__<< " " << itr->str() << " " << m[i++].str() << std::endl;
  //}


  SocketMetrics now;
  now.msgIn    = std::stod(m[NumMessageIn].str());
  now.msgOut   = std::stod(m[NumMessageOut].str());
  // mega bytes  
  now.bytesIn  = std::stod(m[BytesIn].str());
  now.bytesOut = std::stod(m[BytesOut].str());

  auto& sum = fSocketMetrics[subChannelName];
  sum.msgIn    += now.msgIn;
  sum.msgOut   += now.msgOut;
  sum.bytesIn  += now.bytesIn;
  sum.bytesOut += now.bytesOut;

  //std::cout << __LINE__ << " " << channelName << " (sum) in = " << sum.msgIn << " " << sum.bytesIn << " MB, out = " << sum.msgOut << " " << sum.bytesOut << " MB" << std::endl;
  auto msgIn     = static_cast<uint64_t>(std::nearbyint(now.msgIn));  
  auto msgOut    = static_cast<uint64_t>(std::nearbyint(now.msgOut));  

  auto msgInSum  = static_cast<uint64_t>(std::nearbyint(sum.msgIn));
  auto msgOutSum = static_cast<uint64_t>(std::nearbyint(sum.msgOut));

    

  try {
    if (fPipe) {
      const auto &socketTypeKey = join({"chans", channelName, subChannelIndex, "type"},  ".");
      // std::cout << " channel type key = " << socketTypeKey << std::endl;
      std::string socketType;
      if (PropertyExists(socketTypeKey)) {
        socketType = GetProperty<std::string>(socketTypeKey);
      } else {
        return;
      }
      bool hasInput  = (socketType!="push") && (socketType!="pub");
      bool hasOutput = (socketType!="pull") && (socketType!="sub");
      if (!hasInput && !hasOutput) {
        return;
      }
     
      // LOG(debug) << " subChannelName = " << subChannelName;
     
      const auto tsKey       = fTsSockKey[subChannelName];
      const auto tsSumKey    = fTsSockSumKey[subChannelName];
      
      if (hasInput) {
        fPipe->hset(fSockKey.msgIn,       {std::make_pair(channelId, msgIn)})
              .hset(fSockKey.bytesIn,     {std::make_pair(channelId, now.bytesIn)})  // mega bytes
              .hset(fSockSumKey.msgIn,    {std::make_pair(channelId, msgInSum)})
              .hset(fSockSumKey.bytesIn,  {std::make_pair(channelId, sum.bytesIn)})  // mega bytes
              .hset(fNumMessageKey,       {std::make_pair(channelId+".in",  msgIn)}) 
              .hset(fBytesKey,            {std::make_pair(channelId+".in",  now.bytesIn)})
              .hset(fNumMessageSumKey,    {std::make_pair(channelId+".in",  msgInSum)})
              .hset(fBytesSumKey,         {std::make_pair(channelId+".in",  sum.bytesIn)})
              .command("ts.add", tsKey.msgIn,         "*", std::to_string(msgIn))
              .command("ts.add", tsKey.bytesIn,       "*", std::to_string(now.bytesIn))
              .command("ts.add", tsSumKey.msgIn,      "*", std::to_string(msgInSum))
              .command("ts.add", tsSumKey.bytesIn,    "*", std::to_string(sum.bytesIn));
        //std::cout << __LINE__ << " has input: "
        //          << tsKey.msgIn       << "\t " << msgIn
        //          << "\n " << tsKey.bytesIn     << "\t " << now.bytesIn
        //          << "\n " << tsSumKey.msgIn    << "\t " << msgInSum
        //          << "\n " << tsSumKey.bytesIn  << "\t " << sum.bytesIn << std::endl;
      }    
  
      if (hasOutput) {
        fPipe->hset(fSockKey.msgOut,      {std::make_pair(channelId, msgOut)})
              .hset(fSockKey.bytesOut,    {std::make_pair(channelId, now.bytesOut)}) // mega bytes
              .hset(fSockSumKey.msgOut,   {std::make_pair(channelId, msgOutSum)})
              .hset(fSockSumKey.bytesOut, {std::make_pair(channelId, sum.bytesOut)}) // mega bytes
              .hset(fNumMessageKey,       {std::make_pair(channelId+".out", msgOut)})
              .hset(fBytesKey,            {std::make_pair(channelId+".out", now.bytesOut)})
              .hset(fNumMessageSumKey,    {std::make_pair(channelId+".out", msgOutSum)})
              .hset(fBytesSumKey,         {std::make_pair(channelId+".out", sum.bytesOut)})
              .command("ts.add", tsKey.msgOut,         "*", std::to_string(msgOut))
              .command("ts.add", tsKey.bytesOut,       "*", std::to_string(now.bytesOut))
              .command("ts.add", tsSumKey.msgOut,      "*", std::to_string(msgOutSum))
              .command("ts.add", tsSumKey.bytesOut,    "*", std::to_string(sum.bytesOut));
        //std::cout << __LINE__ << " has output: "
        //          << tsKey.msgOut      << "\t " << msgOut
        //          << "\n " << tsKey.bytesOut    << "\t " << now.bytesOut
        //          << "\n " << tsSumKey.msgOut   << "\t " << msgOutSum
        //          << "\n " << tsSumKey.bytesOut << "\t " << sum.bytesOut << std::endl;
  
      }

      auto &count = fNumChannels[subChannelName]; 
      if (count==0) {
        ++count;
      }
      auto countAll = 0;
      for (const auto &[k, v] : fNumChannels) {
        countAll += v;
      }
      if (countAll==fSocketMetrics.size()) {
        SendProcessMetrics();
        fPipe->exec();
        fNumChannels.clear();
      } 
    } //else {
      //std::cout << __LINE__ << " no pipeline is created " << std::endl;
    //}
  } catch (const std::exception &e) {
    LOG(error) << MyClass << " " << __FUNCTION__ << " exception : what() = " << e.what();
  } catch (...) {
    LOG(error) << MyClass << " " << __FUNCTION__ << " exception : unknown";
  }
    
  LOG(debug) << MyClass << " " << __FUNCTION__ << " done";
}
