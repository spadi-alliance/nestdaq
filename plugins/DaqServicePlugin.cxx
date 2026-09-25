/** @file
 *  @brief Implements the DAQ service plugin for publishing device state.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <sw/redis++/redis++.h>
#include <sw/redis++/patterns/redlock.h>
#include <sw/redis++/errors.h>

#include <fairmq/Tools.h>

#include "plugins/TimeUtil.h"
#include "plugins/TopologyConfig.h"
#include "plugins/Constants.h"
#include "plugins/Functions.h"
#include "plugins/tools.h"
#include "plugins/DaqServicePlugin.h"

// valid if _GNU_SOURCE is defined
//extern char *program_invocation_name;
//extern char *program_invocation_short_name;
//extern char *__progname; // same as program_invocation_short_name

static constexpr std::string_view kMyClass{"nestdaq::daq::service::Plugin"};

static constexpr std::string_view kStartupState{"startup-state"};

static constexpr std::string_view kEnableUds{"enable-uds"};
static constexpr std::string_view kConnectConfig{"connect-config"};
static constexpr std::string_view kMaxRetryToResolveAddress{"max-retry-to-resolve-address"};
static constexpr long long kDefaultMaxTtl{5};
static constexpr long long kDefaultTtlUpdateInterval{3};
static constexpr long long kMillisecondsPerSecond{1000};
static constexpr std::size_t kCwdBufferSize{512};
static constexpr std::chrono::milliseconds kRedLockRetryInterval{100};

static const std::unordered_set<std::string_view> kKnownCommandList{
    nestdaq::fairmq::command::kBind,
    nestdaq::fairmq::command::kCompleteInit,
    nestdaq::fairmq::command::kConnect,
    nestdaq::fairmq::command::kEnd,
    nestdaq::fairmq::command::kInitDevice,
    nestdaq::fairmq::command::kInitTask,
    nestdaq::fairmq::command::kResetDevice,
    nestdaq::fairmq::command::kResetTask,
    nestdaq::fairmq::command::kRun,
    nestdaq::fairmq::command::kStop,
    nestdaq::daq::command::kExit,
    nestdaq::daq::command::kQuit,
    nestdaq::daq::command::kReset,
    nestdaq::daq::command::kStart,
};

using namespace std::string_literals;

bool endsWith(const std::string& s, const std::string& suffix)
{
    if (s.size() < suffix.size()) return false;
    return std::equal(std::rbegin(suffix), std::rend(suffix), std::rbegin(s));
}

namespace nestdaq::daq::service {

auto pluginProgramOptions() -> fair::mq::Plugin::ProgOptions
{
    namespace bpo = boost::program_options;

    LOG(debug) << "nestdaq::daq::service::pluginProgramOptions: add_options";
    auto plugin_options = bpo::options_description(std::string{kMyClass});
    plugin_options.add_options() //
                  (std::string{kServiceName}.data(),        bpo::value<std::string>(),  "name of this service")
                  //
                  (std::string{kUuid}.data(),               bpo::value<std::string>(),  "uuid of this service")
                  //
                  (std::string{kHostIpAddress}.data(),      bpo::value<std::string>(),  "IP address or hostname of this service")
                  //
                  (std::string{kHostname}.data(),           bpo::value<std::string>(),  "hostname of this service")
                  //
                  (std::string{kServiceRegistryUri}.data(), bpo::value<std::string>()->default_value("tcp://127.0.0.1:6379/0"), "DAQ service registry's URI")
                  //
                  (std::string{kSeparator}.data(),          bpo::value<std::string>()->default_value(":"), "separator character for key space name")
                  //
                  (std::string{kMaxTtl}.data(),             bpo::value<long long>()->default_value(kDefaultMaxTtl), "max TTL (time-to-live) in second for keys")
                  //
                  (std::string{kTtlUpdateInterval}.data(),  bpo::value<long long>()->default_value(kDefaultTtlUpdateInterval), "TTL update interval in second for keys")
                  //
                  (std::string{kStartupState}.data(),       bpo::value<std::string>()->default_value("idle"),
                   "state on startup. (idle, initializing-device, initialized, bound, device-ready, ready, running)")
                  //
                  (std::string{kEnableUds}.data(),          bpo::value<std::string>()->default_value("true"),
                   "Use Unix Domain Socket for the local IPC if available (bool)")
                  //
                  (std::string{kConnectConfig}.data(),          bpo::value<std::string>(),
                   "MQ channel parameters of JSON string for temporary connection with method=connect\n"
                   " '{ \"my-channel-a\": { parameters-a }, \"my-channel-b\":  { parameters-b } }'\n\n"
                   " NOTE: When using start_device.sh, the JSON string must be enclosed in \\' (backslash + single quote)\n"
                   " \\''{ \"my-channel-a\": { parameters-a }, \"my-channel-b\":  { parameters-b } }'\\'\n\n"
                   " e.g. 1 \n"
                   " '{ \"in\": { \"type\": \"pull\", \"peer\": \"Sampler:out\" } }'\n"
                   " e.g. 2 \n"
                   " '{ \"in\": { \"type\": \"pill\",  \"peer\": \"Sampler-0:out\" } }'\n"
                   " e.g. 3 \n"
                   " '{ \"in\": {\"type\": \"sub\", \"peer\": [ \"Sampler:Sampler-0:out[0]\", \"Sampler:Sampler-1:out[1]\" ] } }'\n"
                   " e.g. 4 \n"
                   " '{ \"in\": {\"type\": \"sub\", \"peer\": \"Sampler:Sampler-0:out[0]\" }, \"out\": { \"type\": \"pub\",  \"peer\": \"Sink:Sink-2:in[1]\" } }'\n")
                  //
                  (std::string{kMaxRetryToResolveAddress}.data(), bpo::value<std::string>()->default_value("10"), "max retry to resolve connect address");

    return plugin_options;
}

Plugin::Plugin(std::string_view name,
               const fair::mq::Plugin::Version& version,
               std::string_view maintainer,
               std::string_view homepage,
               fair::mq::PluginServices *pluginServices)
    : fair::mq::Plugin(std::string{name}, version, std::string{maintainer}, std::string{homepage}, pluginServices)
{
    fUuid = boost::uuids::nil_uuid();

    LOG(debug) << kMyClass << "() hello";
    setCurrentWorkingDirectory();
    setProcessName();

    if (PropertyExists(std::string{kUuid})) {
        fUuid = boost::lexical_cast<boost::uuids::uuid>(GetProperty<std::string>(std::string{kUuid}));
    }
    if (fUuid.is_nil()) {
        fUuid = boost::uuids::random_generator()();
        SetProperty("uuid", boost::uuids::to_string(fUuid));
    }
    LOG(debug) << kMyClass << " uuid = "  << fUuid;

    fSeparator = GetProperty<std::string>(std::string{kSeparator});
    SetProperty("top-prefix", std::string{kTopPrefix});

    fPresence = std::make_unique<Presence>();
    fMaxTtl = GetProperty<long long>(std::string{kMaxTtl});
    fTtlUpdateInterval = GetProperty<long long>(std::string{kTtlUpdateInterval});

    fHealth = std::make_unique<Health>();
    if (PropertyExists(std::string{kHostname})) {
        fHealth->host_name = GetProperty<std::string>(std::string{kHostname});
    } else {
        fHealth->host_name = net::ip::host_name();
    }
    SetProperty(std::string{kHostname}, fHealth->host_name);
    fHealth->created_time_system = std::chrono::system_clock::now();
    fHealth->created_time       = std::chrono::steady_clock::now();

    SetProperty("created-time", std::chrono::duration_cast<std::chrono::nanoseconds>(fHealth->created_time_system.time_since_epoch()).count());

    if (PropertyExists(std::string{kHostIpAddress})) {
        auto ip_address = GetProperty<std::string>(std::string{kHostIpAddress});
        fHealth->ip_address = fair::mq::tools::getIpFromHostname(ip_address);
        auto host_ips = fair::mq::tools::getHostIPs();
        //LOG(debug) << " host ip size = " << host_ips.size();
        for (const auto& [nic, ip] : host_ips) {
            //LOG(debug) << " nic = " << nic << ", ip = " << ip << ", ip_address = " << fHealth->ip_address;
            if (ip==fHealth->ip_address) {
                SetProperty<std::string>("network-interface", nic);
                break;
            }
        }
    } else if (PropertyExists("network-interface")) {
        LOG(debug) << " find my ip address by network-interface";
        const auto kDefaultNic = fair::mq::tools::getDefaultRouteNetworkInterface();
        auto nic = GetProperty<std::string>("network-interface");
        if (nic!="default") {
            fHealth->ip_address = fair::mq::tools::getInterfaceIP(nic);
        }
        if (fHealth->ip_address.empty()) {
            LOG(debug) << " use default route NIC = " << kDefaultNic;
            fHealth->ip_address = fair::mq::tools::getInterfaceIP(kDefaultNic);
            SetProperty<std::string>("network-interface", kDefaultNic);
        }
    }

    LOG(debug) << " ip = " << fHealth->ip_address;
    SetProperty(std::string{kHostIpAddress}, fHealth->ip_address);

    if (PropertyExists(std::string{kServiceName})) {
        fServiceName = GetProperty<std::string>(std::string{kServiceName});
    }
    if (fServiceName.empty()) {
        std::vector<std::string> v;
        boost::split(v, fProcessName, boost::is_any_of("/"));
        LOG(debug) << " service name is empty. use process name (filename of executable) as service name";
        fServiceName = v.back();
        SetProperty(std::string{kServiceName}, fServiceName);
    }
    fStartupState   = GetProperty<std::string>(std::string{kStartupState});

    auto host_ips = fair::mq::tools::getHostIPs();
    for (const auto& [nic, ip] : host_ips) {
        LOG(debug) << " nic = " << nic << ", ip = " << ip;
    }

    try {
        TakeDeviceControl();
        LOG(info) << kMyClass << " succeeded in TakeDeviceControl()";
    } catch (fair::mq::PluginServices::DeviceControlError &e) {
        LOG(error) << kMyClass << "'s constructor : " << e.what();
    } catch (const std::exception &e) {
        LOG(error) << kMyClass << "'s constructor : canneo take device control. " << e.what();
    } catch (...) {
        LOG(error) << kMyClass << "'s constructor : unknwo exception";
    }

    // register to service registry
    registerService();
    fTopology = std::make_unique<TopologyConfig>(*this);
    if (PropertyExists(std::string{kConnectConfig})) {
        fTopology->setConnectConfig(GetProperty<std::string>(std::string{kConnectConfig}));
        fTopology->setMaxRetryToResolveAddress(std::stoi(GetProperty<std::string>(std::string{kMaxRetryToResolveAddress})));
        // for quick debug
        //fTopology->configConnect();
    }

    // register functions
    LOG(warn) << " register GetPeerStateOfBindChannels()";
    SetProperty<std::function<std::map<std::string, std::string>()>>("GetPeerStateOfBindChannels()", [this]() {
        return fTopology->getPeerStateOfBindChannels();
    });

    LOG(warn) << " register GetPeerStaetOfConnectChannels()";
    SetProperty<std::function<std::map<std::string, std::string>()>>("GetPeerStateOfConnectChannels()", [this]() {
        return fTopology->getPeerStateOfConnectChannels();
    });

    LOG(warn) << kMyClass << " SubscribeToDeviceStateChange()";
    SubscribeToDeviceStateChange([this](DeviceState newState) {
        try {
            auto state_name = GetStateName(newState);
            LOG(info) << kMyClass << " state : " << state_name;
            fStateQueue.Push(newState);

            {
                std::scoped_lock<std::mutex> lock{fMutex};
                auto pipe = fClient->pipeline();
                pipe.setex(fFairMQStateKey, fMaxTtl, state_name)
                    .hset(fHealth->key, "fair:mq:state", state_name)
                    .expire(fHealth->key, fMaxTtl);
                pipe.exec();
            }

            writeProgOptions();
            readRunNumber();
            const auto& v = boost::to_lower_copy(GetProperty<std::string>(std::string{kEnableUds}));
            fTopology->enableUds((v=="1") || (v=="true"));
            switch (newState) {
            case DeviceState::Idle:
                fResetDeviceRequested = false;
                break;
            case DeviceState::InitializingDevice:
            case DeviceState::Bound:
            case DeviceState::ResettingDevice:
                fTopology->onDeviceStateChange(newState);
                break;
            case DeviceState::Error:
                fPluginShutdownRequested = true;
            default:
                break;
            }
        } catch (const std::exception &e) {
            LOG(error) << kMyClass << " exception during device state change: " << e.what();
        } catch (...) {
            LOG(error) << kMyClass << " exception during device state change: unknow exception";
        }
    });

    fStateControlThread = std::thread([this]() {
        try {
            runStartupSequence();
            subscribeToDaqCommand();
            runShutdownSequence();
        } catch (const fair::mq::PluginServices::DeviceControlError &e) {
            LOG(error) << kMyClass << " " << e.what();
        } catch (const fair::mq::DeviceErrorState &e) {
            LOG(error) << kMyClass << " " << e.what();
            ReleaseDeviceControl();
        }
    });
    fStateControlThread.detach();

    LOG(debug) << kMyClass << "() done";

}

Plugin::~Plugin()
{
    UnsubscribeFromDeviceStateChange();
    LOG(warn) << kMyClass << " UnsubscribeFromDeviceStateChange()";
    auto state = GetCurrentDeviceState();
    if (state==DeviceState::Exiting) {
        ReleaseDeviceControl();
        LOG(info) << kMyClass << " ReleaseDeviceControl() done";
    }
    fPluginShutdownRequested = true;
//  std::this_thread::sleep_for(std::chrono::microseconds(1000000));
    fContext->stop();

    if (fTimerThread.joinable()) {
        LOG(debug) << " wait for join: timer thread";
        fTimerThread.join();
        LOG(debug) << " timer thread joined";
    }
    if (fStateControlThread.joinable()) {
        LOG(debug) << " wait for join: state control thread";
        fStateControlThread.join();
        LOG(debug) << " state control thread joined";
    }

    if (fTopology) {
        (*fTopology).reset();
    }
    unregisterService();
    LOG(debug) << "~" << kMyClass << "() bye";
}

void Plugin::changeDeviceStateByMultiCommand(std::string_view cmd)
{
    //LOG(debug) << kMyClass << ":" << __func__;
    auto state = GetCurrentDeviceState();
    //auto state_name = GetStateName(state);

    switch (state) {
    // ---------- state transition from Idle ----------
    case DeviceState::Idle:
        // Idle -> InitialzingDevice -> Initialized
        if ((cmd==nestdaq::fairmq::command::kInitDevice) || (cmd==nestdaq::fairmq::command::kCompleteInit)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitDevice);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            break;
        }
        // Idle -> ... -> Bound
        if (cmd==nestdaq::fairmq::command::kBind) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitDevice);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            break;
        }
        // Idle -> ... -> Bound -> Connecting -> DeviceReady
        if (cmd==nestdaq::fairmq::command::kConnect) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitDevice);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            break;
        }
        // Idle -> ... -> DeviceReady -> InitializingTask -> Ready
        if (cmd==nestdaq::fairmq::command::kInitTask) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitDevice);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            break;
        }
        // Idle -> ... -> Ready -> Running I
        if ((cmd==nestdaq::fairmq::command::kRun) || (cmd==nestdaq::daq::command::kStart)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitDevice);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kRun);
            break;
        }
        break;

    // ---------- state transition from InitializingDevice ----------
    case DeviceState::InitializingDevice:
        // InitializingDevice -> Initialized
        if (cmd==nestdaq::fairmq::command::kCompleteInit) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            break;
        }
        // InitializingDevice -> ... -> Bound
        if (cmd==nestdaq::fairmq::command::kBind) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            break;
        }
        // InitializingDevice -> ... -> Bound -> Connecting -> DeviceReady
        if (cmd==nestdaq::fairmq::command::kConnect) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            break;
        }
        // InitializingDevice -> ... -> DeviceReady -> InitializingTask -> Ready
        if (cmd==nestdaq::fairmq::command::kInitTask) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            break;
        }
        // InitializingDevice -> ... -> Ready -> Running
        if ((cmd==nestdaq::fairmq::command::kRun) || (cmd==nestdaq::daq::command::kStart)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kRun);
            break;
        }
        break;

    // ---------- state transition from Initialized ----------
    case DeviceState::Initialized:
        // Initialized -> ... -> Bound
        if (cmd==nestdaq::fairmq::command::kBind) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            break;
        }
        // Initialized -> ... -> Bound -> Connecting -> DeviceReady
        if (cmd==nestdaq::fairmq::command::kConnect) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            break;
        }
        // Initialized -> ... -> DeviceReady -> InitializingTask -> Ready
        if (cmd==nestdaq::fairmq::command::kInitTask) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            break;
        }
        // Initialized -> ... -> Ready -> Running
        if ((cmd==nestdaq::fairmq::command::kRun) || (cmd==nestdaq::daq::command::kStart)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kRun);
            break;
        }

        // Initialized -> ResettingDevice -> Idle
        if ((cmd==nestdaq::fairmq::command::kResetDevice) || (cmd==nestdaq::daq::command::kReset)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kResetDevice);
            break;
        }
        break;

    // ---------- state transition from Bound ----------
    case DeviceState::Bound:
        // Bound -> Connecting -> DeviceReady
        if (cmd==nestdaq::fairmq::command::kConnect) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            break;
        }
        // Bound -> ... -> DeviceReady -> InitializingTask -> Ready
        if (cmd==nestdaq::fairmq::command::kInitTask) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            break;
        }
        // Bound -> ... -> Ready -> Running
        if ((cmd==nestdaq::fairmq::command::kRun) || (cmd==nestdaq::daq::command::kStart)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kRun);
            break;
        }

        // Bound -> ResettingDevice -> Idle
        if ((cmd==nestdaq::fairmq::command::kResetDevice) || (cmd==nestdaq::daq::command::kReset)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kResetDevice);
            break;
        }
        break;

    // ---------- state transition from DeviceReady ----------
    case DeviceState::DeviceReady:
        // DeviceReady -> InitializingTask -> Ready
        if (cmd==nestdaq::fairmq::command::kInitTask) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            break;
        }
        // DeviceReady -> ... -> Ready -> Running
        if ((cmd==nestdaq::fairmq::command::kRun) || (cmd==nestdaq::daq::command::kStart)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kRun);
            break;
        }

        // DeviceReady -> ResettingDevice -> Idle
        if ((cmd==nestdaq::fairmq::command::kResetDevice) || (cmd==nestdaq::daq::command::kReset)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kResetDevice);
            break;
        }
        break;

    // ---------- state transition from Ready ----------
    case DeviceState::Ready:
        // Ready -> Running
        if ((cmd==nestdaq::fairmq::command::kRun) || (cmd==nestdaq::daq::command::kStart)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kRun);
            break;
        }

        // Ready -> ResettingDevice -> DeviceReady
        if (cmd==nestdaq::fairmq::command::kResetTask) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kResetTask);
            break;
        }
        // Ready -> ResettingTask -> DeviceReady -> ResettingDevice -> Idle
        if ((cmd==nestdaq::fairmq::command::kResetDevice) || (cmd==nestdaq::daq::command::kReset)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kResetTask);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kResetDevice);
            break;
        }
        break;

    // ---------- state transition from Running ----------
    case DeviceState::Running:
        // Running -> Ready
        if (cmd==nestdaq::fairmq::command::kStop) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kStop);
            break;
        }

        // Running -> Ready -> ResettingTask -> DeviceReady
        if (cmd==nestdaq::fairmq::command::kResetTask) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kStop);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kResetTask);
            break;
        }
        // Running -> ... -> ResettingDevice -> Idle
        if ((cmd==nestdaq::fairmq::command::kResetDevice) || (cmd==nestdaq::daq::command::kReset)) {
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kStop);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kResetTask);
            changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kResetDevice);
            break;
        }
        break;

    default: // do nothing
        break;
    }
    //LOG(debug) << kMyClass << ":" << __func__ << " done";

}

void Plugin::changeDeviceStateBySingleCommand(std::string_view cmd)
{
    //LOG(debug) << kMyClass << ":" << __func__;
    auto state = GetCurrentDeviceState();
    //auto state_name = GetStateName(state);

    switch (state) {
    case DeviceState::Idle:
        // Idle -> InitializingDevice
        if (cmd==nestdaq::fairmq::command::kInitDevice) {
            ChangeDeviceState(DeviceStateTransition::InitDevice);
            while (fStateQueue.WaitForNext() != DeviceState::InitializingDevice) {}
        }
        break;
    case DeviceState::InitializingDevice:
        // InitializingDevice -> Initialized
        if (cmd==nestdaq::fairmq::command::kCompleteInit) {
            ChangeDeviceState(DeviceStateTransition::CompleteInit);
            while (fStateQueue.WaitForNext() != DeviceState::Initialized) {}
        }
        break;
    case DeviceState::Initialized:
        // Initialized -> Binding -> Bound
        if (cmd==nestdaq::fairmq::command::kBind) {
            ChangeDeviceState(DeviceStateTransition::Bind);
            while (fStateQueue.WaitForNext() != DeviceState::Bound) {}
        }
        // Initialized -> RessetingDevice -> Idle
        if (cmd==nestdaq::fairmq::command::kResetDevice) {
            fResetDeviceRequested = true;
            ChangeDeviceState(DeviceStateTransition::ResetDevice);
            while (fStateQueue.WaitForNext() != DeviceState::Idle) {}
        }
        break;
    case DeviceState::Bound:
        // Bound -> Connecting -> DeviceReady
        if (cmd==nestdaq::fairmq::command::kConnect) {
            ChangeDeviceState(DeviceStateTransition::Connect);
            while (fStateQueue.WaitForNext() != DeviceState::DeviceReady) {}
        }
        // Bound -> RessetingDevice -> Idle
        if (cmd==nestdaq::fairmq::command::kResetDevice) {
            fResetDeviceRequested = true;
            ChangeDeviceState(DeviceStateTransition::ResetDevice);
            while (fStateQueue.WaitForNext() != DeviceState::Idle) {}
        }
        break;
    case DeviceState::Connecting:
        if (cmd==nestdaq::fairmq::command::kResetDevice) {
            fResetDeviceRequested = true;
        }
        break;
    case DeviceState::DeviceReady:
        // DeviceReady -> InitializingTask -> Ready
        if (cmd==nestdaq::fairmq::command::kInitTask) {
            ChangeDeviceState(DeviceStateTransition::InitTask);
            while (fStateQueue.WaitForNext() != DeviceState::Ready) {}
        }
        // DeviceReady -> RessetingDevice -> Idle
        if (cmd==nestdaq::fairmq::command::kResetDevice) {
            fResetDeviceRequested = true;
            ChangeDeviceState(DeviceStateTransition::ResetDevice);
            while (fStateQueue.WaitForNext() != DeviceState::Idle) {}
        }
        break;
    case DeviceState::Ready:
        // Ready -> Running
        if (cmd==nestdaq::fairmq::command::kRun) {
            ChangeDeviceState(DeviceStateTransition::Run);
            while (fStateQueue.WaitForNext() != DeviceState::Running) {}
            writeStartTime();
        }
        // Ready -> ResettingTask -> DeviceReady
        if (cmd==nestdaq::fairmq::command::kResetTask) {
            ChangeDeviceState(DeviceStateTransition::ResetTask);
            while (fStateQueue.WaitForNext() != DeviceState::DeviceReady) {}
        }
        break;
    case DeviceState::Running:
        if (cmd==nestdaq::fairmq::command::kStop) {
            ChangeDeviceState(DeviceStateTransition::Stop);
            while (fStateQueue.WaitForNext() != DeviceState::Ready) {}
            writeStopTime();
        }
        break;
    default: // do nothing
        break;
    }
    //LOG(debug) << kMyClass << ":" << __func__ << " done";

}

void Plugin::readRunNumber()
{
    auto key = join({std::string{kRunInfoPrefix}, std::string{kRunNumber}}, fSeparator);

    // LOG(debug) << " run number key = " << key;
    const auto kFetchedRunNumber = fClient->get(key);
    if (!kFetchedRunNumber) {
        LOG(error) << " could not find run-number key in redis = " << key;
        return;
    }
    LOG(debug) << kMyClass << " run number (from redis) = " << *kFetchedRunNumber;
    std::string my_run_number;
    if (PropertyExists(std::string{kRunNumber})) {
        my_run_number = GetProperty<std::string>(std::string{kRunNumber});
    }
    if (my_run_number!=*kFetchedRunNumber) {
        LOG(warn) << kMyClass << " update run number " << *kFetchedRunNumber << " (old = " << my_run_number << ")";
        SetProperty(std::string{kRunNumber}, *kFetchedRunNumber);
    } else {
        // LOG(debug) << kMyClass << " same run number " << *run_number << " (old = " << my_run_number << ")";
    }
}

/**
 * @brief registerService the FairMQ service instance in Redis.
 *
 * This records health, presence, state, and command-line option metadata, then
 * starts the periodic TTL refresh timer used by the service registry.
 */
