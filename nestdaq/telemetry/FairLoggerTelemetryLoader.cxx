/** @file
 *  @brief Implements telemetry option parsing and the OpenTelemetry shared-library loader.
 */

#include "nestdaq/telemetry/FairLoggerTelemetryLoader.h"

#include <fairmq/ProgOptions.h>
#include <fairmq/Version.h>
#include <fairlogger/Logger.h>

#include <boost/program_options.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <algorithm>
#include <array>
#include <dlfcn.h>

#include <cstdlib>
#include <unistd.h>

namespace nestdaq::telemetry {
namespace {
constexpr auto kHostNameBufferSize = std::size_t{256};

/**
 * @brief Resolve one symbol from the loaded OpenTelemetry implementation library.
 *
 * The result is wrapped in `std::function` so callers can store optional C ABI
 * entries uniformly and test whether a symbol was present before calling it.
 */
template<typename T>
auto resolveSymbol(void* handle, const char* symbol) -> std::function<T> {
    dlerror(); // NOLINT(concurrency-mt-unsafe)
    return reinterpret_cast<T*>(dlsym(handle, symbol)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

auto isValidSpdlogAsyncOverflowPolicy(std::string_view value) -> bool {
    return value == "block" || value == "overrun_oldest" || value == "discard_new";
}

auto normalizeSpdlogAsyncOptions(TelemetryOptions& options) -> void {
    if (options.spdlog_async_queue_size == 0) {
        options.spdlog_async_queue_size = kDefaultSpdlogAsyncQueueSize;
    }
    if (options.spdlog_async_thread_count == 0) {
        options.spdlog_async_thread_count = kDefaultSpdlogAsyncThreadCount;
    }
    if (!isValidSpdlogAsyncOverflowPolicy(options.spdlog_async_overflow_policy)) {
        options.spdlog_async_overflow_policy = kDefaultSpdlogAsyncOverflowPolicy;
    }
}

} // namespace

auto addTelemetryOptions(boost::program_options::options_description& options,
                         std::string_view default_service_name) -> void {
    namespace bpo = boost::program_options;
    options.add_options()
           ("otel-library", bpo::value<std::string>()->default_value(std::string{kDefaultTelemetryLibrary}), "Telemetry shared library path or soname to dlopen")
           ("otel-log-protocol", bpo::value<std::string>()->default_value(std::string{kDefaultProtocol})->implicit_value(""), "Comma-separated OTel log exporter protocols to enable: console, otlp-http, otlp-grpc. Empty disables OTel output")
           ("otel-metric-protocol", bpo::value<std::string>()->default_value("")->implicit_value(""), "Comma-separated OTel metric exporter protocols to enable: console, otlp-http, otlp-grpc. Empty disables metrics")
           ("otel-trace-protocol", bpo::value<std::string>()->default_value("")->implicit_value(""), "Comma-separated OTel trace exporter protocols to enable: console, otlp-http, otlp-grpc. Empty disables traces")
           ("otel-log-endpoint-http", bpo::value<std::string>()->default_value(std::string{kDefaultLogHttpEndpoint}), "OTLP HTTP logs endpoint")
           ("otel-log-endpoint-grpc", bpo::value<std::string>()->default_value(std::string{kDefaultGrpcEndpoint}), "OTLP gRPC logs endpoint")
           ("otel-metric-endpoint-http", bpo::value<std::string>()->default_value(std::string{kDefaultMetricHttpEndpoint}), "OTLP HTTP metrics endpoint")
           ("otel-metric-endpoint-grpc", bpo::value<std::string>()->default_value(std::string{kDefaultGrpcEndpoint}), "OTLP gRPC metrics endpoint")
           ("otel-trace-endpoint-http", bpo::value<std::string>()->default_value(std::string{kDefaultTraceHttpEndpoint}), "OTLP HTTP traces endpoint")
           ("otel-trace-endpoint-grpc", bpo::value<std::string>()->default_value(std::string{kDefaultGrpcEndpoint}), "OTLP gRPC traces endpoint")
           ("otel-log-headers", bpo::value<std::string>(), "OTel exporter headers as comma-separated key=value pairs")
           ("otel-metric-headers", bpo::value<std::string>(), "OTel metric exporter headers as comma-separated key=value pairs")
           ("otel-trace-headers", bpo::value<std::string>(), "OTel trace exporter headers as comma-separated key=value pairs")
           ("otel-log-severity", bpo::value<std::string>()->default_value("info"), "Minimum severity exported to OTel")
           ("otel-log-required", bpo::value<bool>()->default_value(false), "Fail startup if telemetry library cannot be loaded")
           ("otel-timeout-ms", bpo::value<uint32_t>()->default_value(kDefaultTimeoutMs), "OTel force-flush/shutdown timeout in milliseconds")
           ("otel-metric-export-interval-ms", bpo::value<uint32_t>()->default_value(kDefaultMetricExportIntervalMs), "OTel periodic metric export interval in milliseconds")
           ("otel-log-http-json", bpo::value<bool>()->default_value(true), "Use JSON content type for OTLP HTTP logs")
           ("otel-metric-http-json", bpo::value<bool>()->default_value(true), "Use JSON content type for OTLP HTTP metrics")
           ("otel-trace-http-json", bpo::value<bool>()->default_value(true), "Use JSON content type for OTLP HTTP traces")
           ("otel-service-name", bpo::value<std::string>()->default_value(std::string{default_service_name}), "OTel service.name resource attribute")
           ("otel-service-namespace", bpo::value<std::string>()->default_value(std::string{kDefaultServiceNamespace}), "OTel service.namespace resource attribute")
           ("otel-service-instance-id", bpo::value<std::string>(), "OTel service.instance.id resource attribute")
           ("otel-fairmq-id", bpo::value<std::string>(), "FairMQ id resource attribute")
           ("otel-fairmq-device", bpo::value<std::string>(), "FairMQ device resource attribute")
           ("otel-fairmq-session", bpo::value<std::string>(), "FairMQ session resource attribute")
           ("otel-fairmq-transport", bpo::value<std::string>(), "FairMQ transport resource attribute")
           ("spdlog-console-pattern", bpo::value<std::string>()->default_value(std::string{kDefaultSpdlogConsolePattern}), "spdlog native console sink pattern")
           ("spdlog-native-console", bpo::value<bool>()->default_value(true), "Enable spdlog native console sink independently from OTel spdlog sink")
           ("spdlog-async", bpo::value<bool>()->default_value(false), "Use spdlog async logger for NestDAQ helper loggers")
           ("spdlog-async-queue-size", bpo::value<uint32_t>()->default_value(kDefaultSpdlogAsyncQueueSize), "spdlog async queue size")
           ("spdlog-async-thread-count", bpo::value<uint32_t>()->default_value(kDefaultSpdlogAsyncThreadCount), "spdlog async worker thread count")
           ("spdlog-async-overflow-policy", bpo::value<std::string>()->default_value(std::string{kDefaultSpdlogAsyncOverflowPolicy}), "spdlog async overflow policy: block, overrun_oldest, discard_new");
}

auto applyEnvironment(TelemetryOptions& options) -> void {
    if (const auto* value = env("NESTDAQ_OTEL_LIBRARY")) {
        options.library = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_LOG_PROTOCOL")) {
        options.log_protocol = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_METRIC_PROTOCOL")) {
        options.metric_protocol = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_TRACE_PROTOCOL")) {
        options.trace_protocol = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_LOG_ENDPOINT_HTTP")) {
        options.log_endpoint_http = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_LOG_ENDPOINT_GRPC")) {
        options.log_endpoint_grpc = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_METRIC_ENDPOINT_HTTP")) {
        options.metric_endpoint_http = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_METRIC_ENDPOINT_GRPC")) {
        options.metric_endpoint_grpc = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_TRACE_ENDPOINT_HTTP")) {
        options.trace_endpoint_http = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_TRACE_ENDPOINT_GRPC")) {
        options.trace_endpoint_grpc = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_LOG_HEADERS")) {
        options.log_headers = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_METRIC_HEADERS")) {
        options.metric_headers = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_TRACE_HEADERS")) {
        options.trace_headers = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_LOG_SEVERITY")) {
        options.severity = value;
    }
    if (const auto* value = env("NESTDAQ_OTEL_LOG_REQUIRED")) {
        options.required = parseBool(value);
    }
    if (const auto* value = env("NESTDAQ_SPDLOG_CONSOLE_PATTERN")) {
        options.spdlog_console_pattern = value;
    }
    if (const auto* value = env("NESTDAQ_SPDLOG_NATIVE_CONSOLE")) {
        options.spdlog_native_console = parseBool(value);
    }
    if (const auto* value = env("NESTDAQ_SPDLOG_ASYNC")) {
        options.spdlog_async = parseBool(value);
    }
    if (const auto* value = env("NESTDAQ_SPDLOG_ASYNC_QUEUE_SIZE")) {
        options.spdlog_async_queue_size = parseUInt32(value, options.spdlog_async_queue_size);
    }
    if (const auto* value = env("NESTDAQ_SPDLOG_ASYNC_THREAD_COUNT")) {
        options.spdlog_async_thread_count = parseUInt32(value, options.spdlog_async_thread_count);
    }
    if (const auto* value = env("NESTDAQ_SPDLOG_ASYNC_OVERFLOW_POLICY")) {
        options.spdlog_async_overflow_policy = value;
    }
}

auto assignOption(TelemetryOptions& options, std::string_view key, std::string_view value) -> void {
    if (key == "otel-library") {
        options.library = value;
    } else if (key == "otel-log-protocol") {
        options.log_protocol = value;
    } else if (key == "otel-metric-protocol") {
        options.metric_protocol = value;
    } else if (key == "otel-trace-protocol") {
        options.trace_protocol = value;
    } else if (key == "otel-log-endpoint-http") {
        options.log_endpoint_http = value;
    } else if (key == "otel-log-endpoint-grpc") {
        options.log_endpoint_grpc = value;
    } else if (key == "otel-metric-endpoint-http") {
        options.metric_endpoint_http = value;
    } else if (key == "otel-metric-endpoint-grpc") {
        options.metric_endpoint_grpc = value;
    } else if (key == "otel-trace-endpoint-http") {
        options.trace_endpoint_http = value;
    } else if (key == "otel-trace-endpoint-grpc") {
        options.trace_endpoint_grpc = value;
    } else if (key == "otel-log-headers") {
        options.log_headers = value;
    } else if (key == "otel-metric-headers") {
        options.metric_headers = value;
    } else if (key == "otel-trace-headers") {
        options.trace_headers = value;
    } else if (key == "otel-log-severity") {
        options.severity = value;
    } else if (key == "otel-log-required") {
        options.required = parseBool(value);
    } else if (key == "otel-timeout-ms") {
        options.timeout_ms = parseUInt32(value, options.timeout_ms);
    } else if (key == "otel-metric-export-interval-ms") {
        options.metric_export_interval_ms = parseUInt32(value, options.metric_export_interval_ms);
    } else if (key == "otel-log-http-json") {
        options.log_otlp_http_json = parseBool(value) ? 1U : 0U;
    } else if (key == "otel-metric-http-json") {
        options.metric_otlp_http_json = parseBool(value) ? 1U : 0U;
    } else if (key == "otel-trace-http-json") {
        options.trace_otlp_http_json = parseBool(value) ? 1U : 0U;
    } else if (key == "otel-service-name") {
        options.service_name = value;
    } else if (key == "otel-service-namespace") {
        options.service_namespace = value;
    } else if (key == "otel-service-instance-id") {
        options.service_instance_id = value;
    } else if (key == "otel-fairmq-id") {
        options.fairmq_id = value;
    } else if (key == "otel-fairmq-device") {
        options.fairmq_device = value;
    } else if (key == "otel-fairmq-session") {
        options.fairmq_session = value;
    } else if (key == "otel-fairmq-transport") {
        options.fairmq_transport = value;
    } else if (key == "spdlog-console-pattern") {
        options.spdlog_console_pattern = value;
    } else if (key == "spdlog-native-console") {
        options.spdlog_native_console = parseBool(value);
    } else if (key == "spdlog-async") {
        options.spdlog_async = parseBool(value);
    } else if (key == "spdlog-async-queue-size") {
        options.spdlog_async_queue_size = parseUInt32(value, options.spdlog_async_queue_size);
    } else if (key == "spdlog-async-thread-count") {
        options.spdlog_async_thread_count = parseUInt32(value, options.spdlog_async_thread_count);
    } else if (key == "spdlog-async-overflow-policy") {
        options.spdlog_async_overflow_policy = value;
    }
}

auto basename(std::string_view path) -> std::string_view {
    const auto kSlash = path.find_last_of("/\\");
    if (kSlash == std::string_view::npos) {
        return path;
    }
    return path.substr(kSlash + 1);
}

auto env(const char* name) -> const char* {
    return std::getenv(name); // NOLINT(concurrency-mt-unsafe)
}

auto detectHostName() -> std::string {
    auto buffer = std::array<char, kHostNameBufferSize> {};
    if (gethostname(buffer.data(), buffer.size()) != 0) {
        return {};
    }
    if (buffer.back() != '\0') {
        return {};
    }
    return std::string{buffer.data()};
}

auto ensureHostName(TelemetryOptions& options) -> void {
    if (!options.host_name.empty()) {
        return;
    }
    options.host_name = detectHostName();
}

auto ensureServiceInstanceId(TelemetryOptions& options) -> void {
    if (!options.service_instance_id.empty()) {
        return;
    }
    options.service_instance_id = generateUuidString();
    options.generated_service_instance_id = true;
}

auto generateUuidString() -> std::string {
    return boost::uuids::to_string(boost::uuids::random_generator{}());
}

auto toLowerAscii(std::string_view value) -> std::string {
    auto lowered = std::string{value};
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return lowered;
}

auto normalizeServiceName(TelemetryOptions& options) -> void {
    options.service_name = toLowerAscii(options.service_name);
}

auto makeConfig(const TelemetryOptions& options) -> nestdaq_otel_config {
    nestdaq_otel_config config{};
    config.size = sizeof(config);
    config.logs = makeSignalConfig(options.log_protocol,
                                   options.log_endpoint_http,
                                   options.log_endpoint_grpc,
                                   options.log_headers,
                                   options.log_otlp_http_json);
    config.metrics = makeSignalConfig(options.metric_protocol,
                                      options.metric_endpoint_http,
                                      options.metric_endpoint_grpc,
                                      options.metric_headers,
                                      options.metric_otlp_http_json);
    config.traces = makeSignalConfig(options.trace_protocol,
                                     options.trace_endpoint_http,
                                     options.trace_endpoint_grpc,
                                     options.trace_headers,
                                     options.trace_otlp_http_json);
    config.service_name = options.service_name.data();
    config.service_namespace = options.service_namespace.data();
    config.service_instance_id = options.service_instance_id.data();
    config.host_name = options.host_name.data();
    config.nestdaq_instance_id = options.nestdaq_instance_id.data();
    config.nestdaq_instance_id_status = options.nestdaq_instance_id_status.data();
    config.fairmq_id = options.fairmq_id.data();
    config.fairmq_device = options.fairmq_device.data();
    config.fairmq_session = options.fairmq_session.data();
    config.fairmq_transport = options.fairmq_transport.data();
    config.fairmq_git_version = FAIRMQ_GIT_VERSION;
    config.fairmq_build_type = FAIRMQ_BUILD_TYPE;
    config.fairmq_repo_url = FAIRMQ_REPO_URL;
    config.fairmq_license = FAIRMQ_LICENSE;
    config.fairmq_copyright = FAIRMQ_COPYRIGHT;
    config.min_severity = parseFairLoggerSeverity(options.severity).value;
    config.timeout_ms = options.timeout_ms;
    config.metric_export_interval_ms = options.metric_export_interval_ms;
    return config;
}

auto makeSignalConfig(std::string_view protocol,
                      std::string_view endpoint_http,
                      std::string_view endpoint_grpc,
                      std::string_view headers,
                      uint32_t otlp_http_json) -> nestdaq_otel_signal_config {
    auto config = nestdaq_otel_signal_config{};
    config.protocol = protocol.data();
    config.endpoint_http = endpoint_http.data();
    config.endpoint_grpc = endpoint_grpc.data();
    config.headers = headers.data();
    config.otlp_http_json = otlp_http_json;
    return config;
}

auto parseBool(std::string_view value) -> bool {
    return value == "1" || value == "true" || value == "TRUE" ||
           value == "on" || value == "ON" || value == "yes" || value == "YES";
}

auto parseTelemetryOptions(int argc, char* argv[], // NOLINT(cppcoreguidelines-avoid-c-arrays)
                           std::string_view default_service_name) -> TelemetryOptions {
    auto options = TelemetryOptions{};
    options.service_name = default_service_name;
    if (argv == nullptr) {
        applyEnvironment(options);
        normalizeSpdlogAsyncOptions(options);
        ensureHostName(options);
        ensureServiceInstanceId(options);
        return options;
    }
    if (argc > 0) {
        if (const auto kExecutable = basename(argv[0]); !kExecutable.empty()) { // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            options.service_name = kExecutable;
        }
    }
    applyEnvironment(options);
    auto explicit_telemetry_service_name = false;
    auto explicit_telemetry_service_instance_id = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view kArg{argv[i]}; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        if (kArg.rfind("--", 0) != 0) {
            continue;
        }

        auto key = kArg.substr(2);
        std::string_view value;
        const auto kEquals = key.find('=');
        if (kEquals != std::string_view::npos) {
            value = key.substr(kEquals + 1);
            key = key.substr(0, kEquals);
        } else if ((key.rfind("otel-", 0) == 0 || key.rfind("spdlog-", 0) == 0 || key == "service-name" || key == "uuid") && i + 1 < argc &&
                   std::string_view{argv[i + 1]}.rfind("--", 0) != 0) { // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            value = argv[++i]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        } else if (key == "otel-log-protocol" || key == "otel-metric-protocol" || key == "otel-trace-protocol") {
            value = "";
        } else {
            continue;
        }
        if (key == "otel-service-name") {
            assignOption(options, key, value);
            explicit_telemetry_service_name = true;
        } else if (key == "service-name") {
            if (!explicit_telemetry_service_name) {
                options.service_name = value;
            }
        } else if (key == "otel-service-instance-id") {
            assignOption(options, key, value);
            options.generated_service_instance_id = false;
            explicit_telemetry_service_instance_id = true;
        } else if (key == "uuid") {
            if (!explicit_telemetry_service_instance_id) {
                options.service_instance_id = value;
                options.generated_service_instance_id = false;
            }
        } else {
            assignOption(options, key, value);
        }
    }

    normalizeServiceName(options);
    normalizeSpdlogAsyncOptions(options);
    ensureHostName(options);
    ensureServiceInstanceId(options);
    return options;
}

auto parseFairLoggerSeverity(std::string_view severity) -> SeverityParseResult {
    if (const auto kIt = fair::Logger::fSeverityMap.find(severity);
            kIt != fair::Logger::fSeverityMap.end()) {
        return SeverityParseResult{
            .value = static_cast<int32_t>(kIt->second),
            .used_fallback = false,
        };
    }
    return SeverityParseResult{
        .value = static_cast<int32_t>(fair::Severity::info),
        .used_fallback = true,
    };
}

auto parseUInt32(std::string_view value, uint32_t fallback) -> uint32_t {
    try {
        return static_cast<uint32_t>(std::stoul(std::string{value}));
    } catch (...) {
        return fallback;
    }
}

auto readTelemetryOptions(const boost::program_options::variables_map& vm,
                          std::string_view default_service_name) -> TelemetryOptions {
    auto options = TelemetryOptions{};
    options.service_name = default_service_name;
    applyEnvironment(options);

    const auto kReadString = [&vm, &options](std::string_view key) {
        const auto kName = std::string{key};
        if (vm.count(kName) != 0 && !vm[kName].defaulted()) {
            assignOption(options, key, vm[kName].as<std::string>());
        }
    };

    kReadString("otel-library");
    kReadString("otel-log-protocol");
    kReadString("otel-metric-protocol");
    kReadString("otel-trace-protocol");
    kReadString("otel-log-endpoint-http");
    kReadString("otel-log-endpoint-grpc");
    kReadString("otel-metric-endpoint-http");
    kReadString("otel-metric-endpoint-grpc");
    kReadString("otel-trace-endpoint-http");
    kReadString("otel-trace-endpoint-grpc");
    kReadString("otel-log-headers");
    kReadString("otel-metric-headers");
    kReadString("otel-trace-headers");
    kReadString("otel-log-severity");
    kReadString("otel-service-name");
    kReadString("otel-service-namespace");
    kReadString("otel-service-instance-id");
    kReadString("otel-fairmq-id");
    kReadString("otel-fairmq-device");
    kReadString("otel-fairmq-session");
    kReadString("otel-fairmq-transport");
    kReadString("spdlog-console-pattern");

    if (vm.count("spdlog-native-console") != 0 && !vm["spdlog-native-console"].defaulted()) {
        options.spdlog_native_console = vm["spdlog-native-console"].as<bool>();
    }
    if (vm.count("spdlog-async") != 0 && !vm["spdlog-async"].defaulted()) {
        options.spdlog_async = vm["spdlog-async"].as<bool>();
    }
    if (vm.count("spdlog-async-queue-size") != 0 && !vm["spdlog-async-queue-size"].defaulted()) {
        options.spdlog_async_queue_size = vm["spdlog-async-queue-size"].as<uint32_t>();
    }
    if (vm.count("spdlog-async-thread-count") != 0 && !vm["spdlog-async-thread-count"].defaulted()) {
        options.spdlog_async_thread_count = vm["spdlog-async-thread-count"].as<uint32_t>();
    }
    kReadString("spdlog-async-overflow-policy");
    if (vm.count("otel-log-required") != 0 && !vm["otel-log-required"].defaulted()) {
        options.required = vm["otel-log-required"].as<bool>();
    }
    if (vm.count("otel-timeout-ms") != 0 && !vm["otel-timeout-ms"].defaulted()) {
        options.timeout_ms = vm["otel-timeout-ms"].as<uint32_t>();
    }
    if (vm.count("otel-metric-export-interval-ms") != 0 && !vm["otel-metric-export-interval-ms"].defaulted()) {
        options.metric_export_interval_ms = vm["otel-metric-export-interval-ms"].as<uint32_t>();
    }
    if (vm.count("otel-log-http-json") != 0 && !vm["otel-log-http-json"].defaulted()) {
        options.log_otlp_http_json = vm["otel-log-http-json"].as<bool>() ? 1U : 0U;
    }
    if (vm.count("otel-metric-http-json") != 0 && !vm["otel-metric-http-json"].defaulted()) {
        options.metric_otlp_http_json = vm["otel-metric-http-json"].as<bool>() ? 1U : 0U;
    }
    if (vm.count("otel-trace-http-json") != 0 && !vm["otel-trace-http-json"].defaulted()) {
        options.trace_otlp_http_json = vm["otel-trace-http-json"].as<bool>() ? 1U : 0U;
    }
    normalizeServiceName(options);
    normalizeSpdlogAsyncOptions(options);
    ensureHostName(options);
    ensureServiceInstanceId(options);
    return options;
}

auto warnUnknownSeverityFallback(std::string_view severity) -> void {
    if (!parseFairLoggerSeverity(severity).used_fallback) {
        return;
    }
    LOG(warn) << "Unknown otel-log-severity '" << severity << "', using FairLogger severity '"
              << fair::Logger::SeverityName(fair::Severity::info) << "'";
}

auto severityToFairLoggerValue(std::string_view severity) -> int32_t {
    return parseFairLoggerSeverity(severity).value;
}

auto setGeneratedUuidProperty(fair::mq::ProgOptions& config,
                              const TelemetryOptions& options,
                              std::string_view key) -> void {
    if (!options.generated_service_instance_id || options.service_instance_id.empty()) {
        return;
    }
    const auto kPropertyKey = std::string{key};
    if (config.Count(kPropertyKey) != 0) {
        return;
    }
    config.SetProperty<std::string>(kPropertyKey, options.service_instance_id);
}

TelemetryLibrary::~TelemetryLibrary() {
    shutdownTelemetry(kDefaultTimeoutMs);
    if (fHandle) {
        dlclose(fHandle);
    }
}

auto TelemetryLibrary::getLastError() const -> const std::string& {
    return fLastError;
}

auto TelemetryLibrary::initializeWith(const nestdaq_otel_config& config) -> bool {
    if (!fInitialize) {
        return false;
    }
    const auto kRc = fInitialize(&config);
    if (kRc != 0) {
        if (fLastErrorFunction) {
            if (const auto* error = fLastErrorFunction()) {
                fLastError = error;
            }
        }
        return false;
    }
    fShutdownCalled = false;
    fLogExportEnabled = config.logs.protocol != nullptr && !std::string_view{config.logs.protocol}.empty();
    fLastError.clear();
    return true;
}

auto TelemetryLibrary::forceFlush(uint64_t timeout_ms) -> bool {
    if (!fForceFlush) {
        return false;
    }
    return storeResult(fForceFlush(timeout_ms));
}

auto TelemetryLibrary::createSpdlogSink() const -> std::shared_ptr<spdlog::sinks::sink> {
    if (!fLogExportEnabled || !fCreateSpdlogSink) {
        return {};
    }
    return fCreateSpdlogSink();
}

auto TelemetryLibrary::recordFrameworkFairMQState(int64_t state_id, std::string_view state_name) -> void {
    if (!fRecordFrameworkFairMQState) {
        return;
    }
    const auto kValue = std::string{state_name};
    fRecordFrameworkFairMQState(state_id, kValue.data());
}

auto TelemetryLibrary::metricAddDoubleCounter(std::string_view name,
        double value,
        std::string_view unit,
        std::string_view description,
        const nestdaq_otel_attribute* attributes,
        uint64_t attribute_count) -> bool {
    if (!fMetricAddDoubleCounter) {
        return false;
    }
    return storeResult(fMetricAddDoubleCounter(name.data(), value, unit.data(), description.data(), attributes, attribute_count));
}

auto TelemetryLibrary::metricRecordDoubleHistogram(std::string_view name,
        double value,
        std::string_view unit,
        std::string_view description,
        const nestdaq_otel_attribute* attributes,
        uint64_t attribute_count) -> bool {
    if (!fMetricRecordDoubleHistogram) {
        return false;
    }
    return storeResult(fMetricRecordDoubleHistogram(name.data(), value, unit.data(), description.data(), attributes, attribute_count));
}

auto TelemetryLibrary::metricRecordDoubleGauge(std::string_view name,
        double value,
        std::string_view unit,
        std::string_view description,
        const nestdaq_otel_attribute* attributes,
        uint64_t attribute_count) -> bool {
    if (!fMetricRecordDoubleGauge) {
        return false;
    }
    return storeResult(fMetricRecordDoubleGauge(name.data(), value, unit.data(), description.data(), attributes, attribute_count));
}

auto TelemetryLibrary::load(const std::string& library) -> bool {
    auto flags = RTLD_NOW | RTLD_LOCAL;
#ifdef RTLD_NODELETE
    // OpenTelemetry providers are process-wide; avoid unmapping implementation-library code
    // while SDK state or background shutdown paths may still reference it.
    flags |= RTLD_NODELETE;
#endif
    fHandle = dlopen(library.data(), flags);
    if (!fHandle) {
        fLastError = dlerror(); // NOLINT(concurrency-mt-unsafe)
        return false;
    }

    fInitialize = resolveSymbol<int(const nestdaq_otel_config*)>(fHandle, "nestdaq_otel_init");
    fForceFlush = resolveSymbol<int(uint64_t)>(fHandle, "nestdaq_otel_force_flush");
    fCreateSpdlogSink = resolveSymbol<std::shared_ptr<spdlog::sinks::sink>()>(fHandle, "nestdaq_otel_create_spdlog_sink");
    fShutdown = resolveSymbol<int(uint64_t)>(fHandle, "nestdaq_otel_shutdown");
    fLastErrorFunction = resolveSymbol<const char*()>(fHandle, "nestdaq_otel_last_error");
    fRecordFrameworkFairMQState = resolveSymbol<void(int64_t, const char*)>(
                                      fHandle, "nestdaq_otel_framework_record_fairmq_state");
    fSetMinSeverity = resolveSymbol<int(int32_t)>(fHandle, "nestdaq_otel_set_min_severity");
    fSetNestdaqInstanceId = resolveSymbol<int(const char*)>(fHandle, "nestdaq_otel_set_nestdaq_instance_id");
    fMetricAddDoubleCounter = resolveSymbol<int(const char*, double, const char*, const char*, const nestdaq_otel_attribute*, uint64_t)>(
                                  fHandle, "nestdaq_otel_metric_add_double_counter");
    fMetricRecordDoubleHistogram = resolveSymbol<int(const char*, double, const char*, const char*, const nestdaq_otel_attribute*, uint64_t)>(
                                       fHandle, "nestdaq_otel_metric_record_double_histogram");
    fMetricRecordDoubleGauge = resolveSymbol<int(const char*, double, const char*, const char*, const nestdaq_otel_attribute*, uint64_t)>(
                                   fHandle, "nestdaq_otel_metric_record_double_gauge");
    fSpanEnd = resolveSymbol<int(uint64_t)>(fHandle, "nestdaq_otel_span_end");
    fSpanSetAttribute = resolveSymbol<int(uint64_t, const nestdaq_otel_attribute*)>(fHandle, "nestdaq_otel_span_set_attribute");
    fSpanStart = resolveSymbol<uint64_t(const char*, const nestdaq_otel_attribute*, uint64_t)>(fHandle, "nestdaq_otel_span_start");

    if (!fInitialize || !fShutdown) {
        fLastError = "telemetry library does not export the required nestdaq_otel_* C ABI";
        dlclose(fHandle);
        fHandle = nullptr;
        fInitialize = nullptr;
        fForceFlush = nullptr;
        fCreateSpdlogSink = nullptr;
        fShutdown = nullptr;
        fLastErrorFunction = nullptr;
        fRecordFrameworkFairMQState = nullptr;
        fSetMinSeverity = nullptr;
        fSetNestdaqInstanceId = nullptr;
        fMetricAddDoubleCounter = nullptr;
        fMetricRecordDoubleHistogram = nullptr;
        fMetricRecordDoubleGauge = nullptr;
        fSpanEnd = nullptr;
        fSpanSetAttribute = nullptr;
        fSpanStart = nullptr;
        return false;
    }

    fLastError.clear();
    return true;
}

auto TelemetryLibrary::spanEnd(uint64_t span_handle) -> bool {
    if (!fSpanEnd) {
        return false;
    }
    return storeResult(fSpanEnd(span_handle));
}

auto TelemetryLibrary::spanSetAttribute(uint64_t span_handle, const nestdaq_otel_attribute& attribute) -> bool {
    if (!fSpanSetAttribute) {
        return false;
    }
    return storeResult(fSpanSetAttribute(span_handle, &attribute));
}

auto TelemetryLibrary::setNestdaqInstanceId(std::string_view instance_id) -> bool {
    if (!fSetNestdaqInstanceId) {
        return false;
    }
    const auto kValue = std::string{instance_id};
    return storeResult(fSetNestdaqInstanceId(kValue.data()));
}

auto TelemetryLibrary::spanStart(std::string_view name,
                                 const nestdaq_otel_attribute* attributes,
                                 uint64_t attribute_count) -> uint64_t {
    if (!fSpanStart) {
        return 0;
    }
    const auto kSpanHandle = fSpanStart(name.data(), attributes, attribute_count);
    if (kSpanHandle == 0) {
        storeResult(NESTDAQ_OTEL_ERROR);
    } else {
        fLastError.clear();
    }
    return kSpanHandle;
}

auto TelemetryLibrary::setMinSeverity(std::string_view severity) -> bool {
    const auto kParsedSeverity = parseFairLoggerSeverity(severity);
    const auto kUpdated = setMinSeverity(kParsedSeverity.value);
    if (kUpdated && kParsedSeverity.used_fallback) {
        warnUnknownSeverityFallback(severity);
    }
    return kUpdated;
}

auto TelemetryLibrary::setMinSeverity(int32_t severity) -> bool {
    if (!fSetMinSeverity) {
        return false;
    }
    const auto kRc = fSetMinSeverity(severity);
    if (kRc != 0) {
        if (fLastErrorFunction) {
            if (const auto* error = fLastErrorFunction()) {
                fLastError = error;
            }
        }
        return false;
    }
    fLastError.clear();
    return true;
}

auto TelemetryLibrary::shutdownTelemetry(uint64_t timeout_ms) const -> void {
    fLogExportEnabled = false;
    if (fShutdown && !fShutdownCalled) {
        fShutdownCalled = true;
        fShutdown(timeout_ms);
    }
}

auto TelemetryLibrary::storeResult(int rc) -> bool {
    if (rc == 0) {
        fLastError.clear();
        return true;
    }
    if (fLastErrorFunction) {
        if (const auto* error = fLastErrorFunction()) {
            fLastError = error;
        }
    }
    return false;
}

auto subscribeTelemetryOptionChanges(const fair::mq::ProgOptions& config,
                                     TelemetryLibrary& telemetry) -> void {
    config.SubscribeAsString(std::string{kTelemetryConfigSubscriber},
    [&telemetry](const fair::mq::PropertyChange::KeyType& key, std::string value) {
        if (key == "id") {
            if (!telemetry.setNestdaqInstanceId(value)) {
                LOG(error) << "Failed to update OTel NestDAQ instance id: "
                           << telemetry.getLastError();
            }
            return;
        }
        if (key == "otel-log-severity" && !telemetry.setMinSeverity(value)) {
            LOG(error) << "Failed to update OTel log severity: "
                       << telemetry.getLastError();
        }
    });
}

auto unsubscribeTelemetryOptionChanges(const fair::mq::ProgOptions& config) -> void {
    config.UnsubscribeAsString(std::string{kTelemetryConfigSubscriber});
}

} // namespace nestdaq::telemetry
