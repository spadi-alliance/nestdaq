#pragma once

#include <nestdaq/telemetry/OpenTelemetryInitializer.h>
#include <nestdaq/telemetry/Telemetry.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace boost::program_options {
class options_description;
class variables_map;
} // namespace boost::program_options

namespace fair::mq {
class ProgOptions;
} // namespace fair::mq

namespace spdlog::sinks {
class sink;
} // namespace spdlog::sinks

namespace nestdaq::telemetry {

/** Default soname loaded by NestDAQ executables when telemetry is enabled. */
inline constexpr std::string_view kDefaultTelemetryLibrary{"libnestdaq_otel.so"};
inline constexpr std::string_view kDefaultProtocol{"console"};
inline constexpr std::string_view kDefaultLogHttpEndpoint{"http://localhost:4318/v1/logs"};
inline constexpr std::string_view kDefaultMetricHttpEndpoint{"http://localhost:4318/v1/metrics"};
inline constexpr std::string_view kDefaultTraceHttpEndpoint{"http://localhost:4318/v1/traces"};
inline constexpr std::string_view kDefaultGrpcEndpoint{"localhost:4317"};
inline constexpr uint32_t kDefaultTimeoutMs{5000};
inline constexpr uint32_t kDefaultMetricExportIntervalMs{1000};
inline constexpr std::string_view kTelemetryConfigSubscriber{"nestdaq-telemetry"};
inline constexpr std::string_view kDefaultServiceNamespace{"nestdaq"};

/**
 * @brief Runtime options used to configure the OpenTelemetry implementation library.
 *
 * Values are initialized from defaults, optionally overridden by environment
 * variables and command-line/FairMQ options. `makeConfig()` returns borrowed
 * pointers into this object, so the object must outlive the call to
 * `TelemetryLibrary::initializeWith()`.
 */
struct TelemetryOptions {
    std::string library{kDefaultTelemetryLibrary};
    std::string log_protocol{kDefaultProtocol};
    std::string metric_protocol;
    std::string trace_protocol;
    std::string log_endpoint_http{kDefaultLogHttpEndpoint};
    std::string log_endpoint_grpc{kDefaultGrpcEndpoint};
    std::string metric_endpoint_http{kDefaultMetricHttpEndpoint};
    std::string metric_endpoint_grpc{kDefaultGrpcEndpoint};
    std::string trace_endpoint_http{kDefaultTraceHttpEndpoint};
    std::string trace_endpoint_grpc{kDefaultGrpcEndpoint};
    std::string log_headers;
    std::string metric_headers;
    std::string trace_headers;
    std::string spdlog_console_pattern{kDefaultSpdlogConsolePattern};
    bool spdlog_native_console{true};
    bool spdlog_async{false};
    uint32_t spdlog_async_queue_size{kDefaultSpdlogAsyncQueueSize};
    uint32_t spdlog_async_thread_count{kDefaultSpdlogAsyncThreadCount};
    std::string spdlog_async_overflow_policy{kDefaultSpdlogAsyncOverflowPolicy};
    std::string severity{"info"};
    std::string service_name{"nestdaq"};
    std::string service_namespace{kDefaultServiceNamespace};
    std::string service_instance_id;
    std::string host_name;
    std::string nestdaq_instance_id;
    std::string nestdaq_instance_id_status{"unresolved"};
    std::string fairmq_id;
    std::string fairmq_device;
    std::string fairmq_session;
    std::string fairmq_transport;
    uint32_t timeout_ms{kDefaultTimeoutMs};
    uint32_t metric_export_interval_ms{kDefaultMetricExportIntervalMs};
    uint32_t log_otlp_http_json{1};
    uint32_t metric_otlp_http_json{1};
    uint32_t trace_otlp_http_json{1};
    bool required{false};
    bool generated_service_instance_id{false};
};

/**
 * @brief Result of converting a severity name into a FairLogger severity value.
 */
struct SeverityParseResult {
    int32_t value{};
    bool used_fallback{false};
};

/**
 * @brief Add command-line options that configure the optional OpenTelemetry implementation library.
 */
auto addTelemetryOptions(boost::program_options::options_description& options,
                         std::string_view default_service_name = "nestdaq") -> void;
/**
 * @brief Apply `NESTDAQ_OTEL_*` environment variables to @p options.
 */
auto applyEnvironment(TelemetryOptions& options) -> void;
/**
 * @brief Assign one parsed command-line, FairMQ, or environment option.
 */
auto assignOption(TelemetryOptions& options,
                  std::string_view key,
                  std::string_view value) -> void;
/** @brief Return the final path component of an executable path. */
auto basename(std::string_view path) -> std::string_view;
/** @brief Read an environment variable as a nullable borrowed C string. */
auto env(const char* name) -> const char*;
/** @brief Detect the current host name for the OTel host.name resource attribute. */
auto detectHostName() -> std::string;
/** @brief Detect and store host.name when it has not already been set. */
auto ensureHostName(TelemetryOptions& options) -> void;
/** @brief Generate and store a service instance id when the user did not set one. */
auto ensureServiceInstanceId(TelemetryOptions& options) -> void;
/** @brief Generate a UUID string for the default service instance id. */
auto generateUuidString() -> std::string;
/**
 * @brief Build the C ABI configuration consumed by `libnestdaq_otel.so`.
 *
 * The returned struct contains string pointers borrowed from @p options, so the
 * options object must outlive the immediate `TelemetryLibrary::initializeWith()`
 * call that consumes the config.
 */
auto makeConfig(const TelemetryOptions& options) -> nestdaq_otel_config;
/**
 * @brief Build one signal-specific C ABI exporter configuration.
 *
 * The returned struct borrows the supplied string_view storage.
 */
auto makeSignalConfig(std::string_view protocol,
                      std::string_view endpoint_http,
                      std::string_view endpoint_grpc,
                      std::string_view headers,
                      uint32_t otlp_http_json) -> nestdaq_otel_signal_config;
/** @brief Parse common true values such as `1`, `true`, `on`, and `yes`. */
auto parseBool(std::string_view value) -> bool;
/**
 * @brief Parse process arguments into telemetry options.
 *
 * This accepts both `--otel-*` options and selected DAQ aliases such as
 * `--service-name` and `--uuid` so telemetry resource attributes match FairMQ
 * device identity by default.
 */
auto parseTelemetryOptions(int argc, char* argv[], // NOLINT(cppcoreguidelines-avoid-c-arrays)
                           std::string_view default_service_name = "nestdaq") -> TelemetryOptions;
/** @brief Convert a FairLogger severity name to its numeric value. */
auto parseFairLoggerSeverity(std::string_view severity) -> SeverityParseResult;
/** @brief Parse an unsigned integer option with a fallback on invalid input. */
auto parseUInt32(std::string_view value, uint32_t fallback) -> uint32_t;
/**
 * @brief Read telemetry options from a Boost variables_map after option parsing.
 */
auto readTelemetryOptions(const boost::program_options::variables_map& vm,
                          std::string_view default_service_name) -> TelemetryOptions;
/** @brief Emit a warning when an unknown severity name falls back to info. */
auto warnUnknownSeverityFallback(std::string_view severity) -> void;
/** @brief Return the numeric FairLogger severity value for @p severity. */
auto severityToFairLoggerValue(std::string_view severity) -> int32_t;
/**
 * @brief Mirror a generated telemetry UUID into FairMQ ProgOptions.
 *
 * Explicit user-provided FairMQ UUID values are preserved.
 */
auto setGeneratedUuidProperty(fair::mq::ProgOptions& config,
                              const TelemetryOptions& options,
                              std::string_view key = "uuid") -> void;

/**
 * @brief Runtime loader for the optional OpenTelemetry implementation shared library.
 *
 * `TelemetryLibrary` owns the `dlopen()` handle and resolves the required
 * `nestdaq_otel_*` C ABI symbols. It is intentionally non-copyable and
 * non-movable because the telemetry state is process-wide. Destruction shuts down
 * telemetry once and then closes the shared library.
 */
class TelemetryLibrary {
public:
    TelemetryLibrary() = default;
    TelemetryLibrary(const TelemetryLibrary&) = delete;
    auto operator=(const TelemetryLibrary&) -> TelemetryLibrary& = delete;
    TelemetryLibrary(TelemetryLibrary&&) = delete;
    auto operator=(TelemetryLibrary&&) -> TelemetryLibrary& = delete;
    ~TelemetryLibrary();