void Plugin::registerService()
{
    auto registry_uri = GetProperty<std::string>(std::string{kServiceRegistryUri});
    LOG(debug) << " registry URI = " << registry_uri;

    try {
        {
            fClient = std::make_shared<sw::redis::Redis>(registry_uri);
            fClient->command("client", "setname", join({std::string{kTopPrefix}, fServiceName, fId}, fSeparator));
        }
        setId();
        LOG(debug) << " mq device id = " << fId << ", service = " << fServiceName << ", hostname = " << fHealth->host_name
                   << " ip(from_hostname) = " << fair::mq::tools::getIpFromHostname(fHealth->host_name)

                   << ", " << fHealth->ip_address;

        fProgOptionKeyName = join({std::string{kTopPrefix}, fServiceName, fId, std::string{kProgOptionPrefix}}, fSeparator);

        LOG(debug) << "(registerService) id = " << fId << ", service = " << fServiceName;
        fHealth->key    = join({std::string{kTopPrefix}, fServiceName, fId, std::string{kHealthPrefix}}, fSeparator);
        fFairMQStateKey = join({std::string{kTopPrefix}, fServiceName, fId, std::string{kFairMQStatePrefix}}, fSeparator);
        fUpdateTimeKey  = join({std::string{kTopPrefix}, fServiceName, fId, std::string{kUpdateTimePrefix}}, fSeparator);
        fRegisteredKeys.insert(fFairMQStateKey);
        fRegisteredKeys.insert(fUpdateTimeKey);
        LOG(debug) << " precense (key) = " << fPresence->key << ", presence (ttl) = " << fMaxTtl;

        if (!fContext) {
            // create io_context, work_guard (to avoid exit of io_context::run())
            fContext = std::make_shared<net::io_context>();
            fWorkGuard = std::make_unique<work_guard_t>(std::move(net::make_work_guard(*fContext)));
            // start io_context::run() in another thread
            fTimerThread = std::thread([this]() {
                fContext->run();
            });
            fTimerThread.detach();
            LOG(debug) << " thread start";

            LOG(debug) << " timer start " << (fTtlUpdateInterval * kMillisecondsPerSecond)  << " msec";
            fTimer = std::make_unique<Timer>();
            fTimer->start(fContext, fTtlUpdateInterval * kMillisecondsPerSecond, [this](const auto& /*ec*/) {
                resetTtl();
                return false; // for restart
            });

        }

        //auto uptime_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - fHealth->created_time);
        //fHealth->updated_time = fHealth->created_time_system + std::chrono::duration_cast<std::chrono::seconds>(uptime_nsec);
        const auto &[uptime_nsec, updated_time] = updateDate(fHealth->created_time_system, fHealth->created_time);
        LOG(debug) << kMyClass << " hset " << fHealth->key << " " << fHealth->host_name << " " << fHealth->ip_address;
        LOG(debug) << kMyClass << " hset " << fProgOptionKeyName;

        {
            // pipeline
            std::scoped_lock<std::mutex> lock{fMutex};
            auto pipe = fClient->pipeline();
            pipe.hset(fHealth->key,
            {   std::make_pair("instanceID",  fId),
                std::make_pair("uuid",        boost::uuids::to_string(fUuid)),
                std::make_pair("hostName",    fHealth->host_name),
                std::make_pair("hostIp",      fHealth->ip_address),
                std::make_pair("serviceName", fServiceName),
                std::make_pair("createdTime", toDate(fHealth->created_time_system)),
//              std::make_pair("updated_time", toDate(fHealth->updated_time)),
                std::make_pair("updated_time", toDate(updated_time)),
                std::make_pair("uptime",      std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(uptime_nsec).count())),
            })
            .expire(fHealth->key, fMaxTtl)
            .exec();
            fRegisteredKeys.insert(fHealth->key);
        }

        writeProgOptions();
        fRegisteredKeys.insert(fProgOptionKeyName);

    } catch (const sw::redis::Error &e) {
        LOG(error) << " registerService failed (redis error): " << e.what();
    } catch (const std::exception& e) {
        LOG(error) << " registerService failed: " << e.what();
    } catch (...) {
        LOG(error) << " registerService failed: unknown exception";
    }
}

