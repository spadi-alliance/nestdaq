#include <algorithm>
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
#include "plugins/LoggingFileSink.h"
#include "plugins/DaqRunLogger.h"
#include "plugins/DaqServicePlugin.h"

// valid if _GNU_SOURCE is defined
//extern char *program_invocation_name;
//extern char *program_invocation_short_name;
//extern char *__progname; // same as program_invocation_short_name

static constexpr std::string_view MyClass{"daq::service::Plugin"};

static constexpr std::string_view StartupState{"startup-state"};

static constexpr std::string_view EnableUds{"enable-uds"};

static const std::unordered_set<std::string_view> knownCommandList{
    fairmq::command::Bind,
    fairmq::command::CompleteInit,
    fairmq::command::Connect,
    fairmq::command::End,
    fairmq::command::InitDevice,
    fairmq::command::InitTask,
    fairmq::command::ResetDevice,
    fairmq::command::ResetTask,
    fairmq::command::Run,
    fairmq::command::Stop,
    daq::command::Exit,
    daq::command::Quit,
    daq::command::Reset,
    daq::command::Start,
};

using namespace std::string_literals;

//_____________________________________________________________________________
bool ends_with(const std::string& s, const std::string& suffix)
{
    if (s.size() < suffix.size()) return false;
    return std::equal(std::rbegin(suffix), std::rend(suffix), std::rbegin(s));
}

