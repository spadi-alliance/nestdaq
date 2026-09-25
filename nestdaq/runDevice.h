#pragma once

/**
 * @file runDevice.h
 * @brief NestDAQ replacement entry point for FairMQ devices.
 *
 * Including this header defines `main()` and expects the application to provide
 * `addCustomOptions()` and `getDevice()`. The wrapper installs NestDAQ
 * telemetry options before constructing and running the FairMQ device.
 */

#include <fairmq/DeviceRunner.h>
#include <fairlogger/Logger.h>

#include <boost/program_options.hpp>

#include <nestdaq/telemetry/FairLoggerTelemetryLoader.h>
#include <nestdaq/telemetry/Telemetry.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Add application-specific command-line options.
 *
 * The function must be implemented by the executable that includes
 * `nestdaq/runDevice.h`.
 */
void addCustomOptions(boost::program_options::options_description& options);

/**
 * @brief Create the FairMQ device instance for the application.
 *
 * The function must return exclusive ownership of a `fair::mq::Device`
 * subclass. It is called after command-line options have been registered.
 */
std::unique_ptr<fair::mq::Device> getDevice(const fair::mq::ProgOptions& config);

namespace nestdaq::run_device_detail {

inline constexpr std::string_view kTelemetryStateSubscriber{"nestdaq-otel-framework-state"};

struct ProgramArguments {
    std::vector<std::string> storage;
    std::vector<char*> argv;