/**
 * @brief Refresh registry TTLs and liveness timestamps for this service.
 *
 * The refresh keeps the service presence, FairMQ state, health hash, program
 * option hash, and topology entries alive while the process is running.
 */
void Plugin::resetTtl()
{
//  LOG(debug) << " reset presence ttl";
//  auto uptime_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - fHealth->created_time);
//  fHealth->updated_time = fHealth->created_time_system + std::chrono::duration_cast<std::chrono::seconds>(uptime_nsec);
    const auto &[uptime_nsec, updated_time] = updateDate(fHealth->created_time_system, fHealth->created_time);
    const auto & last_checked = toDate(updated_time);

    std::scoped_lock<std::mutex> lock{fMutex};
    auto pipe = fClient->pipeline();
    pipe.hset(fHealth->key,
    {   std::make_pair("updated_time", last_checked),
        std::make_pair("uptime", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(uptime_nsec).count())),
    })
    .setex(fPresence->key, fMaxTtl, boost::uuids::to_string(fUuid))
    .setex(fFairMQStateKey, fMaxTtl, GetStateName(GetCurrentDeviceState()))
    .setex(fUpdateTimeKey, fMaxTtl, last_checked)
    .expire(fHealth->key, fMaxTtl)
    .expire(fProgOptionKeyName, fMaxTtl);
    if (fTopology) {
        fTopology->resetTtl(pipe);
    }
    pipe.exec();
}

