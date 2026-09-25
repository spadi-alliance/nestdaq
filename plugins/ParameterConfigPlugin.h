#pragma once

/**
 * @file ParameterConfigPlugin.h
 * @brief FairMQ plugin that mirrors Redis parameter values into ProgOptions.
 */

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

namespace nestdaq::daq::service {

/** @brief Redis key prefix that stores parameter configuration. */
static constexpr std::string_view kParametersPrefix{"parameters"};

/**
 * @brief FairMQ plugin that mirrors Redis parameter values into ProgOptions.
 *
 * The plugin reads initial values at startup and subscribes to Redis keyspace
 * changes so DAQ runtime parameters can be updated without restarting devices.
 */
class ParameterConfigPlugin : public fair::mq::Plugin
{
public:
    /** @brief Command-line option names for parameter configuration. */
    struct OptionKey {
        static constexpr std::string_view kServerUri{"parameter-config-uri"};
    };

    /** @brief Construct and initialize the Redis-backed parameter config plugin. */
    ParameterConfigPlugin(std::string_view name,
                          const fair::mq::Plugin::Version &version,
                          std::string_view maintainer,
                          std::string_view homepage,
                          fair::mq::PluginServices *pluginServices);
    ParameterConfigPlugin(const ParameterConfigPlugin&) = delete;
    ParameterConfigPlugin& operator=(const ParameterConfigPlugin&) = delete;
    ParameterConfigPlugin(ParameterConfigPlugin&&) = delete;
    ParameterConfigPlugin& operator=(ParameterConfigPlugin&&) = delete;
    ~ParameterConfigPlugin() override;

private:
    std::shared_ptr<sw::redis::Redis> fClient;

    std::string fId;
    std::string fSeparator;
    std::string fKey;
    std::string fGroupKey;
    std::thread fSubscriberThread;
    std::atomic<bool> fPluginShutdownRequested{false};

    /** @brief Return true for options that must not be overwritten from Redis. */
    static bool isReservedOption(std::string_view name);
    /** @brief Parse one Redis value and dispatch to the appropriate type converter. */
    void parse(std::string_view name, std::string line);
    /** @brief Read a Redis hash into FairMQ properties. */
    void readHash(const std::string& name);
    /** @brief Read a Redis list into an indexed FairMQ property array. */
    void readList(const std::string& name);
    /** @brief Read all configured parameter keys from Redis at startup. */
    void readParameters();
    /** @brief Read a Redis set into a FairMQ property array. */
    void readSet(const std::string& name);
    /** @brief Read a Redis string into a FairMQ property. */
    void readString(const std::string& name);
    /** @brief Read a Redis sorted set into an ordered FairMQ property array. */
    void readZset(const std::string& name);
    /** @brief Apply reserved options through their dedicated FairMQ property path. */
    void setPropertyOfReservedOption(std::string_view name, std::string_view value);
    /**
     * @brief Convert one Redis string value to the requested FairMQ property type.
     *
     * Existing values are updated only when the converted value differs.
     */
    template <typename T>
    void setPropertyFromString(std::string_view name, std::string_view value)
    {
        auto is_new_value = !PropertyExists(name.data());
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

        if (!is_new_value) {
            const auto &v0 = GetProperty<T>(name.data());
            if constexpr (std::is_floating_point_v<T>) {
                is_new_value = std::abs(v0 - v) > std::numeric_limits<T>::epsilon();
            } else {
                is_new_value = v0!=v;
            }
        }
        if (is_new_value) {
            LOG(info) << " new parameter: field = " << name << ", value = " << value;
            SetProperty<T>(name.data(), v);
        }
    }
    /** @brief Subscribe to Redis parameter key changes and update properties. */
    void subscribeToParameterChange();
    /** @brief Convert a comma-separated value into an indexed property array. */
    void toArray(std::string_view name, std::string line);
    /** @brief Convert a comma-separated key/value list into FairMQ properties. */
    void toMap(std::string_view name, std::string line);
};

/**
 * @brief Declare parameter configuration plugin command-line options.
 */
auto parameterConfigPluginProgramOptions() -> fair::mq::Plugin::ProgOptions;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
REGISTER_FAIRMQ_PLUGIN(
    ParameterConfigPlugin,
    parameter_config,
(fair::mq::Plugin::Version{0, 0, 0}),
"ParameterConfig <maintainer@daq.service.net>",
"https://github.com/spadi-alliance/nestdaq",
nestdaq::daq::service::parameterConfigPluginProgramOptions
) // end of macro: REGISTER_FAIRMQ_PLUGIN

} // namespace nestdaq::daq::service
