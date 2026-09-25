#pragma once

/**
 * @file MetricsPlugin.h
 * @brief FairMQ plugin that publishes device and channel metrics to Redis.
 */

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <boost/asio.hpp>

#include <fairmq/Plugin.h>

#include "plugins/Timer.h"
#include "plugins/TopologyData.h"

// forward declaration
namespace sw::redis {
class Redis;
template <typename Impl> class QueuedRedis;
class PipelineImpl;
using Pipeline = QueuedRedis<PipelineImpl>;
}

namespace nestdaq::daq::service {

static constexpr std::string_view kMetricsPrefix{"metrics"};
static constexpr std::string_view kStatePrefix{"state"};
static constexpr std::string_view kStateIdPrefix{"state-id"};
static constexpr std::string_view kCpuStatPrefix{"cpu-stat"};
static constexpr std::string_view kRamStatPrefix{"ram-stat"};

static constexpr std::string_view kMessageInPrefix{"msg-in"};
static constexpr std::string_view kBytesInPrefix{"mb-in"};
static constexpr std::string_view kMessageOutPrefix{"msg-out"};
static constexpr std::string_view kBytesOutPrefix{"mb-out"};

static constexpr std::string_view kNumMessagePrefix{"num-msg"};
static constexpr std::string_view kBytesPrefix{"mb"};
static constexpr std::string_view kNumMessageSumPrefix{"num-msg-sum"};
static constexpr std::string_view kBytesSumPrefix{"mb-sum"};

static constexpr std::string_view kCreatedTimePrefix{"created-time"};
static constexpr std::string_view kLastUpdatePrefix{"last-update"};
static constexpr std::string_view kLastUpdateNsPrefix{"last-update-ns"};

static constexpr std::string_view kHostnamePrefix{"hostname"};
static constexpr std::string_view kHostIpAddressPrefix{"host-ip"};

/** @brief RedisTimeSeries label names attached to socket metric series. */
static constexpr std::string_view kDataType{"data"};
static constexpr std::string_view kSocketName{"name"};
static constexpr std::string_view kSocketType{"socket"};
static constexpr std::string_view kSocketTransport{"transport"};
static constexpr std::string_view kSocketMethod{"method"};

/**
 * @brief Process CPU sample used to compute CPU usage between timer ticks.
 */
struct ProcessUsageSample {
    double cpu_seconds{0.0};
    std::chrono::steady_clock::time_point timestamp;
};

/**
 * @brief Per-socket throughput values parsed from FairMQ rate log lines.
 */
struct SocketMetrics {
    double msg_in{0};
    double msg_out{0};
    double bytes_in{0};
    double bytes_out{0};
};

/** @brief Redis hash and time-series keys for process-level metrics. */
struct ProcessStatKey {
    std::string cpu;
    std::string ram;
    std::string state_id;
};

/** @brief Redis hash and time-series keys for one socket metric group. */
struct SocketMetricsKey {
    std::string msg_in;
    std::string msg_out;
    std::string bytes_in;
    std::string bytes_out;
};

/**
 * @brief FairMQ plugin that exports process and socket metrics to Redis.
 *
 * The plugin samples CPU/RSS on a timer and parses FairMQ throughput log lines
 * for channel metrics. It can also manage RedisTimeSeries keys when the Redis
 * module is available.
 */
class MetricsPlugin : public fair::mq::Plugin
{
public:
    using work_guard_t = net::executor_work_guard<net::io_context::executor_type>;

    /** @brief Command-line option names for metrics plugin configuration. */
    struct OptionKey {
        static constexpr std::string_view kUpdateInterval{"proc-stat-update-interval"};
        static constexpr std::string_view kServerUri{"metrics-uri"};
        static constexpr std::string_view kRetention{"retention"};
        static constexpr std::string_view kRecreateTs{"recreate-ts"};
        static constexpr std::string_view kMaxTtl{"metrics-max-ttl"};
    };

