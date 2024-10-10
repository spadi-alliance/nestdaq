#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <thread>

#include "plugins/Constants.h"
#include "plugins/tools.h"

#include "plugins/TelemetryPlugin.h"


static constexpr std::string_view MyClass{"daq::service::TelemetryPlugin"};

using namespace std::string_literals;
using namespace std::chrono_literals;


namespace daq::service {

//_____________________________________________________________________________
auto TelemetryPluginProgramOptions() -> fair::mq::Plugin::ProgOptions
{
    namespace bpo = boost::program_options;
    using opt = TelemetryPlugin::OptionKey;
    auto options = bpo::options_description(MyClass.data());
    options.add_options()
    (opt::TelemetrySeverity.data(),
        bpo::value<std::string>()->default_value(
#if 1
	fair::Logger::SeverityName(fair::Severity::trace).data()),
#else //FairLogger 1.5 changed the class interface of this.
	fair::Logger::SeverityName(fair::Severity::trace)),
#endif
     "Log severity level (telemetry): trace, debug, info, state, warn, error, fatal, nolog.");

    return options;
}


//_____________________________________________________________________________
TelemetryPlugin::TelemetryPlugin(std::string_view name,
                                 const fair::mq::Plugin::Version &version,
                                 std::string_view maintainer,
                                 std::string_view homepage,
                                 fair::mq::PluginServices *pluginService)
    : fair::mq::Plugin(name.data(), version, maintainer.data(), homepage.data(), pluginService)
{

    using opt = TelemetryPlugin::OptionKey;
    fSeverity = GetProperty<std::string>(opt::TelemetrySeverity.data());
    while (true) {
        if (PropertyExists("id")) {
            fId = GetProperty<std::string>("id");
            break;
        }
        std::this_thread::sleep_for(100ms);
    }

    if (PropertyExists(ServiceName.data())) {
        fServiceName = GetProperty<std::string>(ServiceName.data());
    }

    fair::Logger::AddCustomSink(MyClass.data(), fSeverity, [this](const std::string &content, const fair::LogMetaData &metadata) {
        OutputToConsole(content, metadata);

    });
}

//_____________________________________________________________________________
TelemetryPlugin::~TelemetryPlugin()
{
    fair::Logger::RemoveCustomSink(MyClass.data());
}

//_____________________________________________________________________________
void TelemetryPlugin::OutputToConsole(const std::string &content, const fair::LogMetaData &metadata)
{
    std::ostringstream timestamp;
    {
        const auto lt = std::localtime(&metadata.timestamp);
        timestamp << std::put_time(lt, "%H:%M:%S") << "." << std::setw(6) << std::setfill('0') << metadata.us.count();
    }

    //std::timespec ts;
    //if (std::timespec_get(&ts, TIME_UTC) == 0) {
    //  return;
    //}


    boost::property_tree::ptree obj;
    obj.put("id", fId);
    obj.put(ServiceName.data(), fServiceName);
    obj.put("process_name", metadata.process_name);
    obj.put("timestamp", timestamp.str());
    obj.put("file", metadata.file);
    obj.put("line", metadata.line);
    obj.put("func", metadata.func);
    obj.put("severity", metadata.severity_name);
    obj.put("time_t", metadata.timestamp);
    obj.put("log", content);
    //obj.put("tv_sec", ts.tv_sec);
    //obj.put("tv_nsec", ts.tv_nsec);

    // pretty = false -> single line
    std::cout << to_string(obj, false) << std::endl;
}

}