/**
 * @brief Drive the device from Idle toward the configured startup state.
 */
void Plugin::runStartupSequence()
{
    // Idle -> .. -> DeviceReady
    LOG(debug) << kMyClass << " runStartupSequence()";
    auto s = boost::to_lower_copy(fStartupState);
    LOG(debug) << " startup state = " << fStartupState << " " << s;

    if (s=="idle") return;

    changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitDevice);
    if (s=="initialingdevice" || s=="initializing-device") return;

    changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kCompleteInit);
    if (s=="initialized") return;

    changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kBind);
    if (s=="bound") return;

    changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kConnect);
    if (s=="deviceready" || s=="device-ready") return;

    changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kInitTask);
    if (s=="ready") return;

    changeDeviceStateBySingleCommand(nestdaq::fairmq::command::kRun);
    if (s=="running") return;

    LOG(debug) << kMyClass << " runStartupSequence() done";
}

/**
 * @brief Drive the device through the shutdown path and release control.
 */
void Plugin::runShutdownSequence()
{
    LOG(debug) << kMyClass << " runShutdownSequence()";
    auto next_state = GetCurrentDeviceState();
    if (next_state != DeviceState::Error) {
        fStateQueue.Clear();
    }

    // any state -> .. -> Exiting
    while (next_state != DeviceState::Exiting && next_state != DeviceState::Error) {
        switch (next_state) {
        case DeviceState::Idle:
            ChangeDeviceState(DeviceStateTransition::End);
            break;
        case DeviceState::InitializingDevice:
            ChangeDeviceState(DeviceStateTransition::CompleteInit);
            break;
        case DeviceState::Initialized:
        case DeviceState::Bound:
        case DeviceState::DeviceReady:
            ChangeDeviceState(DeviceStateTransition::ResetDevice);
            break;
        case DeviceState::Ready:
            ChangeDeviceState(DeviceStateTransition::ResetTask);
            break;
        case DeviceState::Running:
            ChangeDeviceState(DeviceStateTransition::Stop);
            writeStopTime();
            break;
        default:
            break;
        }
        next_state = fStateQueue.WaitForNext();
    }
    ReleaseDeviceControl();
    LOG(debug) << kMyClass << " runShutdownSequence() done";
}