namespace daq::service {
//_____________________________________________________________________________
auto PluginProgramOptions() -> fair::mq::Plugin::ProgOptions
{
    namespace bpo = boost::program_options;

    LOG(debug) << "daq::service::PluginProgramOptions: add_options";
    using opt_logger = RunLogger::OptionKey;
    auto pluginOptions = bpo::options_description(MyClass.data());
    pluginOptions.add_options() //
    (ServiceName.data(),        bpo::value<std::string>(),  "name of this service")
    //
    (Uuid.data(),               bpo::value<std::string>(),  "uuid of this service")
    //
    (HostIpAddress.data(),      bpo::value<std::string>(),  "IP address or hostname of this service")
    //
    (Hostname.data(),           bpo::value<std::string>(),  "hostname of this service")
    //
    (ServiceRegistryUri.data(), bpo::value<std::string>()->default_value("tcp://127.0.0.1:6379/0"), "DAQ service registry's URI")
    //
    (Separator.data(),          bpo::value<std::string>()->default_value(":"), "separator charactor for key space name")
    //
    (MaxTtl.data(),             bpo::value<long long>()->default_value(5), "max TTL (time-to-live) in second for keys")
    //
    (TtlUpdateInterval.data(),  bpo::value<long long>()->default_value(3), "TTL update interval in second for keys")
    //
    (StartupState.data(),       bpo::value<std::string>()->default_value("idle"),
     "state on startup. (idle, initializing-device, initialized, bound, device-ready, ready, running)")
    //
    (EnableUds.data(),          bpo::value<bool>()->default_value(false),
     "Use Unix Domain Socket for the local IPC if available (bool)")
    //
    (opt_logger::RunLogPrefix.data(), bpo::value<std::string>(), "prefix of file path for run-by-run logging")
    //
    (opt_logger::RunLogSeverity.data(), bpo::value<std::string>()->default_value("trace"), "severity for run-by-run logging")
    //
    (opt_logger::RunNumberFormat.data(), bpo::value<std::string>()->default_value("run{:08d}"), "run number format for the file path");

    LoggingFileSink::AddOptions(pluginOptions);
    return pluginOptions;
}

//_____________________________________________________________________________
Plugin::Plugin(std::string_view name,
               const fair::mq::Plugin::Version& version,
               std::string_view maintainer,
               std::string_view homepage,
               fair::mq::PluginServices *pluginServices)
    : fair::mq::Plugin(name.data(), version, maintainer.data(), homepage.data(), pluginServices)
{
    fUuid = boost::uuids::nil_uuid();

    LOG(debug) << MyClass << "() hello";
    SetCurrentWorkingDirectory();
    SetProcessName();

    if (PropertyExists(Uuid.data())) {
        fUuid = boost::lexical_cast<boost::uuids::uuid>(GetProperty<std::string>(Uuid.data()));
    }
    if (fUuid.is_nil()) {
        fUuid = boost::uuids::random_generator()();
        SetProperty("uuid", boost::uuids::to_string(fUuid));
    }
    LOG(debug) << MyClass << " uuid = "  << fUuid;

    fSeparator = GetProperty<std::string>(Separator.data());
    SetProperty("top-prefix", std::string(TopPrefix.data()));

    fPresence = std::make_unique<Presence>();
    fMaxTtl = GetProperty<long long>(MaxTtl.data());
    fTtlUpdateInterval = GetProperty<long long>(TtlUpdateInterval.data());

    fHealth = std::make_unique<Health>();
    if (PropertyExists(Hostname.data())) {
        fHealth->hostName = GetProperty<std::string>(Hostname.data());
    } else {
        fHealth->hostName = net::ip::host_name();
    }
    SetProperty(Hostname.data(), fHealth->hostName);
    fHealth->createdTimeSystem = std::chrono::system_clock::now();
    fHealth->createdTime       = std::chrono::steady_clock::now();

    SetProperty("created-time", std::chrono::duration_cast<std::chrono::nanoseconds>(fHealth->createdTimeSystem.time_since_epoch()).count());

    if (PropertyExists(HostIpAddress.data())) {
        auto ipAddress = GetProperty<std::string>(HostIpAddress.data());
        fHealth->ipAddress = fair::mq::tools::getIpFromHostname(ipAddress);
        auto hostIPs = fair::mq::tools::getHostIPs();
        //LOG(debug) << " host ip size = " << hostIPs.size();
        for (const auto& [nic, ip] : hostIPs) {
            //LOG(debug) << " nic = " << nic << ", ip = " << ip << ", ipAddress = " << fHealth->ipAddress;
            if (ip==fHealth->ipAddress) {
                SetProperty<std::string>("network-interface", nic);
                break;
            }
        }
    } else if (PropertyExists("network-interface")) {
        LOG(debug) << " find my ip address by network-interface";
        const auto defaultNIC = fair::mq::tools::getDefaultRouteNetworkInterface();
        auto nic = GetProperty<std::string>("network-interface");
        if (nic!="default") {
            fHealth->ipAddress = fair::mq::tools::getInterfaceIP(nic);
        }
        if (fHealth->ipAddress.empty()) {
            LOG(debug) << " use default route NIC = " << defaultNIC;
            fHealth->ipAddress = fair::mq::tools::getInterfaceIP(defaultNIC);
            SetProperty<std::string>("network-interface", defaultNIC);
        }
    }

    LOG(debug) << " ip = " << fHealth->ipAddress;
    SetProperty(HostIpAddress.data(), fHealth->ipAddress);

    if (PropertyExists(ServiceName.data())) {
        fServiceName = GetProperty<std::string>(ServiceName.data());
    }
    if (fServiceName.empty()) {
        std::vector<std::string> v;
        boost::split(v, fProcessName, boost::is_any_of("/"));
        LOG(debug) << " service name is empty. use process name (filename of executable) as service name";
        fServiceName = v.back();
        SetProperty(ServiceName.data(), fServiceName);
    }
    fStartupState   = GetProperty<std::string>(StartupState.data());



    auto hostIPs = fair::mq::tools::getHostIPs();
    for (const auto& [nic, ip] : hostIPs) {
        LOG(debug) << " nic = " << nic << ", ip = " << ip;
    }


    try {
        TakeDeviceControl();
        LOG(info) << MyClass << " succeeded in TakeDeviceControl()";
    } catch (fair::mq::PluginServices::DeviceControlError &e) {
        LOG(error) << MyClass << "'s constructor : " << e.what();
    } catch (const std::exception &e) {
        LOG(error) << MyClass << "'s constructor : canneo take device control. " << e.what();
    } catch (...) {
        LOG(error) << MyClass << "'s constructor : unknwo exception";
    }

    // register to service registry
    Register();
    fTopology = std::make_unique<TopologyConfig>(*this);
    fTopology->EnableUds(GetProperty<bool>(EnableUds.data()));

    LOG(warn) << MyClass << " SubscribeToDeviceStateChange()";
    SubscribeToDeviceStateChange([this](DeviceState newState) {
        try {
            auto stateName = GetStateName(newState);
            LOG(info) << MyClass << " state : " << stateName;
            fStateQueue.Push(newState);
            fClient->hset(fHealth->key, "fair:mq:state", stateName);
            fClient->set(fFairMQStateKey, stateName);
            WriteProgOptions();
            ReadRunNumber();
            switch (newState) {
            case DeviceState::Idle:
                fResetDeviceRequested = false;
                break;
            case DeviceState::InitializingDevice:
                fTopology->OnDeviceStateChange(newState);
                break;
            case DeviceState::Bound:
                fTopology->OnDeviceStateChange(newState);
                break;
            case DeviceState::ResettingDevice:
                fTopology->OnDeviceStateChange(newState);
                break;
            case DeviceState::Error:
                fPluginShutdownRequested = true;
            default:
                break;
            }
        } catch (const std::exception &e) {
            LOG(error) << MyClass << " exception during device state change: " << e.what();
        } catch (...) {
            LOG(error) << MyClass << " exception during device state change: unknow exception";
        }
    });

    fStateControlThread = std::thread([this]() {
        try {
            RunStartupSequence();
            SubscribeToDaqCommand();
            RunShutdownSequence();
        } catch (const fair::mq::PluginServices::DeviceControlError &e) {
            LOG(error) << MyClass << " " << e.what();
        } catch (const fair::mq::DeviceErrorState &e) {
            LOG(error) << MyClass << " " << e.what();
            ReleaseDeviceControl();
        }
    });
    fStateControlThread.detach();

    LOG(debug) << MyClass << "() done";

}

//_____________________________________________________________________________
Plugin::~Plugin()
{
    UnsubscribeFromDeviceStateChange();
    LOG(warn) << MyClass << " UnsubscribeFromDeviceStateChange()";
    auto state = GetCurrentDeviceState();
    if (state==DeviceState::Exiting) {
        ReleaseDeviceControl();
        LOG(info) << MyClass << " ReleaseDeviceControl() done";
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
        fTopology->Reset();
    }
    Unregister();
    LOG(debug) << "~" << MyClass << "() bye";
}

//_____________________________________________________________________________
void Plugin::ChangeDeviceStateByMultiCommand(std::string_view cmd)
{
    //LOG(debug) << MyClass << ":" << __func__;
    auto state = GetCurrentDeviceState();
    //auto stateName = GetStateName(state);

    switch (state) {
    // ---------- state transition from Idle ----------
    case DeviceState::Idle:
        // Idle -> InitialzingDevice -> Initialized
        if ((cmd==fairmq::command::InitDevice) || (cmd==fairmq::command::CompleteInit)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::InitDevice);
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            break;
        }
        // Idle -> ... -> Bound
        if (cmd==fairmq::command::Bind) {
            ChangeDeviceStateBySingleCommand(fairmq::command::InitDevice);
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            break;
        }
        // Idle -> ... -> Bound -> Connecting -> DeviceReady
        if (cmd==fairmq::command::Connect) {
            ChangeDeviceStateBySingleCommand(fairmq::command::InitDevice);
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            break;
        }
        // Idle -> ... -> DeviceReady -> InitializingTask -> Ready
        if (cmd==fairmq::command::InitTask) {
            ChangeDeviceStateBySingleCommand(fairmq::command::InitDevice);
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            break;
        }
        // Idle -> ... -> Ready -> Running I
        if ((cmd==fairmq::command::Run) || (cmd==daq::command::Start)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::InitDevice);
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            ChangeDeviceStateBySingleCommand(fairmq::command::Run);
            break;
        }
        break;