    auto argc() const -> int {
        return static_cast<int>(argv.size());
    }
};

auto normalizeArguments(int argc, char* argv[]) -> ProgramArguments { // NOLINT(cppcoreguidelines-avoid-c-arrays)
    auto arguments = ProgramArguments{};
    arguments.storage.reserve(static_cast<std::size_t>(argc));
    arguments.argv.reserve(static_cast<std::size_t>(argc));

    // Keep normalized arguments in owned storage so both telemetry option
    // parsing and FairMQ DeviceRunner receive stable argv pointers.
    for (int i = 0; i < argc; ++i) {
        const auto kArg = std::string_view{argv[i]}; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        if (kArg == "--otel-log-protocol=") {
            // Treat an explicit empty log protocol as the option's implicit
            // empty value before handing the same argv to FairMQ.
            arguments.storage.emplace_back("--otel-log-protocol");
        } else {
            arguments.storage.emplace_back(argv[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        }
    }

    for (auto& arg : arguments.storage) {
        arguments.argv.emplace_back(arg.data());
    }

    return arguments;
}

} // namespace nestdaq::run_device_detail

int main(int argc, char* argv[]) {
    using namespace fair::mq;
    using namespace fair::mq::hooks;

    try {
        auto arguments = nestdaq::run_device_detail::normalizeArguments(argc, argv);
        const auto kTelemetryOptions =
            nestdaq::telemetry::parseTelemetryOptions(arguments.argc(), arguments.argv.data(), "nestdaq");
        nestdaq::telemetry::setSpdlogConsolePattern(kTelemetryOptions.spdlog_console_pattern);
        nestdaq::telemetry::setSpdlogNativeConsoleEnabled(kTelemetryOptions.spdlog_native_console);
        nestdaq::telemetry::setSpdlogAsyncOptions({
            .enabled = kTelemetryOptions.spdlog_async,
            .queue_size = kTelemetryOptions.spdlog_async_queue_size,
            .thread_count = kTelemetryOptions.spdlog_async_thread_count,
            .overflow_policy = kTelemetryOptions.spdlog_async_overflow_policy,
        });
        auto telemetry = std::make_unique<nestdaq::telemetry::TelemetryLibrary>();
        auto telemetry_loaded = false;
        auto telemetry_initialized = false;
        auto telemetry_resolved = false;

        if (!kTelemetryOptions.library.empty()) {
            telemetry_loaded = telemetry->load(kTelemetryOptions.library);
            if (!telemetry_loaded) {
                LOG(error) << "Failed to load telemetry library '" << kTelemetryOptions.library
                           << "': " << telemetry->getLastError();
                if (kTelemetryOptions.required) {
                    return EXIT_FAILURE;
                }
            } else {
                auto unresolved_options = kTelemetryOptions;
                unresolved_options.metric_protocol.clear();
                unresolved_options.trace_protocol.clear();
                unresolved_options.nestdaq_instance_id.clear();
                unresolved_options.nestdaq_instance_id_status = "unresolved";
                const auto kConfig = nestdaq::telemetry::makeConfig(unresolved_options);
                if (!telemetry->initializeWith(kConfig)) {
                    LOG(error) << "Failed to initialize telemetry library '" << kTelemetryOptions.library
                               << "': " << telemetry->getLastError();
                    if (kTelemetryOptions.required) {
                        return EXIT_FAILURE;
                    }
                } else {
                    telemetry_initialized = true;
                    nestdaq::telemetry::warnUnknownSeverityFallback(kTelemetryOptions.severity);
                }
            }
        }

        DeviceRunner runner{arguments.argc(), arguments.argv.data(), false};

        runner.AddHook<SetCustomCmdLineOptions>([telemetry_initialized, telemetry = telemetry.get()](DeviceRunner& r) {
            boost::program_options::options_description customOptions("Custom options");
            addCustomOptions(customOptions);
            r.fConfig.AddToCmdLineOptions(customOptions);

            boost::program_options::options_description otelOptions("OpenTelemetry options");
            nestdaq::telemetry::addTelemetryOptions(otelOptions, "nestdaq");
            r.fConfig.AddToCmdLineOptions(otelOptions);

            if (telemetry_initialized) {
                nestdaq::telemetry::subscribeTelemetryOptionChanges(r.fConfig, *telemetry);
            }
        });

        runner.AddHook<InstantiateDevice>([&kTelemetryOptions,
                                           &telemetry_resolved,
                                           telemetry_initialized,
        telemetry = telemetry.get()](DeviceRunner& r) {
            nestdaq::telemetry::setGeneratedUuidProperty(r.fConfig, kTelemetryOptions);
            r.fDevice = getDevice(r.fConfig);
            if (telemetry_initialized && r.fConfig.Count("id") != 0) {
                auto resolved_options = kTelemetryOptions;
                resolved_options.nestdaq_instance_id = r.fConfig.GetProperty<std::string>("id");
                resolved_options.nestdaq_instance_id_status = "resolved";
                const auto kConfig = nestdaq::telemetry::makeConfig(resolved_options);
                if (!telemetry->initializeWith(kConfig)) {
                    LOG(error) << "Failed to reinitialize telemetry with NestDAQ instance id '"
                               << resolved_options.nestdaq_instance_id << "': " << telemetry->getLastError();
                    if (kTelemetryOptions.required) {
                        throw std::runtime_error{"failed to reinitialize required telemetry"};
                    }
                } else {
                    telemetry_resolved = true;
                    nestdaq::telemetry::setActiveTelemetryLibrary(telemetry);
                    telemetry->setNestdaqInstanceId(resolved_options.nestdaq_instance_id);
                }
            }
            if (telemetry_initialized && telemetry_resolved && r.fDevice) {
                r.fDevice->SubscribeToStateChange(
                    std::string{nestdaq::run_device_detail::kTelemetryStateSubscriber},
                [telemetry](const fair::mq::State kNewState) {
                    telemetry->recordFrameworkFairMQState(
                        static_cast<int64_t>(kNewState),
                        fair::mq::GetStateName(kNewState));
                });
            }
        });

        const auto kRc = runner.Run();
        if (telemetry_initialized && telemetry_resolved && runner.fDevice) {
            runner.fDevice->UnsubscribeFromStateChange(
                std::string{nestdaq::run_device_detail::kTelemetryStateSubscriber});
        }
        if (telemetry_initialized) {
            nestdaq::telemetry::unsubscribeTelemetryOptionChanges(runner.fConfig);
        }
        if (telemetry_loaded) {
            nestdaq::telemetry::setActiveTelemetryLibrary(nullptr);
            telemetry->shutdownTelemetry(kTelemetryOptions.timeout_ms);
        }
        return kRc;
    } catch (std::exception& e) {
        LOG(error) << "Uncaught exception reached the top of main: " << e.what();
        return EXIT_FAILURE;
    } catch (...) {
        LOG(error) << "Uncaught exception reached the top of main.";
        return EXIT_FAILURE;
    }
}
