#ifndef DaqService_Plugins_ParameterConfigPlugin_h
#define DaqService_Plugins_ParameterConfigPlugin_h

// Parameter configuration plugin using Redis

#include <atomic>
#include <cmath>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <fairmq/Plugin.h>

// forward declaration
namespace sw::redis {
class Redis;
}

namespace daq::service {

static constexpr std::string_view ParametersPrefix{"parameters"};

class ParameterConfigPlugin : public fair::mq::Plugin
{
public:
    struct OptionKey {
        static constexpr std::string_view ServerUri{"parameter-config-uri"};
    };

    ParameterConfigPlugin(std::string_view name,
                          const fair::mq::Plugin::Version &version,
                          std::string_view maintainer,
                          std::string_view homepage,
                          fair::mq::PluginServices *pluginServices);
    ParameterConfigPlugin(const ParameterConfigPlugin&) = delete;
    ParameterConfigPlugin& operator=(const ParameterConfigPlugin&) = delete;
    ~ParameterConfigPlugin() override;

private:
    std::shared_ptr<sw::redis::Redis> fClient;

    std::string fId;
    std::string fSeparator;
    std::string fKey;
    std::string fGroupKey;
    std::thread fSubscriberThread;
    std::atomic<bool> fPluginShutdownRequested{false};

    bool IsReservedOption(std::string_view name) const;
    void Parse(std::string_view name, std::string line);
    void ReadHash(const std::string& name);
    void ReadList(const std::string& name);
    void ReadParameters();
    void ReadSet(const std::string& name);
    void ReadString(const std::string& name);
    void ReadZset(const std::string& name);
    void SetPropertyOfReservedOption(std::string_view name, std::string_view value);
    template <typename T>
    void SetPropertyFromString(std::string_view name, std::string_view value)
    {
        auto isNewValue = !PropertyExists(name.data());
        T v;
        if constexpr (std::is_same_v<std::string, T>) {
            v = value.data();
        } else if constexpr (std::is_same_v<bool, T>) {
            v = (value=="1")   //
                || (value=="true") || (value=="TRUE") || (value=="True") //
                || (value=="on")  || (value=="ON")  || (value=="On") //
                || (value=="yes") || (value=="YES") || (value=="Yes"); //
        } else if constexpr (std::is_floating_point_v<T>) {
            v = static_cast<T>(std::stod(value.data()));
        } else if constexpr (std::is_signed_v<T>) {
            v = static_cast<T>(std::stoll(value.data()));
        } else if constexpr (std::is_unsigned_v<T>) {
            v = static_cast<T>(std::stoull(value.data()));
        } else {
            LOG(error) << "unknown  type for parameter: field = " << name << " value = " << value;
            return;
        }

        if (!isNewValue) {
            const auto &v0 = GetProperty<T>(name.data());
            if constexpr (std::is_floating_point_v<T>) {
                isNewValue = std::abs(v0 - v) > std::numeric_limits<T>::epsilon();
            } else {
                isNewValue = v0!=v;
            }
        }
        if (isNewValue) {
            LOG(info) << " new parameter: field = " << name << ", value = " << value;
            SetProperty<T>(name.data(), v);
        }
    }
    void SubscribeToParameterChange();
    void ToArray(std::string_view name, std::string line);
    void ToMap(std::string_view name, std::string line);
};

//_____________________________________________________________________________
auto ParameterConfigPluginProgramOptions() -> fair::mq::Plugin::ProgOptions;

REGISTER_FAIRMQ_PLUGIN(
    ParameterConfigPlugin,
    parameter_config,
(fair::mq::Plugin::Version{0, 0, 0}),
"ParameterConfig <maintainer@daq.service.net>",
"https://github.com/spadi-alliance/nestdaq",
daq::service::ParameterConfigPluginProgramOptions
) // end of macro: REGISTER_FAIRMQ_PLUGIN

} // namespace daq::service

#endif
