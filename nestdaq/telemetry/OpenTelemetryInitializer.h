/** @file
 *  @brief C ABI and implementation-side C++ entry points for NestDAQ OpenTelemetry support.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
#  include <optional>
#endif

#ifdef _WIN32
#  define NESTDAQ_OTEL_EXPORT __declspec(dllexport)
#else
#  define NESTDAQ_OTEL_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
static constexpr int NESTDAQ_OTEL_OK = 0;
static constexpr int NESTDAQ_OTEL_ERROR = -1;
#else
enum {
    NESTDAQ_OTEL_OK = 0,
    NESTDAQ_OTEL_ERROR = -1
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Value kind used by @ref nestdaq_otel_attribute.
 *
 * The C ABI exposes a small union-like attribute representation so that NestDAQ
 * executables do not include or link OpenTelemetry C++ headers.
 */
typedef enum nestdaq_otel_attribute_type { // NOLINT(cppcoreguidelines-use-enum-class): C ABI enum.
    NESTDAQ_OTEL_ATTRIBUTE_STRING = 0,
    NESTDAQ_OTEL_ATTRIBUTE_INT64 = 1,
    NESTDAQ_OTEL_ATTRIBUTE_UINT64 = 2,
    NESTDAQ_OTEL_ATTRIBUTE_DOUBLE = 3,
    NESTDAQ_OTEL_ATTRIBUTE_BOOL = 4
} nestdaq_otel_attribute_type;

/**
 * @brief Key/value attribute passed to metrics and spans.
 *
 * @p key must be non-null and non-empty. The field selected by @p type is used
 * as the value; all other value fields are ignored. Invalid attributes are
 * ignored when building attribute sets for telemetry records.
 */
typedef struct nestdaq_otel_attribute {
    const char *key;
    nestdaq_otel_attribute_type type;
    const char *string_value;
    int64_t int_value;
    uint64_t uint_value;
    double double_value;
    uint32_t bool_value;
} nestdaq_otel_attribute;

/**
 * @brief Exporter configuration for one OpenTelemetry signal.
 *
 * @p protocol is a comma-separated list of `console`, `otlp-http`, and
 * `otlp-grpc`. An empty protocol disables the signal. The HTTP and gRPC
 * endpoints are signal-specific; @p headers uses comma-separated `key=value`
 * pairs. @p otlp_http_json selects JSON OTLP/HTTP when non-zero and binary
 * protobuf otherwise.
 */
typedef struct nestdaq_otel_signal_config {
    const char *protocol;      /* Comma-separated "console", "otlp-http", and/or "otlp-grpc"; empty disables the signal. */
    const char *endpoint_http; /* Optional OTLP HTTP endpoint for this signal. */
    const char *endpoint_grpc; /* Optional OTLP gRPC endpoint for this signal. */
    const char *headers;       /* Optional comma-separated key=value pairs. */
    uint32_t otlp_http_json;   /* Non-zero selects JSON for otlp-http. */
} nestdaq_otel_signal_config;

/**
 * @brief Process-wide telemetry configuration consumed by `libnestdaq_otel.so`.
 *
 * Callers must set @p size to `sizeof(nestdaq_otel_config)`. String pointers
 * are borrowed for the duration of @ref nestdaq_otel_init only. Logs, metrics,
 * and traces are configured independently but share OpenTelemetry resource
 * attributes. @p min_severity is the FairLogger severity threshold numeric
 * value. A zero timeout means the OpenTelemetry SDK default/no-limit timeout.
 */