    /** @brief Return the last loader or implementation-library error message. */
    auto getLastError() const -> const std::string&;
    /**
     * @brief Initialize the loaded implementation library with a C ABI config.
     *
     * @p config may contain borrowed pointers because the implementation copies the
     * values it needs during initialization.
     */
    auto initializeWith(const nestdaq_otel_config& config) -> bool;
    /** @brief Force-flush initialized telemetry providers. */
    auto forceFlush(uint64_t timeout_ms) -> bool;
    /**
     * @brief Create the optional spdlog OpenTelemetry sink from the loaded implementation library.
     *
     * Returns null when the library does not provide spdlog instrumentation or
     * when OTel log export is disabled.
     */
    auto createSpdlogSink() const -> std::shared_ptr<spdlog::sinks::sink>;
    /** @brief Record a FairMQ state transition as a framework metric sample. */
    auto recordFrameworkFairMQState(int64_t state_id, std::string_view state_name) -> void;
    /** @brief Add to a user double counter through the implementation library C ABI. */
    auto metricAddDoubleCounter(std::string_view name,
                                double value,
                                std::string_view unit = "",
                                std::string_view description = "",
                                const nestdaq_otel_attribute* attributes = nullptr,
                                uint64_t attribute_count = 0) -> bool;
    /** @brief Record a user double histogram value through the implementation library C ABI. */
    auto metricRecordDoubleHistogram(std::string_view name,
                                     double value,
                                     std::string_view unit = "",
                                     std::string_view description = "",
                                     const nestdaq_otel_attribute* attributes = nullptr,
                                     uint64_t attribute_count = 0) -> bool;
    /** @brief Record a user double gauge value through the implementation library C ABI. */
    auto metricRecordDoubleGauge(std::string_view name,
                                 double value,
                                 std::string_view unit = "",
                                 std::string_view description = "",
                                 const nestdaq_otel_attribute* attributes = nullptr,
                                 uint64_t attribute_count = 0) -> bool;
    /**
     * @brief Load the OpenTelemetry implementation shared library and resolve its C ABI.
     *
     * The library is optional in normal NestDAQ startup; callers decide whether
     * a failed load is fatal based on their runtime options.
     */
    auto load(const std::string& library) -> bool;
    /** @brief End a span handle previously returned by @ref spanStart. */
    auto spanEnd(uint64_t span_handle) -> bool;
    /** @brief Set one attribute on an active span handle. */
    auto spanSetAttribute(uint64_t span_handle, const nestdaq_otel_attribute& attribute) -> bool;
    /** @brief Update the NestDAQ instance id attached to exported log records. */
    auto setNestdaqInstanceId(std::string_view instance_id) -> bool;
    /** @brief Start a span and return its opaque implementation-owned handle. */
    auto spanStart(std::string_view name,
                   const nestdaq_otel_attribute* attributes = nullptr,
                   uint64_t attribute_count = 0) -> uint64_t;
    /** @brief Update the FairLogger severity threshold by severity name. */
    auto setMinSeverity(std::string_view severity) -> bool;
    /** @brief Update the FairLogger severity threshold by numeric value. */
    auto setMinSeverity(int32_t severity) -> bool;
    /** @brief Shut down the implementation library once; subsequent calls are no-ops. */
    auto shutdownTelemetry(uint64_t timeout_ms) const -> void;

private:
    auto storeResult(int rc) -> bool;