    // ---------- state transition from InitializingDevice ----------
    case DeviceState::InitializingDevice:
        // InitializingDevice -> Initialized
        if (cmd==fairmq::command::CompleteInit) {
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            break;
        }
        // InitializingDevice -> ... -> Bound
        if (cmd==fairmq::command::Bind) {
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            break;
        }
        // InitializingDevice -> ... -> Bound -> Connecting -> DeviceReady
        if (cmd==fairmq::command::Connect) {
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            break;
        }
        // InitializingDevice -> ... -> DeviceReady -> InitializingTask -> Ready
        if (cmd==fairmq::command::InitTask) {
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            break;
        }
        // InitializingDevice -> ... -> Ready -> Running
        if ((cmd==fairmq::command::Run) || (cmd==daq::command::Start)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            ChangeDeviceStateBySingleCommand(fairmq::command::Run);
            break;
        }
        break;

    // ---------- state transition from Initialized ----------
    case DeviceState::Initialized:
        // Initialized -> ... -> Bound
        if (cmd==fairmq::command::Bind) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            break;
        }
        // Initialized -> ... -> Bound -> Connecting -> DeviceReady
        if (cmd==fairmq::command::Connect) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            break;
        }
        // Initialized -> ... -> DeviceReady -> InitializingTask -> Ready
        if (cmd==fairmq::command::InitTask) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            break;
        }
        // Initialized -> ... -> Ready -> Running
        if ((cmd==fairmq::command::Run) || (cmd==daq::command::Start)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            ChangeDeviceStateBySingleCommand(fairmq::command::Run);
            break;
        }

        // Initialized -> ResettingDevice -> Idle
        if ((cmd==fairmq::command::ResetDevice) || (cmd==daq::command::Reset)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::ResetDevice);
            break;
        }
        break;

    // ---------- state transition from Bound ----------
    case DeviceState::Bound:
        // Bound -> Connecting -> DeviceReady
        if (cmd==fairmq::command::Connect) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            break;
        }
        // Bound -> ... -> DeviceReady -> InitializingTask -> Ready
        if (cmd==fairmq::command::InitTask) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            break;
        }
        // Bound -> ... -> Ready -> Running
        if ((cmd==fairmq::command::Run) || (cmd==daq::command::Start)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            ChangeDeviceStateBySingleCommand(fairmq::command::Run);
            break;
        }

        // Bound -> ResettingDevice -> Idle
        if ((cmd==fairmq::command::ResetDevice) || (cmd==daq::command::Reset)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::ResetDevice);
            break;
        }
        break;

    // ---------- state transition from DeviceReady ----------
    case DeviceState::DeviceReady:
        // DeviceReady -> InitializingTask -> Ready
        if (cmd==fairmq::command::InitTask) {
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            break;
        }
        // DeviceReady -> ... -> Ready -> Running
        if ((cmd==fairmq::command::Run) || (cmd==daq::command::Start)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
            ChangeDeviceStateBySingleCommand(fairmq::command::Run);
            break;
        }

        // DeviceReady -> ResettingDevice -> Idle
        if ((cmd==fairmq::command::ResetDevice) || (cmd==daq::command::Reset)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::ResetDevice);
            break;
        }
        break;

    // ---------- state transition from Ready ----------
    case DeviceState::Ready:
        // Ready -> Running
        if ((cmd==fairmq::command::Run) || (cmd==daq::command::Start)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Run);
            break;
        }

        // Ready -> ResettingDevice -> DeviceReady
        if (cmd==fairmq::command::ResetTask) {
            ChangeDeviceStateBySingleCommand(fairmq::command::ResetTask);
            break;
        }
        // Ready -> ResettingTask -> DeviceReady -> ResettingDevice -> Idle
        if ((cmd==fairmq::command::ResetDevice) || (cmd==daq::command::Reset)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::ResetTask);
            ChangeDeviceStateBySingleCommand(fairmq::command::ResetDevice);
            break;
        }
        break;

    // ---------- state transition from Running ----------
    case DeviceState::Running:
        // Running -> Ready
        if (cmd==fairmq::command::Stop) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Stop);
            break;
        }

        // Running -> Ready -> ResettingTask -> DeviceReady
        if (cmd==fairmq::command::ResetTask) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Stop);
            ChangeDeviceStateBySingleCommand(fairmq::command::ResetTask);
            break;
        }
        // Running -> ... -> ResettingDevice -> Idle
        if ((cmd==fairmq::command::ResetDevice) || (cmd==daq::command::Reset)) {
            ChangeDeviceStateBySingleCommand(fairmq::command::Stop);
            ChangeDeviceStateBySingleCommand(fairmq::command::ResetTask);
            ChangeDeviceStateBySingleCommand(fairmq::command::ResetDevice);
            break;
        }
        break;

    default: // do nothing
        break;
    }
    //LOG(debug) << MyClass << ":" << __func__ << " done";

}