    /** @brief Construct and initialize the Redis-backed metrics plugin. */
    MetricsPlugin(std::string_view name,
                  const fair::mq::Plugin::Version &version,
                  std::string_view maintainer,
                  std::string_view homepage,
                  fair::mq::PluginServices *pluginServices);
    MetricsPlugin(const MetricsPlugin&) = delete;
    MetricsPlugin& operator=(const MetricsPlugin&) = delete;
    MetricsPlugin(MetricsPlugin&&) = delete;
    MetricsPlugin& operator=(MetricsPlugin&&) = delete;
    ~MetricsPlugin() override;

private:
    /** @brief Create RedisTimeSeries entries for one socket metric pair. */
    bool createSocketTs(std::string_view key_msg,
                        std::string_view key_bytes,
                        std::string_view label_msg,
                        std::string_view label_bytes,
                        const std::unordered_map<std::string, std::string> &labels);
    /** @brief Create all configured socket RedisTimeSeries entries. */
    bool createSocketTs();
    /** @brief Create one RedisTimeSeries key with labels and retention. */
    bool createTimeseries(std::string_view key,
                          const std::unordered_map<std::string, std::string> &labels);
    /** @brief Remove stale hash fields whose update timestamp exceeded max TTL. */
    void deleteExpiredFields();
    /** @brief Delete RedisTimeSeries keys owned by this metrics instance. */
    void deleteTsKeys();
    /** @brief Load socket metadata from topology keys for metric labels. */
    void initializeSocketProperties();
    /** @brief Return whether time-series keys should be recreated on startup. */
    bool isRecreateTs();
    /** @brief Read process user/system CPU time for usage deltas. */
    ProcessUsageSample readProcessUsage() const;
    /** @brief Read resident memory in MiB from `/proc/self/stat`. */
    double readResidentMemoryMiB() const;
    /** @brief Publish process CPU, RSS, and FairMQ state metrics to Redis. */
    void sendProcessMetrics();
    /** @brief Parse and publish socket throughput metrics from a FairMQ log line. */
    void sendSocketMetrics(const std::string &content);

    //pid_t fPid;
    std::string fId;
    std::unordered_map<std::string, SocketMetrics> fSocketMetrics;
    ProcessUsageSample fProcessUsage;
    long fPageSize;

    std::unique_ptr<work_guard_t> fWorkGuard;
    std::shared_ptr<net::io_context> fContext;
    std::unique_ptr<Timer> fTimer;
    std::thread fTimerThread;

    // milliseconds
    static constexpr long long kDefaultUpdateIntervalMs{1000};
    long long fUpdateInterval{kDefaultUpdateIntervalMs};
    long long fMaxTtl{0};

    std::string fStartTimeKey;
    std::string fStartTimeNsKey;
    std::string fStopTimeKey;
    std::string fStopTimeNsKey;
    std::string fRunNumberKey;

    std::chrono::system_clock::time_point fCreatedTimeSystem;
    std::chrono::steady_clock::time_point fCreatedTime;
    std::string fCreatedTimeKey;
    std::string fHostNameKey;
    std::string fIpAddressKey;

    std::mutex fMutex;
    std::shared_ptr<sw::redis::Redis> fClient;
    std::unique_ptr<sw::redis::Pipeline> fPipe;
    std::string fSeparator;
    std::string fServiceName;
    std::string fTopPrefix;

    // keys for hash (displayed in table)
    ProcessStatKey fProcKey;
    std::string fStateKey;
    std::string fLastUpdateKey;
    std::string fLastUpdateNsKey;

    SocketMetricsKey fSockKey;
    SocketMetricsKey fSockSumKey;
    std::string fNumMessageKey;
    std::string fBytesKey;
    std::string fNumMessageSumKey;
    std::string fBytesSumKey;

    // keys for time series data
    ProcessStatKey   fTsProcKey;

    std::unordered_map<std::string, SocketProperty> fSocketProperties;
    std::unordered_map<std::string, SocketMetricsKey> fTsSockKey;
    std::unordered_map<std::string, SocketMetricsKey> fTsSockSumKey;
    std::unordered_map<std::string, int> fNumChannels;
    std::string fRetentionMs{"0"};
    std::unordered_set<std::string> fRegisteredTsKeys;
    std::unordered_set<std::string> fRegisteredKeys;
    std::unordered_set<std::string> fRegisteredSockKeys;
};

/**
 * @brief Declare metrics plugin command-line options.
 */
auto metricsPluginProgramOptions() -> fair::mq::Plugin::ProgOptions;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
REGISTER_FAIRMQ_PLUGIN(
    MetricsPlugin,
    metrics,
(fair::mq::Plugin::Version{0, 0, 0}),
"Metrics <maintainer@daq.service.net>",
"https://github.com/spadi-alliance/nestdaq",
nestdaq::daq::service::metricsPluginProgramOptions
) // end of macro: REGISTER_FAIRMQ_PLUGIN

} // namespace nestdaq::daq::service