typedef struct nestdaq_otel_config {
    uint32_t size;
    nestdaq_otel_signal_config logs;
    nestdaq_otel_signal_config metrics;
    nestdaq_otel_signal_config traces;
    const char *service_name;        /* OpenTelemetry service.name. */
    const char *service_namespace;   /* OpenTelemetry service.namespace. */
    const char *service_instance_id; /* OpenTelemetry service.instance.id. */
    const char *host_name;           /* OpenTelemetry host.name. */
    const char *nestdaq_instance_id;        /* NestDAQ FairMQ device instance id. */
    const char *nestdaq_instance_id_status; /* "unresolved" before id is known, otherwise "resolved". */
    const char *fairmq_id;           /* FairMQ device id. */
    const char *fairmq_device;       /* FairMQ device/class name. */
    const char *fairmq_session;      /* FairMQ session id/name. */
    const char *fairmq_transport;    /* FairMQ transport. */
    const char *fairmq_git_version;  /* FAIRMQ_GIT_VERSION. */
    const char *fairmq_build_type;   /* FAIRMQ_BUILD_TYPE. */
    const char *fairmq_repo_url;     /* FAIRMQ_REPO_URL. */
    const char *fairmq_license;      /* FAIRMQ_LICENSE. */
    const char *fairmq_copyright;    /* FAIRMQ_COPYRIGHT. */
    int32_t min_severity;            /* fair::Severity numeric value. */
    uint32_t timeout_ms;             /* Optional exporter force-flush/shutdown timeout. */
    uint32_t metric_export_interval_ms;
} nestdaq_otel_config;

/**
 * @brief Force-flush all initialized telemetry providers.
 *
 * This flushes logs, user metrics, framework metrics, and traces that have
 * initialized providers. If no providers are active the call succeeds.
 *
 * @return `NESTDAQ_OTEL_OK` on success, otherwise `NESTDAQ_OTEL_ERROR`.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_force_flush(uint64_t timeout_ms);
/**
 * @brief Queue and flush one FairMQ state transition as a framework metric.
 *
 * The value is exported only once. Calls are ignored when framework metrics are
 * disabled or no framework meter has been configured.
 */
NESTDAQ_OTEL_EXPORT void nestdaq_otel_framework_record_fairmq_state(int64_t state_id,
        const char *state_name);
/**
 * @brief initialize process-wide OpenTelemetry providers and the FairLogger sink.
 *
 * Reinitialization shuts down the previous providers before installing the new
 * providers. Passing null uses defaults.
 *
 * @return `NESTDAQ_OTEL_OK` on success, otherwise `NESTDAQ_OTEL_ERROR`.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_init(const nestdaq_otel_config *config);
/**
 * @brief Return the last error message from the OpenTelemetry implementation library.
 *
 * The returned pointer is owned by the implementation library and remains valid until the next
 * telemetry API call that changes the last-error storage.
 */
NESTDAQ_OTEL_EXPORT const char *nestdaq_otel_last_error(void);
/**
 * @brief Add a value to a double counter instrument.
 *
 * If metrics are disabled this is a successful no-op. @p name must be non-empty
 * when metrics are enabled. Instrument identity is `(name, unit, description)`.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_metric_add_double_counter(const char *name,
        double value,
        const char *unit,
        const char *description,
        const nestdaq_otel_attribute *attributes,
        uint64_t attribute_count);
/**
 * @brief Record a value in a double histogram instrument.
 *
 * If metrics are disabled this is a successful no-op. @p name must be non-empty
 * when metrics are enabled. Instrument identity is `(name, unit, description)`.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_metric_record_double_histogram(const char *name,
        double value,
        const char *unit,
        const char *description,
        const nestdaq_otel_attribute *attributes,
        uint64_t attribute_count);
/**
 * @brief Record the latest value for a double observable gauge instrument.
 *
 * If metrics are disabled this is a successful no-op. @p name must be non-empty
 * when metrics are enabled. Instrument identity is `(name, unit, description)`.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_metric_record_double_gauge(const char *name,
        double value,
        const char *unit,
        const char *description,
        const nestdaq_otel_attribute *attributes,
        uint64_t attribute_count);
/**
 * @brief Update the FairLogger severity threshold exported to OpenTelemetry logs.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_set_min_severity(int32_t severity);
/**
 * @brief Update the NestDAQ FairMQ device instance id attached to log records.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_set_nestdaq_instance_id(const char *instance_id);
/**
 * @brief Flush, shut down, and uninstall all telemetry providers.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_shutdown(uint64_t timeout_ms);
/**
 * @brief End and release a span handle returned by @ref nestdaq_otel_span_start.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_span_end(uint64_t span_handle);
/**
 * @brief Set an attribute on an active span handle.
 */
NESTDAQ_OTEL_EXPORT int nestdaq_otel_span_set_attribute(uint64_t span_handle,
        const nestdaq_otel_attribute *attribute);