//_____________________________________________________________________________
void Plugin::ChangeDeviceStateBySingleCommand(std::string_view cmd)
{
    //LOG(debug) << MyClass << ":" << __func__;
    auto state = GetCurrentDeviceState();
    //auto stateName = GetStateName(state);

    switch (state) {
    case DeviceState::Idle:
        // Idle -> InitializingDevice
        if (cmd==fairmq::command::InitDevice) {
            ChangeDeviceState(DeviceStateTransition::InitDevice);
            while (fStateQueue.WaitForNext() != DeviceState::InitializingDevice) {}
        }
        break;
    case DeviceState::InitializingDevice:
        // InitializingDevice -> Initialized
        if (cmd==fairmq::command::CompleteInit) {
            ChangeDeviceState(DeviceStateTransition::CompleteInit);
            while (fStateQueue.WaitForNext() != DeviceState::Initialized) {}
        }
        break;
    case DeviceState::Initialized:
        // Initialized -> Binding -> Bound
        if (cmd==fairmq::command::Bind) {
            ChangeDeviceState(DeviceStateTransition::Bind);
            while (fStateQueue.WaitForNext() != DeviceState::Bound) {}
        }
        // Initialized -> RessetingDevice -> Idle
        if (cmd==fairmq::command::ResetDevice) {
            fResetDeviceRequested = true;
            ChangeDeviceState(DeviceStateTransition::ResetDevice);
            while (fStateQueue.WaitForNext() != DeviceState::Idle) {}
        }
        break;
    case DeviceState::Bound:
        // Bound -> Connecting -> DeviceReady
        if (cmd==fairmq::command::Connect) {
            ChangeDeviceState(DeviceStateTransition::Connect);
            while (fStateQueue.WaitForNext() != DeviceState::DeviceReady) {}
        }
        // Bound -> RessetingDevice -> Idle
        if (cmd==fairmq::command::ResetDevice) {
            fResetDeviceRequested = true;
            ChangeDeviceState(DeviceStateTransition::ResetDevice);
            while (fStateQueue.WaitForNext() != DeviceState::Idle) {}
        }
        break;
    case DeviceState::Connecting:
        if (cmd==fairmq::command::ResetDevice) {
            fResetDeviceRequested = true;
        }
        break;
    case DeviceState::DeviceReady:
        // DeviceReady -> InitializingTask -> Ready
        if (cmd==fairmq::command::InitTask) {
            ChangeDeviceState(DeviceStateTransition::InitTask);
            while (fStateQueue.WaitForNext() != DeviceState::Ready) {}
        }
        // DeviceReady -> RessetingDevice -> Idle
        if (cmd==fairmq::command::ResetDevice) {
            fResetDeviceRequested = true;
            ChangeDeviceState(DeviceStateTransition::ResetDevice);
            while (fStateQueue.WaitForNext() != DeviceState::Idle) {}
        }
        break;
    case DeviceState::Ready:
        // Ready -> Running
        if (cmd==fairmq::command::Run) {
            ChangeDeviceState(DeviceStateTransition::Run);
            while (fStateQueue.WaitForNext() != DeviceState::Running) {}
            WriteStartTime();
        }
        // Ready -> ResettingTask -> DeviceReady
        if (cmd==fairmq::command::ResetTask) {
            ChangeDeviceState(DeviceStateTransition::ResetTask);
            while (fStateQueue.WaitForNext() != DeviceState::DeviceReady) {}
        }
        break;
    case DeviceState::Running:
        if (cmd==fairmq::command::Stop) {
            ChangeDeviceState(DeviceStateTransition::Stop);
            while (fStateQueue.WaitForNext() != DeviceState::Ready) {}
            WriteStopTime();
        }
        break;
    default: // do nothing
        break;
    }
    //LOG(debug) << MyClass << ":" << __func__ << " done";

}

