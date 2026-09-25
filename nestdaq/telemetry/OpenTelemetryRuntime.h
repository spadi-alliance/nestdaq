/** @file
 *  @brief Internal OpenTelemetry runtime state and helper declarations.
 *
 * This header is shared only by the `libnestdaq_otel.so` implementation files.
 * It may include OpenTelemetry C++ SDK headers because it is not used by
 * NestDAQ executables that should remain OpenTelemetry-unlinked.
 */

#pragma once

#include "nestdaq/telemetry/OpenTelemetryInitializer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/exporters/otlp/otlp_environment.h>
#include <opentelemetry/exporters/otlp/otlp_http.h>
#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/logs/exporter.h>
#include <opentelemetry/sdk/logs/processor.h>
#include <opentelemetry/sdk/logs/logger_provider.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/push_metric_exporter.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/tracer.h>

namespace nestdaq::telemetry {
struct FairMQThroughputSample;
}

namespace nestdaq::otel_detail {

/** @brief Export protocol selected for one OpenTelemetry signal. */
enum class Protocol : std::uint8_t {
    Console,
    OtlpHttp,
    OtlpGrpc,
};

/** @brief User metric instrument kind used as part of instrument identity. */
enum class MetricKind : std::uint8_t {
    DoubleCounter,
    DoubleHistogram,
    DoubleGauge,
};

inline constexpr std::string_view kDefaultLogProtocol{"console"};
inline constexpr std::string_view kDefaultLogHttpEndpoint{"http://localhost:4318/v1/logs"};
inline constexpr std::string_view kDefaultMetricHttpEndpoint{"http://localhost:4318/v1/metrics"};
inline constexpr std::string_view kDefaultTraceHttpEndpoint{"http://localhost:4318/v1/traces"};
inline constexpr std::string_view kDefaultGrpcEndpoint{"localhost:4317"};
inline constexpr uint32_t kDefaultMetricExportIntervalMs{1000};

/**
 * @brief Owns strings backing an OpenTelemetry key/value iterable view.
 */
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct AttributeStorage {
    std::vector<std::string> keys;
    std::vector<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>> values;
};

/**
 * @brief Stable key for lazily created user metric instruments.
 */
struct MetricKey {
    MetricKind kind{MetricKind::DoubleCounter};
    std::string name;
    std::string unit;
    std::string description;

    auto operator<(const MetricKey &other) const -> bool
    {
        return std::tie(kind, name, unit, description) <
        std::tie(other.kind, other.name, other.unit, other.description);
    }

    auto operator==(const MetricKey &other) const -> bool
    {
        return std::tie(kind, name, unit, description) ==
        std::tie(other.kind, other.name, other.unit, other.description);
    }
};

/**
 * @brief Comparable representation of one gauge attribute.
 *
 * Observable gauge samples are keyed by full attribute set, so attributes need
 * value semantics independent of the borrowed C ABI pointers passed by callers.
 */
struct GaugeAttribute {
    std::string key;
    nestdaq_otel_attribute_type type{NESTDAQ_OTEL_ATTRIBUTE_STRING};
    std::string string_value;
    int64_t int_value{0};
    uint64_t uint_value{0};
    double double_value{0.0};
    bool bool_value{false};

    auto operator<(const GaugeAttribute &other) const -> bool
    {
        return std::tie(key, type, string_value, int_value, uint_value, double_value, bool_value) <
        std::tie(other.key,
                 other.type,
                 other.string_value,
                 other.int_value,
                 other.uint_value,
                 other.double_value,
                 other.bool_value);
    }