void Plugin::setCurrentWorkingDirectory()
{
    std::array<char, kCwdBufferSize> d{};
    getcwd(d.data(), d.size());
    std::stringstream ss;
    ss << d.data();
    ss >> fCwd;
    LOG(debug) << " cwd = " << fCwd;

}

void Plugin::setId()
{
    // Initialize device ID
    if (PropertyExists("id")) {
        fId = GetProperty<std::string>("id");
    }
    if (fId.empty() && !fServiceName.empty()) {
        while (true) {
            try {
                sw::redis::RedMutex mtx(fClient, "resource");
                std::unique_lock<sw::redis::RedMutex> red_lock(mtx, std::defer_lock);
                if (red_lock.try_lock()) {
                    LOG(debug) << "got lock:  " << fUuid;
                    auto presence_keys = scan(*fClient, {std::string{kTopPrefix}, fServiceName, "*", std::string{kPresencePrefix}}, fSeparator);
                    std::unordered_set<std::string> uuid_list; // existing uuids

                    if (!presence_keys.empty()) {
                        fClient->mget(presence_keys.cbegin(), presence_keys.cend(), std::inserter(uuid_list, uuid_list.begin()));
                    }
                    std::string key = join({std::string{kTopPrefix}, std::string{kServiceInstanceIndexPrefix}, fServiceName}, fSeparator);

                    std::unordered_map<std::string, std::string> hash_index_to_uuid;
                    LOG(debug) << "'id' (instance id) is empty. calculate service-instance-index";
                    fClient->hgetall(key, std::inserter(hash_index_to_uuid, hash_index_to_uuid.begin()));
                    auto my_uuid = boost::uuids::to_string(fUuid);
                    std::vector<std::string> index_expired;
                    std::string my_index;
                    for (const auto &[index, uuid] : hash_index_to_uuid) {
                        if (uuid_list.count(uuid)==0) {
                            LOG(warn) << " expired " << index << " " << uuid;
                            index_expired.emplace_back(index);
                        } else if (uuid == my_uuid) {
                            my_index = index;
                            LOG(debug) << " same uuid is found. reuse the service instance-index: " << my_index;
                        }
                    }
                    if (!index_expired.empty()) {
                        fClient->hdel(key, index_expired.cbegin(), index_expired.cend());
                    }
                    LOG(debug) << " number of expired uuids " << index_expired.size();

                    if (my_index.empty()) {
                        for (auto index=0; ; ++index) {
                            my_index = std::to_string(index);
                            if (fClient->hsetnx(key, my_index, my_uuid)) {
                                fRegisteredHashes.insert({key, my_index});
                                fId = fServiceName + "-" + my_index;
                                fPresence->key = join({std::string{kTopPrefix}, fServiceName, fId, std::string{kPresencePrefix}}, fSeparator);
                                fClient->setex(fPresence->key, fMaxTtl, boost::uuids::to_string(fUuid));
                                fRegisteredKeys.insert(fPresence->key);
                                LOG(debug) << " service instance-index: " << my_index << " for uuid = " << fUuid;
                                break;
                            }
                        }
                    }
                }

                if (red_lock.owns_lock()) {
                    LOG(debug) << "unlock:  " << fUuid;
                    red_lock.unlock();
                    break;
                } else {
                    //LOG(debug) << "extend lock:  " << fUuid;
                    //red_lock.extend_lock(std::chrono::milliseconds(30000));
                    std::this_thread::sleep_for(kRedLockRetryInterval);
                }
            } catch (const sw::redis::Error& e) {
                LOG(error) << " caught exception (redis++) : " << e.what();
            } catch (const std::exception& e) {
                LOG(error) << " caught exception (std) : " << e.what();
            } catch (...) {
                LOG(error) << " caught exception : unknown";
            }
        }
        SetProperty("id", fId);
    }
    if (fId.empty()) {
        fId = boost::uuids::to_string(fUuid);
        SetProperty("id", fId);
    }

}