//_____________________________________________________________________________
void Plugin::ReadRunNumber()
{
    auto key = join({RunInfoPrefix.data(), RunNumber.data()}, fSeparator);

    // LOG(debug) << " run number key = " << key;
    const auto runNumber = fClient->get(key);
    if (!runNumber) {
        LOG(error) << " could not find run-number key in redis = " << key;
        return;
    }
    LOG(debug) << MyClass << " run number (from redis) = " << *runNumber;
    std::string myRunNumber;
    if (PropertyExists(RunNumber.data())) {
        myRunNumber = GetProperty<std::string>(RunNumber.data());
    }
    if (myRunNumber!=*runNumber) {
        LOG(warn) << MyClass << " update run number " << *runNumber << " (old = " << myRunNumber << ")";
        SetProperty(RunNumber.data(), *runNumber);

        using opt = RunLogger::OptionKey;
        if (PropertyExists(opt::RunLogPrefix.data())) {
            auto runLogPrefix   = GetProperty<std::string>(opt::RunLogPrefix.data());
            auto runLogSeverity = GetProperty<std::string>(opt::RunLogSeverity.data());
            auto runNumberFormat = GetProperty<std::string>(opt::RunNumberFormat.data());
            fLogger = std::make_unique<RunLogger>(std::stoll(*runNumber), runLogSeverity, runLogPrefix, runNumberFormat);
        }

    } else {
        // LOG(debug) << MyClass << " same run number " << *runNumber << " (old = " << myRunNumber << ")";
    }
}