    auto operator==(const GaugeAttribute &other) const -> bool
    {
        return std::tie(key, type, string_value, int_value, uint_value, double_value, bool_value) ==
        std::tie(other.key,
                 other.type,
                 other.string_value,
                 other.int_value,
                 other.uint_value,
                 other.double_value,
                 other.bool_value);
    }
};

/**
 * @brief Map key for the latest user gauge value for one instrument/attribute set.
 */
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct GaugeSampleKey {
    MetricKey metric;
    std::vector<GaugeAttribute> attributes;

    auto operator<(const GaugeSampleKey &other) const -> bool
    {
        return std::tie(metric, attributes) < std::tie(other.metric, other.attributes);
    }
};

/** @brief Stored latest user gauge value and its owned attributes. */
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct GaugeMeasurement {
    std::vector<GaugeAttribute> attributes;
    double value{0.0};
};

/**
 * @brief Observable gauge plus the key used by its callback to find samples.
 */
struct GaugeInstrument {
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> instrument;
    MetricKey callback_key;
};

/**
 * @brief One pending FairMQ throughput sample for framework metrics export.
 */
struct FairMQThroughputMeasurement {
    std::string channel_name;
    std::string sub_channel_name;
    std::string direction;
    std::optional<uint64_t> sub_channel_index;
    double messages_per_second = 0.0;
    double megabytes_per_second = 0.0;
};

/** @brief One pending process metrics sample for framework metrics export. */
struct ProcessUsageMeasurement {
    double cpu_user_seconds = 0.0;
    double cpu_system_seconds = 0.0;
    std::optional<double> cpu_utilization;
    double memory_usage_bytes = 0.0;
};

/** @brief Previous process CPU sample used to compute usage deltas. */
struct ProcessCpuUsageSample {
    std::chrono::steady_clock::time_point timestamp;
    double user_seconds = 0.0;
    double system_seconds = 0.0;
};

/** @brief One pending FairMQ state transition for framework metrics export. */
struct FairMQStateMeasurement {
    int64_t state_id = 0;
    std::string state_name;
};

/**
 * @brief Owned copy of one signal's C ABI configuration.
 */
struct SignalConfigStorage {
    std::string protocol;
    std::string endpoint_http;
    std::string endpoint_grpc;
    std::string headers;
    uint32_t otlp_http_json = 1U;
};

/**
 * @brief Owned framework metrics configuration used when recreating readers.
 */
struct FrameworkMetricConfigStorage {
    SignalConfigStorage metrics;
    uint32_t timeout_ms = 5000;
    uint32_t metric_export_interval_ms = kDefaultMetricExportIntervalMs;