void Plugin::setProcessName()
{
    fPid = getpid();

    // /proc/self/cmdline --> process name + all args (without white space)
    //  auto path_name = "/proc/"s + std::to_string(fPid) + "/cmdline";
    //  std::ifstream ifproc(path_name.data());
    //  std::string s;
    //  if (std::getline(ifproc, s)) {
    //    LOG(debug) << " process : " << fPid << " " << s;
    //  }

    //  LOG(debug) << " __progname : " << __progname;
    //  LOG(debug) << " program_invocation_name : " << program_invocation_name;
    //  LOG(debug) << " program_invocation_short_name : " << program_invocation_short_name;

    fProcessName = program_invocation_name;
    LOG(debug) << " process : " << fPid << " " << fProcessName;
    if (!PropertyExists("process-name")) {
        SetProperty("process-name", fProcessName);
    }

}

/**
 * @brief Subscribe to DAQ control commands published through Redis.
 *
 * Matching commands are translated to FairMQ state transitions for this service
 * instance. Exit-like commands also request plugin shutdown so the state-control
 * thread can run the shutdown sequence.
 */
void Plugin::subscribeToDaqCommand()
{
    LOG(debug) << " create a sbuscriber. ";
    auto sub = fClient->subscriber();

    // set callback functions.
    sub.on_message([this](const auto& channel, const auto& msg) {
        // process message of MESSAGE type.
        LOG(debug) << kMyClass << " on_message(MESSAGE): channel = " << channel << " msg = " << msg;
        if (std::string{kCommandChannelName} != channel) {
            return;
        }
        const auto& obj = toJson(msg);
        const auto& cmd = obj. template get_optional<std::string>("command");
        if (!cmd) {
            LOG(error) << kMyClass << " on_message(MESSAGE): missing command";
            return;
        }
        if (*cmd == "change_state") {
            const auto& val = obj. template get_optional<std::string>("value");
            std::unordered_set<std::string> services;
            for (const auto& x : obj.get_child("services")) {
                services.emplace(x.second. template get_value<std::string>());
            }
            std::unordered_set<std::string> instances;
            for (const auto& x : obj.get_child("instances")) {
                instances.emplace(x.second. template get_value<std::string>());
            }
            if (!val) {
                LOG(error) << kMyClass << " on_message() change_state : new state is not specified.";
                return;
            }
            if (services.empty()) {
                LOG(error) << kMyClass << " on_message() change_state : service is not specified.";
                return;
            }
            if (instances.empty()) {
                LOG(error) << kMyClass << " on_message() change_state : instance is not specified.";
                return;
            }
            bool is_single_command = false; // TO DO
            const std::string kLongInstanceId = nestdaq::daq::service::join({fServiceName, fId}, fSeparator);
            if ((services.count("all")>0) ||
                    ((services.count(fServiceName)>0) && ((instances.count("all")>0) || (instances.count(kLongInstanceId)>0)))) {
                if (is_single_command) {
                    changeDeviceStateBySingleCommand(*val);
                } else {
                    changeDeviceStateByMultiCommand(*val);
                }

                // any state Exiting by exiting subscribeToDaqCommand() and calling runShutdownSequence() in the state control thread
                if ((*val==nestdaq::daq::command::kExit) ||
                        (*val==nestdaq::daq::command::kQuit) ||
                        (*val==nestdaq::fairmq::command::kEnd)) {
                    fPluginShutdownRequested = true;
                }
            }
        }
    });
    sub.subscribe(std::string{kCommandChannelName});

    while (!fPluginShutdownRequested) {
        try {
            sub.consume();
        } catch (const sw::redis::TimeoutError &) {
            continue;
        } catch (const sw::redis::Error &e) {
            LOG(error) << kMyClass << "::" << __func__ << ": error in consume(): " << e.what();
            break;
        } catch (const std::exception& e) {
            LOG(error) << kMyClass << "::" << __func__ << ": error in consume(): " << e.what();
            break;
        } catch (...) {
            LOG(error) << kMyClass << "::" << __func__ << ": unknown exception";
            break;
        }
    }
    LOG(debug) << " " << __func__ << " exit.";
}

