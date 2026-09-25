#pragma once

/**
 * @file DaqServicePlugin.h
 * @brief Redis client plugin for DAQ service discovery and command/state data.
 */

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
namespace sw::redis {
class Redis;
}

namespace nestdaq::daq::service {

/**
 * @brief Redis key used to advertise that a DAQ service instance is alive.
 */
struct Presence {
    std::string key;
};

/**
 * @brief Current health record tracked for a DAQ service instance.
 *
 * The service plugin updates this record periodically so controllers can detect
 * stale or disappeared FairMQ devices.
 */
struct Health {
    std::string key;
    std::string host_name;
    std::string ip_address;
    std::chrono::system_clock::time_point created_time_system;
    std::chrono::steady_clock::time_point created_time;
//  std::chrono::system_clock::time_point updatedTime;
};

class TopologyConfig;

/**
 * @brief FairMQ plugin that publishes service presence, health, and state.
 *
 * The plugin owns the Redis connection used by topology, parameter, and metrics
 * helpers. It also subscribes to DAQ command channels and translates commands
 * into FairMQ device state transitions.
 */
class Plugin : public fair::mq::Plugin
{
public:
    using DeviceState = fair::mq::Plugin::DeviceState;
    using work_guard_t = net::executor_work_guard<net::io_context::executor_type>;

    /** @brief Construct and initialize the Redis-backed service plugin. */
    Plugin(std::string_view name,
           const fair::mq::Plugin::Version &version,
           std::string_view maintainer,
           std::string_view homepage,
           fair::mq::PluginServices *pluginServices);
    Plugin(const Plugin &) = delete;
    Plugin& operator=(const Plugin &) = delete;
    Plugin(Plugin&&) = delete;
    Plugin& operator=(Plugin&&) = delete;
    ~Plugin() override;

    /** @brief Return the current health record published for this instance. */
    const Health& getHealth() const {
        return *fHealth;
    }
    /** @brief Return the plugin mutex shared with topology helpers. */
    std::mutex& getMutex() {
        return fMutex;
    }
    /** @brief Return the Redis client used by service-related helpers. */
    std::shared_ptr<sw::redis::Redis> getClient() const {
        return fClient;
    }
    /** @brief Return true when shutdown or reset cancellation was requested. */
    bool isCanceled() const {
        return fResetDeviceRequested || fPluginShutdownRequested;
    }
    /** @brief Return true when a Redis command requested FairMQ RESET DEVICE. */
    bool isResetDeviceRequested() const {
        return fResetDeviceRequested;
    }
    /** @brief Return true when the plugin shutdown sequence has started. */
    bool isShutdownRequested() const {
        return fPluginShutdownRequested;
    }

private:
    /** @brief Execute a multi-step DAQ command sequence such as start or stop. */
    void changeDeviceStateByMultiCommand(std::string_view cmd);
    /** @brief Execute a single FairMQ command received from Redis. */
    void changeDeviceStateBySingleCommand(std::string_view cmd);
    /** @brief Read the current run number from Redis run information. */
    void readRunNumber();
    /** @brief registerService presence, health, state, and option keys in Redis. */
    void registerService();
    /** @brief Refresh TTLs for Redis keys owned by this plugin. */
    void resetTtl();
    /** @brief Run startup registration and initial topology configuration. */
    void runStartupSequence();
    /** @brief Run shutdown cleanup and Redis unregistration. */
    void runShutdownSequence();
    /** @brief Capture the process current working directory for health data. */
    void setCurrentWorkingDirectory();
    /** @brief Resolve the service instance id from options or generated UUID. */
    void setId();
    /** @brief Capture the process name for health data. */
    void setProcessName();
    /** @brief Subscribe to DAQ command messages from Redis pub/sub. */
    void subscribeToDaqCommand();
    /** @brief Remove service keys and subscriptions owned by this plugin. */
    void unregisterService();
    /** @brief Publish FairMQ program options under the service option key. */
    void writeProgOptions();
    /** @brief Publish run start timestamps to Redis. */
    void writeStartTime();
    /** @brief Publish run stop timestamps to Redis. */
    void writeStopTime();

    std::string fSeparator;

    pid_t fPid{0};
    std::string fProcessName;
    std::string fCwd;
    boost::uuids::uuid fUuid{};
    std::string fId; // instance id configured by command line option or uuid
    std::string fServiceName;
    std::shared_ptr<sw::redis::Redis> fClient;
    std::unordered_set<std::string> fRegisteredKeys;
    std::unordered_map<std::string, std::string> fRegisteredHashes;
    //std::string fSeparator;
    std::unique_ptr<Presence> fPresence;
    std::unique_ptr<Health> fHealth;
    std::string fFairMQStateKey;
    std::string fUpdateTimeKey;
    std::string fProgOptionKeyName;
    long long fMaxTtl{0};
    long long fTtlUpdateInterval{0};

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
};

/**
 * @brief Declare DAQ service plugin command-line options.
 */
auto pluginProgramOptions() -> fair::mq::Plugin::ProgOptions;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
REGISTER_FAIRMQ_PLUGIN(
    Plugin,                                      // Class name
    daq_service,                                 // Plugin name (lowercase chars only)
(fair::mq::Plugin::Version{0, 0, 0}),        // Version
"DAQService <maintainer@daq.service.net>",   // Maintainer
"https://github.com/spadi-alliance/nestdaq", // Homepage
nestdaq::daq::service::pluginProgramOptions           // Free function which declares custom program options for the plugin
//    signature: () -> boost::optional<boost::program_options::options_decription>
) // end of macro: REGISTER_FAIRMQ_PLUGIN

} // namespace nestdaq::daq::service
