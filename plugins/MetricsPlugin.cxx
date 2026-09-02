/** @file
 *  @brief Implements the DAQ metrics collection plugin.
 */

#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <system_error>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <fairlogger/Logger.h>

#include <sw/redis++/redis++.h>
#include <sw/redis++/patterns/redlock.h>
#include <sw/redis++/errors.h>

#include "plugins/Constants.h"
#include "plugins/Functions.h"
#include "plugins/TimeUtil.h"
#include "plugins/MetricsPlugin.h"
#include "nestdaq/telemetry/FairMQThroughputLogParser.h"

static constexpr std::string_view kMyClass{"nestdaq::daq::service::MetricsPlugin"};

using namespace std::string_literals;

namespace {

auto timevalToSeconds(const timeval &value) -> double
{
    static constexpr auto kMicrosecondsPerSecond = 1'000'000.0;
    return static_cast<double>(value.tv_sec) + (static_cast<double>(value.tv_usec) / kMicrosecondsPerSecond);
}

} // namespace

namespace nestdaq::daq::service {

ProcessStatKey append(const ProcessStatKey& input, std::string_view s, std::string_view separator)
{
    ProcessStatKey ret;
    ret.cpu     = join({input.cpu,     s.data()}, separator.data());
    ret.ram     = join({input.ram,     s.data()}, separator.data());
    ret.state_id = join({input.state_id, s.data()}, separator.data());
    return ret;
}

SocketMetricsKey append(const SocketMetricsKey &input, std::string_view s, std::string_view separator)
{
    SocketMetricsKey ret;
    ret.msg_in    = join({input.msg_in,    s.data()}, separator.data());
    ret.msg_out   = join({input.msg_out,   s.data()}, separator.data());
    ret.bytes_in  = join({input.bytes_in,  s.data()}, separator.data());
    ret.bytes_out = join({input.bytes_out, s.data()}, separator.data());
    return ret;
}

ProcessStatKey prepend(const ProcessStatKey& input, std::string_view s, std::string_view separator)
{
    ProcessStatKey ret;
    ret.cpu     = join({s.data(), input.cpu},     separator.data());
    ret.ram     = join({s.data(), input.ram},     separator.data());
    ret.state_id = join({s.data(), input.state_id}, separator.data());
    return ret;
}

SocketMetricsKey prepend(const SocketMetricsKey& input, std::string_view s, std::string_view separator)
{
    SocketMetricsKey ret;
    ret.msg_in    = join({s.data(), input.msg_in},    separator.data());
    ret.msg_out   = join({s.data(), input.msg_out},   separator.data());
    ret.bytes_in  = join({s.data(), input.bytes_in},  separator.data());
    ret.bytes_out = join({s.data(), input.bytes_out}, separator.data());
    return ret;
}

ProcessStatKey replaceAll(const ProcessStatKey& input, std::string_view search, std::string format)
{
    ProcessStatKey ret;
    ret.cpu     = boost::replace_all_copy(input.cpu,     search.data(), format.data());
    ret.ram     = boost::replace_all_copy(input.ram,     search.data(), format.data());
    ret.state_id = boost::replace_all_copy(input.state_id, search.data(), format.data());
    return ret;
}

SocketMetricsKey replaceAll(const SocketMetricsKey& input, std::string_view search, std::string format)
{
    SocketMetricsKey ret;
    ret.msg_in    = boost::replace_all_copy(input.msg_in,    search.data(), format.data());
    ret.msg_out   = boost::replace_all_copy(input.msg_out,   search.data(), format.data());
    ret.bytes_in  = boost::replace_all_copy(input.bytes_in,  search.data(), format.data());
    ret.bytes_out = boost::replace_all_copy(input.bytes_out, search.data(), format.data());
    return ret;
}

auto metricsPluginProgramOptions() -> fair::mq::Plugin::ProgOptions
{
    namespace bpo = boost::program_options;
    using opt = MetricsPlugin::OptionKey;
    LOG(debug) << "nestdaq::daq::service::metricsPluginProgramOptions: add_options";

    auto options = bpo::options_description(kMyClass.data());
    options.add_options()
           (opt::kUpdateInterval.data(), bpo::value<long long>()->default_value(1000),     "update interval in milliseconds for CPU and memory usage.")
           (opt::kServerUri.data(),      bpo::value<std::string>(),                        "Redis server URI (if empty, the same URI of the service registry is used.)")
           (opt::kRetention.data(),      bpo::value<std::string>()->default_value("0"),    "kRetention time in msec for time series data. When set to 0, the series is not trimmed at all.")
           (opt::kRecreateTs.data(),     bpo::value<std::string>()->default_value("true"), "Recreate timeseries data on state transition to Running")
           (opt::kMaxTtl.data(),         bpo::value<std::string>()->default_value("3000"), "Max TTL for metrics in milliseconds. (if zero or negative, no TTL is set.)");
    return options;
}

MetricsPlugin::MetricsPlugin(std::string_view name,
                             const fair::mq::Plugin::Version &version,
                             std::string_view maintainer,
                             std::string_view homepage,
                             fair::mq::PluginServices *pluginServices)
    : fair::mq::Plugin(name.data(), version, maintainer.data(), homepage.data(), pluginServices)
{
    using opt = OptionKey;
    LOG(debug) << kMyClass << "() hello " << GetName();

//  fPid          = getpid();
//  LOG(debug) << kMyClass << " pid = " << fPid;
    fPageSize     = sysconf(_SC_PAGESIZE);
    fProcessUsage = readProcessUsage();

    fId          = GetProperty<std::string>("id");
    fServiceName = GetProperty<std::string>(kServiceName.data());
    fSeparator   = GetProperty<std::string>(kSeparator.data());
    fTopPrefix   = kMetricsPrefix.data();

    fRetentionMs = GetProperty<std::string>(opt::kRetention.data());
    fMaxTtl      = std::stoll(GetProperty<std::string>(opt::kMaxTtl.data()));

    if (PropertyExists("created-time")) {
        auto t = GetProperty<int64_t>("created-time");
        std::chrono::nanoseconds dur(t);
        fCreatedTimeSystem = std::chrono::time_point<std::chrono::system_clock>(dur);
    } else {
        fCreatedTimeSystem = std::chrono::system_clock::now();
    }
    fCreatedTime = std::chrono::steady_clock::now();

    fStateKey        = join({fTopPrefix, kStatePrefix.data()},        fSeparator);
    fLastUpdateKey   = join({fTopPrefix, kLastUpdatePrefix.data()},   fSeparator);
    fLastUpdateNsKey = join({fTopPrefix, kLastUpdateNsPrefix.data()}, fSeparator);
    fProcKey.state_id = join({fTopPrefix, kStateIdPrefix.data()},      fSeparator);
    fProcKey.cpu     = join({fTopPrefix, kCpuStatPrefix.data()},      fSeparator);
    fProcKey.ram     = join({fTopPrefix, kRamStatPrefix.data()},      fSeparator);

    fSockKey.msg_in    = join({fTopPrefix, kMessageInPrefix.data()},  fSeparator);
    fSockKey.bytes_in  = join({fTopPrefix, kBytesInPrefix.data()},    fSeparator);
    fSockKey.msg_out   = join({fTopPrefix, kMessageOutPrefix.data()}, fSeparator);
    fSockKey.bytes_out = join({fTopPrefix, kBytesOutPrefix.data()},   fSeparator);

    fSockSumKey       = append(fSockKey, "sum", "-");

    fNumMessageKey    = join({fTopPrefix, kNumMessagePrefix.data()},    fSeparator);
    fBytesKey         = join({fTopPrefix, kBytesPrefix.data()},         fSeparator);
    fNumMessageSumKey = join({fTopPrefix, kNumMessageSumPrefix.data()}, fSeparator);
    fBytesSumKey      = join({fTopPrefix, kBytesSumPrefix.data()},      fSeparator);

    auto t     = replaceAll(fProcKey, std::string(fTopPrefix)+fSeparator.data(), "");
    fTsProcKey = prepend(t, join({"ts", fId}, fSeparator), fSeparator);

    /*
    LOG(debug) << " StateKey       = " << fStateKey
               << "\n LastUpdateKey     = " << fLastUpdateKey
               << "\n LastUpdateNSKey   = " << fLastUpdateNsKey
               << "\n"
               << "\n ProcKey.state_id   = " << fProcKey.state_id
               << "\n ProcKey.cpu       = " << fProcKey.cpu
               << "\n ProcKey.ram       = " << fProcKey.ram
               << "\n"
               << "\n SockKey.msg_in     = " << fSockKey.msg_in
               << "\n SockKey.bytes_in   = " << fSockKey.bytes_in
               << "\n SockKey.msg_out    = " << fSockKey.msg_out
               << "\n SockKey.bytes_out  = " << fSockKey.bytes_out
               << "\n"
               << "\n SockSumKey.msg_in     = " << fSockSumKey.msg_in
               << "\n SockSumKey.bytes_in   = " << fSockSumKey.bytes_in
               << "\n SockSumKey.msg_out    = " << fSockSumKey.msg_out
               << "\n SockSumKey.bytes_out  = " << fSockSumKey.bytes_out
               << "\n"
               << "\n fNumMessageKey    = " << fNumMessageKey
               << "\n fBytesKey         = " << fBytesKey
               << "\n fNuMMessageSumKey = " << fNumMessageSumKey
               << "\n fBytesSumKey      = " << fBytesSumKey
               << "\n"
               << "\n TsProcKey.state_id   = " << fTsProcKey.state_id
               << "\n TsProcKey.cpu       = " << fTsProcKey.cpu
               << "\n PTsrocKey.ram       = " << fTsProcKey.ram;
    */

    std::string server_uri;
    if (PropertyExists(opt::kServerUri.data())) {
        server_uri = GetProperty<std::string>(opt::kServerUri.data());
    } else if (PropertyExists(kServiceRegistryUri.data())) {
        server_uri = GetProperty<std::string>(kServiceRegistryUri.data());
    }
    if (!server_uri.empty()) {
        fClient = std::make_shared<sw::redis::Redis>(server_uri);
    }

    fCreatedTimeKey = join({fTopPrefix, kCreatedTimePrefix.data()},   fSeparator);
    fHostNameKey    = join({fTopPrefix, kHostnamePrefix.data()},      fSeparator);
    fIpAddressKey   = join({fTopPrefix, kHostIpAddressPrefix.data()}, fSeparator);

    //LOG(debug) << " createdTimeKey = " << fCreatedTimeKey
    //           << "\n hostnameKey    = " << fHostnameKey
    //           << "\n ipAddresssKey  = " << fIpAddressKey;

    fStartTimeKey   = join({fTopPrefix, kStartTime.data()}, fSeparator);
    fStartTimeNsKey = join({fTopPrefix, kStartTimeNs.data()}, fSeparator);
    fStopTimeKey    = join({fTopPrefix, kStopTime.data()}, fSeparator);
    fStopTimeNsKey  = join({fTopPrefix, kStopTimeNs.data()}, fSeparator);
    fRunNumberKey   = join({fTopPrefix, kRunNumber.data()}, fSeparator);

    fRegisteredKeys.insert({fStateKey, fLastUpdateKey, fLastUpdateNsKey,
                            fStartTimeKey, fStartTimeNsKey, fStopTimeKey, fStopTimeNsKey,
                            fRunNumberKey,
                            fProcKey.state_id, fProcKey.cpu, fProcKey.ram,
                            fCreatedTimeKey, fHostNameKey, fIpAddressKey});
    //for (auto k : fRegisteredKeys) {
    //  LOG(debug) << " key = " << k;
    //}
    fRegisteredSockKeys.insert({fSockKey.msg_in, fSockKey.bytes_in, fSockKey.msg_out, fSockKey.bytes_out,
                                fSockSumKey.msg_in, fSockSumKey.bytes_in, fSockSumKey.msg_out, fSockSumKey.bytes_out,
                                fNumMessageKey, fBytesKey, fNumMessageSumKey, fBytesSumKey});

    fPipe = std::make_unique<sw::redis::Pipeline>(std::move(fClient->pipeline()));
    if (fMaxTtl>0) {
        deleteExpiredFields();
    }

    {
        //const auto &[uptimeNSec, lastUpdate] = updateDate(fCreatedTimeSystem, fCreatedTime);
        //auto last_update_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(lastUpdate.time_since_epoch());
        std::scoped_lock<std::mutex> lock{fMutex};
        fPipe->hset(fCreatedTimeKey, fId, toDate(fCreatedTimeSystem))
        .hset(fHostNameKey,    fId, GetProperty<std::string>("hostname"))
        .hset(fIpAddressKey,   fId, GetProperty<std::string>("host-ip"))
        //.hset(fLastUpdateKey, fId, toDate(lastUpdate))
        //.hset(fLastUpdateNsKey, fId, std::to_string(last_update_ns.count()))
        .exec();
    }
    fair::Logger::AddCustomSink(kMyClass.data(), "info", [this](const std::string &content, const fair::LogMetaData & /*metadata*/) {
        std::scoped_lock<std::mutex> lock{fMutex};
        sendSocketMetrics(content);
    });

    SubscribeToPropertyChangeAsString([this](const std::string& key, std::string value) {
        if (
            (key==kStartTime)   ||
            (key==kStartTimeNs) ||
            (key==kStopTime)    ||
            (key==kStopTimeNs)  ||
            (key==kRunNumber)) {
            //LOG(debug) << kMyClass << " (subscribed callback) key = " << key << ", value = " << value;
            std::scoped_lock<std::mutex> lock{fMutex};
            fClient->hset(join({fTopPrefix, key}, fSeparator), fId, value);

        }
    });

    SubscribeToDeviceStateChange([this](DeviceState newState) {
        auto pipeline_used{false};
        const auto kStateName = GetStateName(newState);
        LOG(debug) << kMyClass << " state change: " << kStateName;
        {
            std::scoped_lock<std::mutex> lock{fMutex};
            if (fPipe) {
                fPipe->discard();
                fPipe->hset(fStateKey,        fId, kStateName)
                .hset(fProcKey.state_id, {std::make_pair(fId, static_cast<int>(newState))})
                .exec();
                pipeline_used = true;
            }
        }
        switch (newState) {
        case DeviceState::DeviceReady:
            initializeSocketProperties();
            break;
        case DeviceState::Ready:
        {
            if (isRecreateTs()) {
                deleteTsKeys();
            }
            fSocketMetrics.clear();
            fNumChannels.clear();
            break;
        }
        case DeviceState::Running:
            if (isRecreateTs()) {
                pipeline_used |= createTimeseries(fTsProcKey.cpu,     {{kDataType.data(), kCpuStatPrefix.data()}});
                pipeline_used |= createTimeseries(fTsProcKey.ram,     {{kDataType.data(), kRamStatPrefix.data()}});
                pipeline_used |= createTimeseries(fTsProcKey.state_id, {{kDataType.data(), kStateIdPrefix.data()}});
                pipeline_used |= createSocketTs();
                if (pipeline_used) {
                    fPipe->exec();
                }
            }
            break;
        default:
            break;
        }
    });

}

MetricsPlugin::~MetricsPlugin()
{

    UnsubscribeFromDeviceStateChange();
    UnsubscribeFromPropertyChangeAsString();
    fair::Logger::RemoveCustomSink(kMyClass.data());
    LOG(debug) << kMyClass << "UnsubscribeFromDeviceStateChange()";
    //fContext->stop();
    //if (fTimerThread.joinable()) {
    //  fTimerThread.join();
    //  LOG(debug) << kMyClass << " timer thread joined.";
    //}
    if (fPipe) {
        fPipe.reset();
    }
    LOG(debug) << "~" << kMyClass << "() bye";
}

/**
 * @brief Create RedisTimeSeries keys for one socket direction and its sum.
 */
bool MetricsPlugin::createSocketTs(std::string_view key_msg,
                                   std::string_view key_bytes,
                                   std::string_view label_msg,
                                   std::string_view label_bytes,
                                   const std::unordered_map<std::string, std::string>& labels)
{
    //LOG(warn) << __func__ << ":" << __LINE__;
    bool pipeline_used=false;

    auto key_msg_sum     = join({key_msg.data(),     "sum"}, "-");
    auto key_bytes_sum   = join({key_bytes.data(),   "sum"}, "-");
    auto label_msg_sum   = join({label_msg.data(),   "sum"}, "-");
    auto label_bytes_sum = join({label_bytes.data(), "sum"}, "-");

    auto labels_msg      = labels;
    auto labels_bytes    = labels;
    auto labels_msg_sum   = labels;
    auto labels_bytes_sum = labels;

    //LOG(debug) << __func__ << ":"
    //           << "\n key_msg_sum     = " << key_msg_sum
    //           << "\n key_bytes_sum   = " << key_bytes_sum
    //           << "\n label_msg_sum   = " << label_msg_sum
    //           << "\n label_bytes_sum = " << label_bytes_sum;

    labels_msg.emplace(kDataType.data(),      label_msg);
    labels_bytes.emplace(kDataType.data(),    label_bytes);
    labels_msg_sum.emplace(kDataType.data(),   label_msg_sum);
    labels_bytes_sum.emplace(kDataType.data(), label_bytes_sum);
    pipeline_used |= createTimeseries(key_msg,      labels_msg);
    pipeline_used |= createTimeseries(key_bytes,    labels_bytes);
    pipeline_used |= createTimeseries(key_msg_sum,   labels_msg_sum);
    pipeline_used |= createTimeseries(key_bytes_sum, labels_bytes_sum);
    return pipeline_used;
}

/**
 * @brief Create RedisTimeSeries keys for all configured FairMQ sockets.
 */
bool MetricsPlugin::createSocketTs()
{
    //LOG(warn) << __func__ << ":" << __LINE__;
    bool pipeline_used=false;
    for (const auto &[name, property] : fSocketProperties) {
        auto has_input  = (property.type!="push") && (property.type!="pub");
        auto has_output = (property.type!="pull") && (property.type!="sub");
        if (!has_input && !has_output) {
            continue;
        }
        const auto kPrefix = join({"ts", fId, name}, fSeparator);
        auto t = replaceAll(fSockKey, std::string(fTopPrefix)+fSeparator.data(), "");
        auto ts_key = prepend(t, kPrefix, fSeparator);
        fTsSockKey[name]    = ts_key;
        auto sum_key = append(ts_key, "sum", "-");
        fTsSockSumKey[name] =  sum_key;

        //std::string s{" socket TS keys for "};
        //s += name + "\n";
        //s += " " + ts_key.msg_in  + ", " + ts_key.bytes_in  + ", " + ts_key.msg_out  + ", " + ts_key.bytes_out + "\n";
        //s += " " + sum_key.msg_in + ", " + sum_key.bytes_in + ", " + sum_key.msg_out + ", " + sum_key.bytes_out;
        //LOG(debug) << kMyClass << s;

        std::unordered_map<std::string, std::string> labels{{"name",     property.name},
            {"socket",    property.type},
            {"transport", property.transport}};
        if (has_input) {
            pipeline_used |= createSocketTs(ts_key.msg_in, ts_key.bytes_in, kMessageInPrefix, kBytesInPrefix, labels);
        }
        if (has_output) {
            pipeline_used |= createSocketTs(ts_key.msg_out, ts_key.bytes_out, kMessageOutPrefix, kBytesOutPrefix, labels);
        }
    }
    return pipeline_used;
}

/**
 * @brief Queue creation of one RedisTimeSeries key with standard labels.
 */
bool MetricsPlugin::createTimeseries(std::string_view key,
                                     const std::unordered_map<std::string, std::string> &labels)
{
    //LOG(warn) << __func__ << ":" << __LINE__;
    if (fClient->exists(key.data())>0) {
        //LOG(warn) << " TS key = " << key << " already exists in DB";
        fClient->del(key.data());
        fRegisteredTsKeys.erase(key.data());
    }
    std::vector<std::string> cmd;
    cmd.push_back("ts.create");
    cmd.push_back(key.data());
    cmd.push_back("retention");
    cmd.push_back(fRetentionMs);
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
    fRegisteredTsKeys.emplace(key.data());
    return true;
}

/**
 * @brief Delete stale Redis hash fields for service instances past the metrics TTL.
 */
void MetricsPlugin::deleteExpiredFields()
{
    while (true) {
        try {
            sw::redis::RedMutex mtx(fClient, "metrics");
            std::unique_lock<sw::redis::RedMutex> red_lock(mtx, std::defer_lock);
            if (red_lock.try_lock()) {
                LOG(debug) << "got lock: " << kMyClass << " " << fId;

                std::unordered_map<std::string, std::string> hash_instance_to_last_update_ns;
                fClient->hgetall(fLastUpdateNsKey, std::inserter(hash_instance_to_last_update_ns, hash_instance_to_last_update_ns.begin()));
                std::vector<std::string> expired_instances;
                for (const auto& [k, v] : hash_instance_to_last_update_ns) {
                    auto t_ns = std::stoull(v); // nanoseconds -> milliseconds
                    auto t_now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    if ((t_now - t_ns)/1e6 > fMaxTtl) {
                        expired_instances.push_back(k);
                    }
                }

                if (!expired_instances.empty()) {
                    for (const auto &k : fRegisteredKeys) {
                        LOG(debug) << __func__ << ":" << __LINE__ << " delete " << k;
                        fPipe->hdel(k, expired_instances.begin(), expired_instances.end());
                    }

                    std::unordered_map<std::string, std::string> sockets;
                    for (const auto &k : fRegisteredSockKeys) {
                        fClient->hgetall(k, std::inserter(sockets, sockets.begin()));
                        std::vector<std::string> a;
                        for (const auto &inst_name : expired_instances) {
                            for (const auto &[sockName, v] : sockets) {
                                if (sockName.find(inst_name) == 0) {
                                    LOG(debug) << __func__ << ":" << __LINE__ << " delete " << k << " " << sockName;
                                    fPipe->hdel(k, sockName);
                                }
                            }
                        }
                    }
                }
                fPipe->exec();
                if (red_lock.owns_lock()) {
                    LOG(debug) << "unlock: " << kMyClass << " " << fId;
                    break;
                } else {
                    std::this_thread::yield();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        } catch (const sw::redis::Error& e) {
            LOG(error) << " caught exception (redis++) : " << e.what();
        } catch (const std::exception& e) {
            LOG(error) << " caught exception (std) : " << e.what();
        } catch (...) {
            LOG(error) << " caught exception : unknown";
        }
    }
}

/**
 * @brief Delete RedisTimeSeries keys created by this plugin instance.
 */
void MetricsPlugin::deleteTsKeys()
{
    if (!fRegisteredTsKeys.empty()) {
        auto ndeleted = fClient->del(fRegisteredTsKeys.cbegin(), fRegisteredTsKeys.cend());
        fRegisteredTsKeys.clear();
        LOG(debug) << kMyClass << " " << __FUNCTION__ << " n deleted = " << ndeleted;
    }
}

/**
 * @brief Read FairMQ channel properties and cache per-socket metadata.
 */
void MetricsPlugin::initializeSocketProperties()
{
    // Get parameters of channel configuration as std::map<sstd::tring, std::1string>
    const auto kProperties = GetPropertiesAsStringStartingWith("chans.");
    fSocketProperties.clear();
    for (const auto& [k, v] : kProperties) {
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
            p.snd_buf_size = std::stoi(v);
        } else if (field=="rcvBufSize") {
            p.rcv_buf_size = std::stoi(v);
        } else if (field=="sndKernelSize") {
            p.snd_kernel_size = std::stoi(v);
        } else if (field=="rcvKernelSize") {
            p.rcv_kernel_size = std::stoi(v);
        } else if (field=="linger") {
            p.linger = std::stoi(v);
        } else if (field=="rateLogging") {
            p.rate_logging = std::stoi(v);
        } else if (field=="portRangeMin") {
            p.port_range_min = std::stoi(v);
        } else if (field=="portRangeMax") {
            p.port_range_max = std::stoi(v);
        } else if (field=="autoBind") {
            p.auto_bind = (v=="true") || (v=="1");
        }
    }
}

/**
 * @brief Check whether time-series keys should be recreated when running starts.
 */
bool MetricsPlugin::isRecreateTs()
{
    //LOG(warn) << __func__ << ":" << __LINE__;
    using opt = OptionKey;
    if (PropertyExists(opt::kRecreateTs.data())) {
        auto f = GetProperty<std::string>(opt::kRecreateTs.data());
        boost::to_lower(f);
        return (f=="true") || (f=="1");
    }
    //LOG(warn) << __func__ << ":" << __LINE__;
    return false;
}

/**
 * @brief Read cumulative CPU time consumed by this process.
 */
auto MetricsPlugin::readProcessUsage() const -> ProcessUsageSample
{
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        const auto kError = std::error_code{errno, std::generic_category()};
        LOG(error) << kMyClass << " " << __FUNCTION__ << " getrusage failed: " << kError.message();
        return {.cpu_seconds = fProcessUsage.cpu_seconds, .timestamp = std::chrono::steady_clock::now()};
    }

    return {
        .cpu_seconds = timevalToSeconds(usage.ru_utime) + timevalToSeconds(usage.ru_stime),
        .timestamp = std::chrono::steady_clock::now(),
    };
}

/**
 * @brief Read resident memory usage of this process in MiB.
 */
auto MetricsPlugin::readResidentMemoryMiB() const -> double
{
    std::ifstream input{"/proc/self/statm"};
    uint64_t total_pages = 0;
    uint64_t resident_pages = 0;
    if (!(input >> total_pages >> resident_pages)) {
        LOG(error) << kMyClass << " " << __FUNCTION__ << " failed to read /proc/self/statm";
        return 0.0;
    }

    static constexpr auto kBytesPerMiB = 1024.0 * 1024.0;
    return static_cast<double>(resident_pages) * static_cast<double>(fPageSize) / kBytesPerMiB;
}

/**
 * @brief Record CPU, memory, state, and last-update metrics.
 */
void MetricsPlugin::sendProcessMetrics()
{
    //std::cout << kMyClass << " " << __FUNCTION__;

    auto now_process_usage = readProcessUsage();

    const auto kCpuSeconds = now_process_usage.cpu_seconds - fProcessUsage.cpu_seconds;
    const auto kWallSeconds =
        std::chrono::duration<double>(now_process_usage.timestamp - fProcessUsage.timestamp).count();

    // Top/htop style percent: one fully used CPU core is 100%, two cores are 200%.
    const auto kCpuUsage = kWallSeconds > 0.0 ? kCpuSeconds / kWallSeconds * 100.0 : 0.0;
    const auto kRamUsage = readResidentMemoryMiB();

//  std::cout << " diff (self) = " << diffSelf
//             << ", diff (all) = " << diffAll << "\n"
//             << "cpu = " << cpu_usage
//             << ", memory = " << ram_usage;

    fProcessUsage = now_process_usage;
    auto state_id = static_cast<int>(GetCurrentDeviceState());

    const auto &[uptime_nsec, last_update] = updateDate(fCreatedTimeSystem, fCreatedTime);
    auto last_update_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(last_update.time_since_epoch());
    try {
        if (fPipe) {
            fPipe->hset(fProcKey.cpu, {std::make_pair(fId, kCpuUsage)})
            .hset(fProcKey.ram, {std::make_pair(fId, kRamUsage)})
            .hset(fLastUpdateKey, fId, toDate(last_update))
            .hset(fLastUpdateNsKey, fId, std::to_string(last_update_ns.count()))
            .command("ts.add", fTsProcKey.cpu,        "*", std::to_string(kCpuUsage))
            .command("ts.add", fTsProcKey.ram,        "*", std::to_string(kRamUsage))
            .command("ts.add", fTsProcKey.state_id,    "*", std::to_string(state_id));
            //std::cout << " "   << fTsProcKey.cpu       << "\t " << cpu_usage
            //          << "\n " << fTsProcKey.ram       << "\t " << ram_usage
            //          << "\n " << fTsProcKey.state_id   << "\t " << state_id << std::endl;
        }
    } catch (const std::exception& e) {
        LOG(error) << kMyClass << " " << __FUNCTION__ << " exception : what() " << e.what();
    } catch (...) {
        LOG(error) << kMyClass << " " << __FUNCTION__ << " exception : unknown ";
    }
    //std::cout << kMyClass << " " << __FUNCTION__ << " done";
}

/**
 * @brief Parse a FairMQ throughput log line and record socket metrics.
 */
void MetricsPlugin::sendSocketMetrics(const std::string &content)
{
    //LOG(debug) << kMyClass << " " << __FUNCTION__;
    //return;
    //std::cout << kMyClass << " content = \n" << content << "\n length = " << content.size() << std::endl;
    const auto kSample = nestdaq::telemetry::parseFairMQThroughputLog(content);
    if (!kSample || !kSample->sub_channel_index) {
        return;
    }
    //std::cout << kMyClass << " " << __FUNCTION__ << " (passed) content = \n" << content << std::endl;

    const auto &channel_name = kSample->channel_name;
    const auto kSubChannelIndex = std::to_string(*kSample->sub_channel_index);
    const auto &sub_channel_name = kSample->sub_channel_name;
    auto channel_id       = join({fId, sub_channel_name}, fSeparator);

    SocketMetrics now;
    now.msg_in    = kSample->messages_per_second_in;
    now.msg_out   = kSample->messages_per_second_out;
    // mega bytes
    now.bytes_in  = kSample->megabytes_per_second_in;
    now.bytes_out = kSample->megabytes_per_second_out;

    auto& sum = fSocketMetrics[sub_channel_name];
    sum.msg_in    += now.msg_in;
    sum.msg_out   += now.msg_out;
    sum.bytes_in  += now.bytes_in;
    sum.bytes_out += now.bytes_out;

    //std::cout << __LINE__ << " " << channel_name << " (sum) in = " << sum.msg_in << " " << sum.bytes_in << " MB, out = " << sum.msg_out << " " << sum.bytes_out << " MB" << std::endl;
    auto msg_in     = static_cast<uint64_t>(std::nearbyint(now.msg_in));
    auto msg_out    = static_cast<uint64_t>(std::nearbyint(now.msg_out));

    auto msg_in_sum  = static_cast<uint64_t>(std::nearbyint(sum.msg_in));
    auto msg_out_sum = static_cast<uint64_t>(std::nearbyint(sum.msg_out));

    try {
        if (fPipe) {
            const auto &socket_type_key = join({"chans", channel_name, kSubChannelIndex, "type"},  ".");
            // std::cout << " channel type key = " << socket_type_key << std::endl;
            std::string socket_type;
            if (PropertyExists(socket_type_key)) {
                socket_type = GetProperty<std::string>(socket_type_key);
            } else {
                return;
            }
            bool has_input  = (socket_type!="push") && (socket_type!="pub");
            bool has_output = (socket_type!="pull") && (socket_type!="sub");
            if (!has_input && !has_output) {
                return;
            }

            // LOG(debug) << " sub_channel_name = " << sub_channel_name;

            const auto kTsKey       = fTsSockKey[sub_channel_name];
            const auto kTsSumKey    = fTsSockSumKey[sub_channel_name];

            if (has_input) {
                fPipe->hset(fSockKey.msg_in,       {std::make_pair(channel_id, msg_in)})
                .hset(fSockKey.bytes_in,     {std::make_pair(channel_id, now.bytes_in)})  // mega bytes
                .hset(fSockSumKey.msg_in,    {std::make_pair(channel_id, msg_in_sum)})
                .hset(fSockSumKey.bytes_in,  {std::make_pair(channel_id, sum.bytes_in)})  // mega bytes
                .hset(fNumMessageKey,       {std::make_pair(channel_id+".in",  msg_in)})
                .hset(fBytesKey,            {std::make_pair(channel_id+".in",  now.bytes_in)})
                .hset(fNumMessageSumKey,    {std::make_pair(channel_id+".in",  msg_in_sum)})
                .hset(fBytesSumKey,         {std::make_pair(channel_id+".in",  sum.bytes_in)})
                .command("ts.add", kTsKey.msg_in,         "*", std::to_string(msg_in))
                .command("ts.add", kTsKey.bytes_in,       "*", std::to_string(now.bytes_in))
                .command("ts.add", kTsSumKey.msg_in,      "*", std::to_string(msg_in_sum))
                .command("ts.add", kTsSumKey.bytes_in,    "*", std::to_string(sum.bytes_in));
                //std::cout << __LINE__ << " has input: "
                //          << ts_key.msg_in       << "\t " << msg_in
                //          << "\n " << ts_key.bytes_in     << "\t " << now.bytes_in
                //          << "\n " << ts_sum_key.msg_in    << "\t " << msg_in_sum
                //          << "\n " << ts_sum_key.bytes_in  << "\t " << sum.bytes_in << std::endl;
            }

            if (has_output) {
                fPipe->hset(fSockKey.msg_out,      {std::make_pair(channel_id, msg_out)})
                .hset(fSockKey.bytes_out,    {std::make_pair(channel_id, now.bytes_out)}) // mega bytes
                .hset(fSockSumKey.msg_out,   {std::make_pair(channel_id, msg_out_sum)})
                .hset(fSockSumKey.bytes_out, {std::make_pair(channel_id, sum.bytes_out)}) // mega bytes
                .hset(fNumMessageKey,       {std::make_pair(channel_id+".out", msg_out)})
                .hset(fBytesKey,            {std::make_pair(channel_id+".out", now.bytes_out)})
                .hset(fNumMessageSumKey,    {std::make_pair(channel_id+".out", msg_out_sum)})
                .hset(fBytesSumKey,         {std::make_pair(channel_id+".out", sum.bytes_out)})
                .command("ts.add", kTsKey.msg_out,         "*", std::to_string(msg_out))
                .command("ts.add", kTsKey.bytes_out,       "*", std::to_string(now.bytes_out))
                .command("ts.add", kTsSumKey.msg_out,      "*", std::to_string(msg_out_sum))
                .command("ts.add", kTsSumKey.bytes_out,    "*", std::to_string(sum.bytes_out));
                //std::cout << __LINE__ << " has output: "
                //          << ts_key.msg_out      << "\t " << msg_out
                //          << "\n " << ts_key.bytes_out    << "\t " << now.bytes_out
                //          << "\n " << ts_sum_key.msg_out   << "\t " << msg_out_sum
                //          << "\n " << ts_sum_key.bytes_out << "\t " << sum.bytes_out << std::endl;

            }

            auto &count = fNumChannels[sub_channel_name];
            if (count==0) {
                ++count;
            }
            std::size_t count_all = 0;
            for (const auto &[k, v] : fNumChannels) {
                count_all += static_cast<std::size_t>(v);
            }
            if (count_all==fSocketMetrics.size()) {
                sendProcessMetrics();
                fPipe->exec();
                fNumChannels.clear();
            }
        } //else {
        //std::cout << __LINE__ << " no pipeline is created " << std::endl;
        //}
    } catch (const std::exception &e) {
        LOG(error) << kMyClass << " " << __FUNCTION__ << " exception : what() = " << e.what();
    } catch (...) {
        LOG(error) << kMyClass << " " << __FUNCTION__ << " exception : unknown";
    }
}

} // namespace nestdaq::daq::service