/**
 * @brief Remove service registry keys and allocated instance-index fields.
 */
void Plugin::unregisterService()
{
    LOG(debug) << kMyClass << " unregisterService";

    try {
        if (!fRegisteredKeys.empty()) {
            auto ndeleted = fClient->del(fRegisteredKeys.cbegin(), fRegisteredKeys.cend());
            fRegisteredKeys.clear();
            LOG(debug) << " redis : " << ndeleted << " deleted";
        }
        for (const auto& [key, field] : fRegisteredHashes) {
            fClient->hdel(key, field);
            LOG(debug) << " delete redis hash. key = " << key << ", field = " << field;
        }
        fRegisteredHashes.clear();
    } catch (const sw::redis::Error &e) {
        LOG(error) << " unregisterServiceService failed (redis error): " << e.what();
    } catch (const std::exception &e) {
        LOG(error) << " UnReigster failed: " << e.what();
    } catch (...) {
        LOG(error) << " unregisterServiceService failed: unknwon exception";
    }
}

/**
 * @brief Write FairMQ program options to the service registry.
 */
void Plugin::writeProgOptions()
{
    std::scoped_lock<std::mutex> lock{fMutex};
    auto pipe = fClient->pipeline();
    pipe.hset(fProgOptionKeyName,
    {   std::make_pair("severity",            GetProperty<std::string>("severity")),
        std::make_pair("file-severity",       GetProperty<std::string>("file-severity")),
        std::make_pair("verbosity",           GetProperty<std::string>("verbosity")),
        std::make_pair("color",               std::to_string(GetProperty<bool>("color"))),
        std::make_pair("log-to-file",         GetProperty<std::string>("log-to-file")),
        std::make_pair("id",                  GetProperty<std::string>("id")),
        std::make_pair("io-threads",          std::to_string(GetProperty<int>("io-threads"))),
        std::make_pair("transport",           GetProperty<std::string>("transport")),
        std::make_pair("network-interface",   GetProperty<std::string>("network-interface")),
        std::make_pair("init-timeout",        std::to_string(GetProperty<int>("init-timeout"))),
        std::make_pair("shm-segment-size",    std::to_string(GetProperty<std::size_t>("shm-segment-size"))),
        std::make_pair("shm-allocation",      GetProperty<std::string>("shm-allocation")),
        std::make_pair("shm-monitor",         std::to_string(GetProperty<bool>("shm-monitor"))),
        std::make_pair("shm-mlock-segment",   std::to_string(GetProperty<bool>("shm-mlock-segment"))),
        std::make_pair("shm-zero-segment",    std::to_string(GetProperty<bool>("shm-zero-segment"))),
        std::make_pair("shm-throw-bad-alloc", std::to_string(GetProperty<bool>("shm-throw-bad-alloc"))),
        std::make_pair("rate",                std::to_string(GetProperty<float>("rate"))),
        std::make_pair("session",             GetProperty<std::string>("session")),
    })
    .expire(fProgOptionKeyName, fMaxTtl);
}