    auto toConfig() const -> nestdaq_otel_config
    {
        auto config = nestdaq_otel_config{};
        config.size = sizeof(config);
        config.metrics.protocol = metrics.protocol.data();
        config.metrics.endpoint_http = metrics.endpoint_http.data();
        config.metrics.endpoint_grpc = metrics.endpoint_grpc.data();
        config.metrics.headers = metrics.headers.data();
        config.metrics.otlp_http_json = metrics.otlp_http_json;
        config.timeout_ms = timeout_ms;
        config.metric_export_interval_ms = metric_export_interval_ms;
        return config;
    }
};

/**
 * @brief Process-wide telemetry state protected by @ref RuntimeState::mutex.
 *
 * User metrics and framework metrics are intentionally separated: user metrics
 * are held by the normal meter provider, while framework metrics use pending
 * buffers and a dedicated framework meter provider so stale samples are not
 * re-exported after a flush.
 */
struct RuntimeState {
    std::recursive_mutex mutex;
    std::mutex framework_reconfigure_mutex;
    std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> logger_provider;
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> meter_provider;
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> framework_meter_provider;
    std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> tracer_provider;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> framework_meter;
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer;
    std::map<MetricKey, opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<double>>> double_counters;
    std::map<MetricKey, opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>> double_histograms;
    std::map<MetricKey, GaugeInstrument> double_gauges;
    std::map<GaugeSampleKey, double> double_gauge_measurements;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> fairmq_messages_per_second_gauge;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> fairmq_megabytes_per_second_gauge;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> process_cpu_time_counter;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> process_cpu_utilization_gauge;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> process_memory_usage_counter;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> fairmq_state_gauge;
    std::vector<FairMQThroughputMeasurement> pending_fairmq_throughput_measurements;
    std::vector<FairMQThroughputMeasurement> exporting_fairmq_throughput_measurements;
    std::vector<ProcessUsageMeasurement> pending_process_usage_measurements;
    std::vector<ProcessUsageMeasurement> exporting_process_usage_measurements;
    std::vector<FairMQStateMeasurement> pending_fairmq_state_measurements;
    std::vector<FairMQStateMeasurement> exporting_fairmq_state_measurements;
    std::optional<ProcessCpuUsageSample> process_cpu_usage_sample;
    long page_size = 0;
    double available_cpu_count = 0.0;
    std::thread process_metrics_thread;
    std::atomic<bool> stop_process_metrics_thread{false};
    std::chrono::milliseconds process_metrics_interval{kDefaultMetricExportIntervalMs};
    std::vector<Protocol> framework_metric_protocols;
    FrameworkMetricConfigStorage framework_metric_config;
    std::optional<opentelemetry::sdk::resource::Resource> framework_metric_resource;
    std::map<uint64_t, opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>> spans;
    std::atomic<uint64_t> next_span_handle{1};
    std::string last_error;
};

/** @brief Add a non-empty string resource attribute. */
auto addStringAttribute(opentelemetry::sdk::resource::ResourceAttributes &attributes,
                        const char *key,
                        const char *value) -> void;
/** @brief Append one validated C ABI attribute to owned iterable storage. */
auto appendAttribute(AttributeStorage &storage, const nestdaq_otel_attribute &attribute) -> void;
/** @brief Build owned iterable storage from a C ABI attribute array. */
auto buildAttributes(const nestdaq_otel_attribute *attributes, uint64_t attribute_count) -> AttributeStorage;
/** @brief Build comparable gauge attributes from a C ABI attribute array. */
auto buildGaugeAttributes(const nestdaq_otel_attribute *attributes, uint64_t attribute_count) -> std::vector<GaugeAttribute>;
/** @brief Clear the process-wide last-error string. */
auto clearLastError() -> void;
/** @brief Create the log exporter selected by @p protocol. */
auto createLogExporter(const nestdaq_otel_config &config, Protocol protocol)
-> std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>;
/** @brief Wrap a log exporter in the processor appropriate for @p protocol. */
auto createLogProcessor(std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> exporter,
                        Protocol protocol) -> std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor>;
/** @brief Create the metric exporter selected by @p protocol. */
auto createMetricExporter(const nestdaq_otel_config &config, Protocol protocol)
-> std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter>;
/** @brief Create a periodic metric reader for one metric exporter. */
auto createMetricReader(std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> exporter,
                        const nestdaq_otel_config &config)
-> std::unique_ptr<opentelemetry::sdk::metrics::MetricReader>;
/** @brief Create the span exporter selected by @p protocol. */
auto createSpanExporter(const nestdaq_otel_config &config, Protocol protocol)
-> std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>;
/** @brief Wrap a span exporter in the processor appropriate for @p protocol. */
auto createSpanProcessor(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> exporter,
                         Protocol protocol) -> std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>;
/** @brief Register FairMQ throughput observable instruments on the framework meter. */
auto configureFairMQThroughputMetrics(RuntimeState &state) -> void;
/** @brief Register process CPU/RSS observable instruments on the framework meter. */
auto configureProcessMetrics(RuntimeState &state) -> void;
/** @brief Register FairMQ state observable instruments on the framework meter. */
auto configureFairMQStateMetrics(RuntimeState &state) -> void;
/** @brief Recreate the framework metrics provider and its observable instruments. */
auto configureFrameworkMetricsProvider(RuntimeState &state) -> void;
/** @brief Return the implementation defaults used when no C ABI config is supplied. */
auto defaultConfig() -> nestdaq_otel_config;
/** @brief Export and clear pending framework metric samples only when dirty. */
auto flushFrameworkMetricsIfDirty(uint64_t timeout_ms) -> int;
/** @brief Build the structured FairMQ metadata log body emitted at initialization. */
auto fairMQMetadataLogBody(const nestdaq_otel_config &config) -> std::string;
/** @brief Install OpenTelemetry no-op providers after shutdown. */
auto installNoopProviders() -> void;
/** @brief Return true for null or empty C strings. */
auto isEmpty(const char *value) noexcept -> bool;
/** @brief Build OpenTelemetry resource attributes from the C ABI config. */
auto makeResource(const nestdaq_otel_config &config) -> opentelemetry::sdk::resource::Resource;
/** @brief Convert optional metadata C strings to a printable value. */
auto metadataValue(const char *value) -> std::string;
/** @brief Convert optional metadata string_views to a printable value. */
auto metadataValue(std::string_view value) -> std::string;
/** @brief Build the structured NestDAQ metadata log body emitted at initialization. */
auto nestDAQMetadataLogBody() -> std::string;
/** @brief Parse comma-separated OTLP headers into exporter options. */
auto parseHeaders(const char *headers) -> opentelemetry::exporter::otlp::OtlpHeaders;
/** @brief Parse a comma-separated protocol list for one signal. */
auto parseProtocols(const char *protocols, std::vector<Protocol> &out) -> bool;
/** @brief Store @p message as last error and return `NESTDAQ_OTEL_ERROR`. */
auto setLastError(std::string message) -> int;
/** @brief Return true when a signal config names at least one protocol. */
auto signalEnabled(const nestdaq_otel_signal_config &config) noexcept -> bool;
/** @brief Start the background CPU/RSS sampler for framework metrics. */
auto startProcessMetricsThread(uint32_t interval_ms) -> void;
/** @brief Return the process-wide telemetry runtime state. */
auto runtimeState() -> RuntimeState &;
/** @brief Copy framework metric configuration and resource for future reconfiguration. */
auto storeFrameworkMetricConfig(RuntimeState &state,
                                const nestdaq_otel_config &config,
                                const std::vector<Protocol> &protocols,
                                opentelemetry::sdk::resource::Resource resource) -> void;
/** @brief Stop and join the background CPU/RSS sampler if it is running. */
auto stopProcessMetricsThread() -> void;
/** @brief Convert a millisecond timeout to the SDK duration type. */
auto timeoutFromMs(uint64_t timeout_ms) noexcept -> std::chrono::microseconds;
/** @brief Validate a C ABI attribute before converting it to SDK storage. */
auto validateAttribute(const nestdaq_otel_attribute *attribute) noexcept -> bool;
/** @brief Validate that a numeric severity is in the FairLogger range. */
auto validateSeverity(int32_t severity) noexcept -> bool;

} // namespace nestdaq::otel_detail
