#ifndef DaqService_Plugins_TelemetryPlugin_h
#define DaqService_Plugins_TelemetryPlugin_h

#include <mutex>
#include <string>
#include <string_view>

#include <fairmq/FairMQLogger.h>
#include <fairmq/Plugin.h>


namespace daq::service {

class TelemetryPlugin : public fair::mq::Plugin
{
public:

    struct OptionKey {
        static constexpr std::string_view TelemetrySeverity{"telemetry-severity"};
    };

    TelemetryPlugin(std::string_view name,
                    const fair::mq::Plugin::Version &version,
                    std::string_view maintainer,
                    std::string_view homepage,
                    fair::mq::PluginServices *pluginService);
    TelemetryPlugin(const TelemetryPlugin&) = delete;
    TelemetryPlugin& operator=(const TelemetryPlugin&) = delete;
    ~TelemetryPlugin() override;

private:
    std::string fId;
    std::string fServiceName;
    std::string fSeverity;
    std::mutex  fMutex;

    void OutputToConsole(const std::string &content, const fair::LogMetaData &metadata);

};

//_____________________________________________________________________________
auto TelemetryPluginProgramOptions() -> fair::mq::Plugin::ProgOptions;

REGISTER_FAIRMQ_PLUGIN(
    TelemetryPlugin,
    telemetry,
(fair::mq::Plugin::Version{0, 0, 0}),
"Telemetry <maitainer@daq.service.net>",
"https://github.com/spadi-alliance/nestdaq",
daq::service::TelemetryPluginProgramOptions
) // end of macro: REGISTER_FAIRMQ_PLUGIN

} // namespace::daq::service

#endif