/**
 * @brief Record run start time in Redis and mirror it into FairMQ properties.
 */
void Plugin::writeStartTime()
{
    const auto &[uptime_nsec, updated_time] = updateDate(fHealth->created_time_system, fHealth->created_time);
    auto t   = toDate(updated_time);
    auto t_ns = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(uptime_nsec).count());
    fClient->hset(fHealth->key,
    {   std::make_pair(std::string{kStartTime}, t),
        std::make_pair(std::string{kStartTimeNs}, t_ns)
    });
    SetProperty(std::string{kStartTime}, t);
    SetProperty(std::string{kStartTimeNs}, t_ns);
}

/**
 * @brief Record run stop time in Redis and mirror it into FairMQ properties.
 */
void Plugin::writeStopTime()
{
    const auto &[uptime_nsec, updated_time] = updateDate(fHealth->created_time_system, fHealth->created_time);
    auto t   = toDate(updated_time);
    auto t_ns = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(uptime_nsec).count());
    fClient->hset(fHealth->key,
    {   std::make_pair(std::string{kStopTime}, t),
        std::make_pair(std::string{kStopTimeNs}, t_ns)
    });
    SetProperty(std::string{kStopTime}, t);
    SetProperty(std::string{kStopTimeNs}, t_ns);
}

} // namespace nestdaq::daq::service