//_____________________________________________________________________________
void Plugin::Register()
{
    auto registryUri = GetProperty<std::string>(ServiceRegistryUri.data());
    LOG(debug) << " registry URI = " << registryUri;

    try {
        {
            fClient = std::make_shared<sw::redis::Redis>(registryUri);
            fClient->command("client", "setname", join({TopPrefix.data(), fServiceName, fId}, fSeparator));
        }
        SetId();
        if (PropertyExists(LoggingFileSink::OptionKey::Prefix.data())>0) {
            const auto &log_prefix = GetProperty<std::string>(LoggingFileSink::OptionKey::Prefix.data());
            std::string log_severity;
            std::string log_verbosity;
            if (PropertyExists(LoggingFileSink::OptionKey::Severity.data())>0) {
                log_severity = GetProperty<std::string>(LoggingFileSink::OptionKey::Severity.data());
            }
            if (PropertyExists(LoggingFileSink::OptionKey::Verbosity.data())>0) {
                log_verbosity = GetProperty<std::string>(LoggingFileSink::OptionKey::Verbosity.data());
            }
            LoggingFileSink::Open(log_prefix, log_severity, log_verbosity, fId);
        }
        LOG(debug) << " mq device id = " << fId << ", service = " << fServiceName << ", hostname = " << fHealth->hostName
                   << " ip(from_hostname) = " << fair::mq::tools::getIpFromHostname(fHealth->hostName)

                   << ", " << fHealth->ipAddress;

        fProgOptionKeyName = join({TopPrefix.data(), fServiceName, fId, ProgOptionPrefix.data()}, fSeparator);

        LOG(debug) << "(Register) id = " << fId << ", service = " << fServiceName;
        fHealth->key    = join({TopPrefix.data(), fServiceName, fId, HealthPrefix.data()}, fSeparator);
        fFairMQStateKey = join({TopPrefix.data(), fServiceName, fId, FairMQStatePrefix.data()}, fSeparator);
        fUpdateTimeKey  = join({TopPrefix.data(), fServiceName, fId, UpdateTimePrefix.data()}, fSeparator);
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

            LOG(debug) << " timer start " << (fTtlUpdateInterval * 1000)  << " msec";
            fTimer = std::make_unique<Timer>();
            fTimer->Start(fContext, fTtlUpdateInterval * 1000, [this](const auto& ec) {
                ResetTtl();
                return false; // for restart
            });


        }

        //auto uptimeNsec = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - fHealth->createdTime);
        //fHealth->updatedTime = fHealth->createdTimeSystem + std::chrono::duration_cast<std::chrono::seconds>(uptimeNsec);
        const auto &[uptimeNsec, updatedTime] = update_date(fHealth->createdTimeSystem, fHealth->createdTime);
        LOG(debug) << MyClass << " hset " << fHealth->key << " " << fHealth->hostName << " " << fHealth->ipAddress;
        LOG(debug) << MyClass << " hset " << fProgOptionKeyName;

        {
            // pipeline
            std::lock_guard<std::mutex> lock{fMutex};
            auto pipe = fClient->pipeline();
            pipe.hset(fHealth->key,
            {   std::make_pair("instanceID",  fId),
                std::make_pair("uuid",        boost::uuids::to_string(fUuid)),
                std::make_pair("hostName",    fHealth->hostName),
                std::make_pair("hostIp",      fHealth->ipAddress),
                std::make_pair("serviceName", fServiceName),
                std::make_pair("createdTime", to_date(fHealth->createdTimeSystem)),
//              std::make_pair("updatedTime", to_date(fHealth->updatedTime)),
                std::make_pair("updatedTime", to_date(updatedTime)),
                std::make_pair("uptime",      std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(uptimeNsec).count())),
            })
            .expire(fHealth->key, fMaxTtl)
            .exec();
            fRegisteredKeys.insert(fHealth->key);
        }

        WriteProgOptions();
        fRegisteredKeys.insert(fProgOptionKeyName);

    } catch (const sw::redis::Error &e) {
        LOG(error) << " Register failed (redis error): " << e.what();
    } catch (const std::exception& e) {
        LOG(error) << " Register failed: " << e.what();
    } catch (...) {
        LOG(error) << " Register failed: unknown exception";
    }
}

//_____________________________________________________________________________
void Plugin::ResetTtl()
{
//  LOG(debug) << " reset presence ttl";
//  auto uptimeNsec = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - fHealth->createdTime);
//  fHealth->updatedTime = fHealth->createdTimeSystem + std::chrono::duration_cast<std::chrono::seconds>(uptimeNsec);
    const auto &[uptimeNsec, updatedTime] = update_date(fHealth->createdTimeSystem, fHealth->createdTime);
    const auto & lastChecked = to_date(updatedTime);

    std::lock_guard<std::mutex> lock{fMutex};
    auto pipe = fClient->pipeline();
    pipe.hset(fHealth->key,
    {   std::make_pair("updatedTime", lastChecked),
        std::make_pair("uptime", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(uptimeNsec).count())),
    })
    .setex(fPresence->key, fMaxTtl, boost::uuids::to_string(fUuid))
    .setex(fFairMQStateKey, fMaxTtl, GetStateName(GetCurrentDeviceState()))
    .setex(fUpdateTimeKey, fMaxTtl, lastChecked)
    .expire(fHealth->key, fMaxTtl)
    .expire(fProgOptionKeyName, fMaxTtl);
    if (fTopology) {
        fTopology->ResetTtl(pipe);
    }
    pipe.exec();
}

//_____________________________________________________________________________
void Plugin::RunStartupSequence()
{
    // Idle -> .. -> DeviceReady
    LOG(debug) << MyClass << " RunStartupSequence()";
    auto s = boost::to_lower_copy(fStartupState);
    LOG(debug) << " startup state = " << fStartupState << " " << s;

    if (s=="idle") return;

    ChangeDeviceStateBySingleCommand(fairmq::command::InitDevice);
    if (s=="initialingdevice" || s=="initializing-device") return;

    ChangeDeviceStateBySingleCommand(fairmq::command::CompleteInit);
    if (s=="initialized") return;

    ChangeDeviceStateBySingleCommand(fairmq::command::Bind);
    if (s=="bound") return;

    ChangeDeviceStateBySingleCommand(fairmq::command::Connect);
    if (s=="deviceready" || s=="device-ready") return;

    ChangeDeviceStateBySingleCommand(fairmq::command::InitTask);
    if (s=="ready") return;

    ChangeDeviceStateBySingleCommand(fairmq::command::Run);
    if (s=="running") return;

    LOG(debug) << MyClass << " RunStartupSequence() done";
}