/**
 * @brief Start a span and return an opaque handle.
 *
 * Returns 0 when traces are disabled or span creation fails. Non-zero handles
 * must be ended exactly once with @ref nestdaq_otel_span_end.
 */
NESTDAQ_OTEL_EXPORT uint64_t nestdaq_otel_span_start(const char *name,
        const nestdaq_otel_attribute *attributes,
        uint64_t attribute_count);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nestdaq {
namespace telemetry {
struct FairMQThroughputSample;
}

/**
 * @brief C++ implementation facade for the `libnestdaq_otel.so` C ABI.
 *
 * NestDAQ executables normally use `nestdaq::telemetry::TelemetryLibrary`
 * instead of this class so they can remain OpenTelemetry-unlinked. This class is
 * compiled into the OpenTelemetry implementation library itself.
 */
class OpenTelemetryInitializer {
public:
    OpenTelemetryInitializer() = delete;

    /** @brief Implementation for @ref nestdaq_otel_force_flush. */
    static auto forceFlush(uint64_t timeout_ms) -> int;
    /** @brief Configure process-wide OpenTelemetry providers from the C ABI config. */
    static auto initialize(const nestdaq_otel_config *config) -> int;
    /** @brief Return the implementation-owned last-error string. */
    static auto lastError() noexcept -> const char *;
    /** @brief Add to a user double counter instrument. */
    static auto metricAddDoubleCounter(const char *name,
                                       double value,
                                       const char *unit,
                                       const char *description,
                                       const nestdaq_otel_attribute *attributes,
                                       uint64_t attribute_count) -> int;
    /** @brief Record a user double histogram measurement. */
    static auto metricRecordDoubleHistogram(const char *name,
                                            double value,
                                            const char *unit,
                                            const char *description,
                                            const nestdaq_otel_attribute *attributes,
                                            uint64_t attribute_count) -> int;
    /** @brief Store the latest user double gauge measurement for collection. */
    static auto metricRecordDoubleGauge(const char *name,
                                        double value,
                                        const char *unit,
                                        const char *description,
                                        const nestdaq_otel_attribute *attributes,
                                        uint64_t attribute_count) -> int;
    /** @brief Export pending framework metric samples, if any are dirty. */
    static auto flushFrameworkMetricsIfDirty(uint64_t timeout_ms) -> int;
    /** @brief Record a FairMQ state transition for the framework metrics pipeline. */
    static auto recordFrameworkFairMQState(int64_t state_id, const char *state_name) noexcept -> void;
    /** @brief Record parsed FairMQ throughput through the framework metrics pipeline. */
    static auto recordFrameworkFairMQThroughput(const telemetry::FairMQThroughputSample &sample) noexcept -> void;
    /** @brief Record one sampled process CPU/RSS pair through the framework metrics pipeline. */
    static auto recordFrameworkProcessUsage(double cpu_user_seconds,
                                            double cpu_system_seconds,
                                            std::optional<double> cpu_utilization,
                                            double memory_usage_bytes) noexcept -> void;
    /** @brief Record parsed FairMQ throughput observed by the FairLogger sink. */
    static auto recordFairMQThroughput(const telemetry::FairMQThroughputSample &sample) noexcept -> void;
    /** @brief Update log attributes derived from the NestDAQ instance id. */
    static auto setNestdaqInstanceId(const char *instance_id) -> int;
    /** @brief Update the minimum FairLogger severity exported to OpenTelemetry. */
    static auto setMinSeverity(int32_t severity) -> int;
    /** @brief Stop background collection and shut down all active providers. */
    static auto shutdown(uint64_t timeout_ms) -> int;
    /** @brief End and erase an implementation-owned span handle. */
    static auto spanEnd(uint64_t span_handle) -> int;
    /** @brief Set an attribute on an implementation-owned span handle. */
    static auto spanSetAttribute(uint64_t span_handle, const nestdaq_otel_attribute *attribute) -> int;
    /** @brief Start an implementation-owned span and return its opaque handle. */
    static auto spanStart(const char *name,
                          const nestdaq_otel_attribute *attributes,
                          uint64_t attribute_count) -> uint64_t;
};

} // namespace nestdaq
#endif