    void* fHandle{nullptr};
    std::function<int(const nestdaq_otel_config*)> fInitialize;
    std::function<int(uint64_t)> fForceFlush;
    std::function<std::shared_ptr<spdlog::sinks::sink>()> fCreateSpdlogSink;
    std::function<int(uint64_t)> fShutdown;
    std::function<const char*()> fLastErrorFunction;
    std::function<void(int64_t, const char*)> fRecordFrameworkFairMQState;
    std::function<int(int32_t)> fSetMinSeverity;
    std::function<int(const char*)> fSetNestdaqInstanceId;
    std::function<int(const char*, double, const char*, const char*, const nestdaq_otel_attribute*, uint64_t)> fMetricAddDoubleCounter;
    std::function<int(const char*, double, const char*, const char*, const nestdaq_otel_attribute*, uint64_t)> fMetricRecordDoubleHistogram;
    std::function<int(const char*, double, const char*, const char*, const nestdaq_otel_attribute*, uint64_t)> fMetricRecordDoubleGauge;
    std::function<int(uint64_t)> fSpanEnd;
    std::function<int(uint64_t, const nestdaq_otel_attribute*)> fSpanSetAttribute;
    std::function<uint64_t(const char*, const nestdaq_otel_attribute*, uint64_t)> fSpanStart;
    mutable bool fShutdownCalled{false};
    mutable bool fLogExportEnabled{false};
    std::string fLastError;
};

/**
 * @brief Subscribe to FairMQ property changes that affect telemetry runtime state.
 */
auto subscribeTelemetryOptionChanges(const fair::mq::ProgOptions& config,
                                     TelemetryLibrary& telemetry) -> void;
/** @brief Remove the telemetry property-change subscription from FairMQ options. */
auto unsubscribeTelemetryOptionChanges(const fair::mq::ProgOptions& config) -> void;

} // namespace nestdaq::telemetry