//_____________________________________________________________________________
void Plugin::RunShutdownSequence()
{
    LOG(debug) << MyClass << " RunShutdownSequence()";
    auto nextState = GetCurrentDeviceState();
    if (nextState != DeviceState::Error) {
        fStateQueue.Clear();
    }

    // any state -> .. -> Exiting
    while (nextState != DeviceState::Exiting && nextState != DeviceState::Error) {
        switch (nextState) {
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
            WriteStopTime();
            break;
        default:
            break;
        }
        nextState = fStateQueue.WaitForNext();
    }
    ReleaseDeviceControl();
    LOG(debug) << MyClass << " RunShutdownSequence() done";
}

//_____________________________________________________________________________
void Plugin::SetCurrentWorkingDirectory()
{
    char d[512];
    getcwd(d, sizeof(d));
    std::stringstream ss;
    ss << d;
    ss >> fCwd;
    LOG(debug) << " cwd = " << fCwd;

}

//_____________________________________________________________________________
void Plugin::SetId()
{
    // Initialize device ID
    if (PropertyExists("id")) {
        fId = GetProperty<std::string>("id");
    }
    if (fId.empty() && !fServiceName.empty()) {
        while (true) {
            try {
                sw::redis::RedMutex mtx(fClient, "resource");
                std::unique_lock<sw::redis::RedMutex> redLock(mtx, std::defer_lock);
                if (redLock.try_lock()) {
                    LOG(debug) << "got lock:  " << fUuid;
                    auto presenceKeys = scan(*fClient, {TopPrefix.data(), fServiceName, "*", PresencePrefix.data()}, fSeparator);
                    std::unordered_set<std::string> uuidList; // existing uuids

                    if (!presenceKeys.empty()) {
                        fClient->mget(presenceKeys.cbegin(), presenceKeys.cend(), std::inserter(uuidList, uuidList.begin()));
                    }
                    std::string key = join({TopPrefix.data(), ServiceInstanceIndexPrefix.data(), fServiceName}, fSeparator);

                    std::unordered_map<std::string, std::string> hashIndexToUuid;
                    LOG(debug) << "'id' (instance id) is empty. calculate service-instance-index";
                    fClient->hgetall(key, std::inserter(hashIndexToUuid, hashIndexToUuid.begin()));
                    auto myUuid = boost::uuids::to_string(fUuid);
                    std::vector<std::string> indexExpired;
                    std::string myIndex;
                    for (const auto &[index, uuid] : hashIndexToUuid) {
                        if (uuidList.count(uuid)==0) {
                            LOG(warn) << " expired " << index << " " << uuid;
                            indexExpired.emplace_back(index);
                        } else if (uuid == myUuid) {
                            myIndex = index;
                            LOG(debug) << " same uuid is found. reuse the service instance-index: " << myIndex;
                        }
                    }
                    if (!indexExpired.empty()) {
                        fClient->hdel(key, indexExpired.cbegin(), indexExpired.cend());
                    }
                    LOG(debug) << " number of expired uuids " << indexExpired.size();


                    if (myIndex.empty()) {
                        for (auto index=0; ; ++index) {
                            myIndex = std::to_string(index);
                            if (fClient->hsetnx(key, myIndex, myUuid)) {
                                fRegisteredHashes.insert({key, myIndex});
                                fId = fServiceName + "-" + myIndex;
                                fPresence->key = join({TopPrefix.data(), fServiceName, fId, PresencePrefix.data()}, fSeparator);
                                fClient->setex(fPresence->key, fMaxTtl, boost::uuids::to_string(fUuid));
                                fRegisteredKeys.insert(fPresence->key);
                                LOG(debug) << " service instance-index: " << myIndex << " for uuid = " << fUuid;
                                break;
                            }
                        }
                    }
                }

                if (redLock.owns_lock()) {
                    LOG(debug) << "unlock:  " << fUuid;
                    redLock.unlock();
                    break;
                } else {
                    //LOG(debug) << "extend lock:  " << fUuid;
                    //redLock.extend_lock(std::chrono::milliseconds(30000));
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

//_____________________________________________________________________________
void Plugin::SetProcessName()
{
    fPid = getpid();

    // /proc/self/cmdline --> process name + all args (without white space)
    //  auto pathName = "/proc/"s + std::to_string(fPid) + "/cmdline";
    //  std::ifstream ifproc(pathName.data());
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

//_____________________________________________________________________________
void Plugin::SubscribeToDaqCommand()
{
    LOG(debug) << " create a sbuscriber. ";
    auto sub = fClient->subscriber();

    // set callback functions.
    sub.on_message([this](auto channel, auto msg) {
        // process message of MESSAGE type.
        LOG(debug) << MyClass << " on_message(MESSAGE): channel = " << channel << " msg = " << msg;
        if (CommandChannelName.data()!=channel) {
            return;
        }
        const auto& obj = to_json(msg);
        const auto& cmd = obj. template get_optional<std::string>("command");
        if (!cmd) {
            LOG(error) << MyClass << " on_message(MESSAGE): missing command";
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
                LOG(error) << MyClass << " on_message() change_state : new state is not specified.";
                return;
            }
            if (services.empty()) {
                LOG(error) << MyClass << " on_message() change_state : service is not specified.";
                return;
            }
            if (instances.empty()) {
                LOG(error) << MyClass << " on_message() change_state : instance is not specified.";
                return;
            }
            bool isSingleCommand = false; // TO DO
            const std::string longInstanceId = daq::service::join({fServiceName, fId}, fSeparator);
            if ((services.count("all")>0) ||
                    ((services.count(fServiceName)>0) && ((instances.count("all")>0) || (instances.count(longInstanceId)>0)))) {
                if (isSingleCommand) {
                    ChangeDeviceStateBySingleCommand(*val);
                } else {
                    ChangeDeviceStateByMultiCommand(*val);
                }

                // any state Exiting by exiting SubscribeToDaqCommand() and calling RunShutdownSequence() in the state control thread
                if ((*val==daq::command::Exit) ||
                        (*val==daq::command::Quit) ||
                        (*val==fairmq::command::End)) {
                    fPluginShutdownRequested = true;
                }
            }
        }
    });
    sub.subscribe(CommandChannelName.data());

    while (!fPluginShutdownRequested) {
        try {
            sub.consume();
        } catch (const sw::redis::TimeoutError &e) {
            // try again.
        } catch (const sw::redis::Error &e) {
            LOG(error) << MyClass << "::" << __func__ << ": error in consume(): " << e.what();
            break;
        } catch (const std::exception& e) {
            LOG(error) << MyClass << "::" << __func__ << ": error in consume(): " << e.what();
            break;
        } catch (...) {
            LOG(error) << MyClass << "::" << __func__ << ": unknown exception";
            break;
        }
    }
    LOG(debug) << " " << __func__ << " exit.";
}

//_____________________________________________________________________________
void Plugin::Unregister()
{
    LOG(debug) << MyClass << " Unregister";

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
        LOG(error) << " UnRegister failed (redis error): " << e.what();
    } catch (const std::exception &e) {
        LOG(error) << " UnReigster failed: " << e.what();
    } catch (...) {
        LOG(error) << " UnRegister failed: unknwon exception";
    }
}

//_____________________________________________________________________________
void Plugin::WriteProgOptions()
{
    fClient->hset(fProgOptionKeyName,
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
        std::make_pair("ofi-size-hint",       std::to_string(GetProperty<std::size_t>("ofi-size-hint"))),
        std::make_pair("rate",                std::to_string(GetProperty<float>("rate"))),
        std::make_pair("session",             GetProperty<std::string>("session")),
    });
    fClient->expire(fProgOptionKeyName, fMaxTtl);
}
//_____________________________________________________________________________
void Plugin::WriteStartTime()
{
    const auto &[uptimeNsec, updatedTime] = update_date(fHealth->createdTimeSystem, fHealth->createdTime);
    auto t   = to_date(updatedTime);
    auto tNS = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(uptimeNsec).count());
    fClient->hset(fHealth->key,
    {   std::make_pair(StartTime.data(), t),
        std::make_pair(StartTimeNS.data(), tNS)
    });
    SetProperty(StartTime.data(), t);
    SetProperty(StartTimeNS.data(), tNS);
}

//_____________________________________________________________________________
void Plugin::WriteStopTime()
{
    const auto &[uptimeNsec, updatedTime] = update_date(fHealth->createdTimeSystem, fHealth->createdTime);
    auto t   = to_date(updatedTime);
    auto tNS = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(uptimeNsec).count());
    fClient->hset(fHealth->key,
    {   std::make_pair(StopTime.data(), t),
        std::make_pair(StopTimeNS.data(), tNS)
    });
    SetProperty(StopTime.data(), t);
    SetProperty(StopTimeNS.data(), tNS);
}

} // namespace daq::service