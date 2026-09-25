/** @file
 *  @brief Initializes dynamic OpenTelemetry providers and exposes the C ABI.
 */

#include "nestdaq/telemetry/OpenTelemetryRuntime.h"

#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <opentelemetry/logs/provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/view_registry_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>

#include <fairlogger/Logger.h>

#include "nestdaq/telemetry/FairLoggerOpenTelemetrySink.h"

#if __has_include("nestdaq/version.h")
#  include "nestdaq/version.h"
#else
static constexpr std::string_view NESTDAQ_VERSION {"unknown"};
#endif

namespace nestdaq {

using namespace otel_detail;

auto OpenTelemetryInitializer::forceFlush(uint64_t timeout_ms) -> int
{
    try {
        std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> logger_provider;
        std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> meter_provider;
        std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> tracer_provider;
        {
            auto &state = runtimeState();
            std::scoped_lock lock{state.mutex};
            logger_provider = state.logger_provider;
            meter_provider = state.meter_provider;
            tracer_provider = state.tracer_provider;
        }
        auto ok = true;
        if (logger_provider) {
            ok = logger_provider->ForceFlush(timeoutFromMs(timeout_ms)) && ok;
        }
        if (meter_provider) {
            ok = meter_provider->ForceFlush(timeoutFromMs(timeout_ms)) && ok;
        }
        if (tracer_provider) {
            ok = tracer_provider->ForceFlush(timeoutFromMs(timeout_ms)) && ok;
        }
        if (!ok) {
            return setLastError("OpenTelemetry force flush failed");
        }
        clearLastError();
        return NESTDAQ_OTEL_OK;
    } catch (const std::exception &ex) {
        return setLastError(ex.what());
    } catch (...) {
        return setLastError("unknown OpenTelemetry force flush error");
    }
}

auto OpenTelemetryInitializer::flushFrameworkMetricsIfDirty(uint64_t timeout_ms) -> int
{
    return otel_detail::flushFrameworkMetricsIfDirty(timeout_ms);
}

auto OpenTelemetryInitializer::initialize(const nestdaq_otel_config *config) -> int
{
    auto local_config = defaultConfig();
    if (config != nullptr) {
        if (config->size != sizeof(nestdaq_otel_config)) {
            return setLastError("nestdaq_otel_config has an unsupported size");
        }
        local_config = *config;
    }
    if (!validateSeverity(local_config.min_severity)) {
        return setLastError("min_severity must be a valid fair::Severity numeric value");
    }

    auto log_protocols = std::vector<Protocol> {};
    auto metric_protocols = std::vector<Protocol> {};
    auto trace_protocols = std::vector<Protocol> {};
    if ((signalEnabled(local_config.logs) && !parseProtocols(local_config.logs.protocol, log_protocols)) ||
            (signalEnabled(local_config.metrics) && !parseProtocols(local_config.metrics.protocol, metric_protocols)) ||
            (signalEnabled(local_config.traces) && !parseProtocols(local_config.traces.protocol, trace_protocols))) {
        return setLastError("unsupported OpenTelemetry protocol; expected comma-separated console, otlp-http, or otlp-grpc");
    }

    try {
        auto resource = makeResource(local_config);
        std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> logger_provider;
        std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> meter_provider;
        std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> tracer_provider;

        if (!log_protocols.empty()) {
            auto processors = std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor>> {};
            for (const auto kProtocol : log_protocols) {
                processors.emplace_back(createLogProcessor(createLogExporter(local_config, kProtocol), kProtocol));
            }
            logger_provider = std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> {
                opentelemetry::sdk::logs::LoggerProviderFactory::Create(std::move(processors), resource)
            };
        }

        if (!metric_protocols.empty()) {
            auto views = opentelemetry::sdk::metrics::ViewRegistryFactory::Create();
            meter_provider = std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> {
                opentelemetry::sdk::metrics::MeterProviderFactory::Create(std::move(views), resource)
            };
            for (const auto kProtocol : metric_protocols) {
                meter_provider->AddMetricReader(createMetricReader(createMetricExporter(local_config, kProtocol), local_config));
            }
        }

        if (!trace_protocols.empty()) {
            auto processors = std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> {};
            for (const auto kProtocol : trace_protocols) {
                processors.emplace_back(createSpanProcessor(createSpanExporter(local_config, kProtocol), kProtocol));
            }
            tracer_provider = std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> {
                opentelemetry::sdk::trace::TracerProviderFactory::Create(std::move(processors), resource)
            };
        }

        shutdown(local_config.timeout_ms);
        {
            auto &state = runtimeState();
            std::scoped_lock lock{state.mutex};
            state.logger_provider = logger_provider;
            state.meter_provider = meter_provider;
            state.tracer_provider = tracer_provider;
            state.meter = {};
            state.framework_meter = {};
            state.tracer = {};
            if (meter_provider) {
                state.meter = meter_provider->GetMeter("nestdaq", std::string{NESTDAQ_VERSION});
            }
            if (!metric_protocols.empty()) {
                storeFrameworkMetricConfig(state, local_config, metric_protocols, resource);
                configureFrameworkMetricsProvider(state);
            }
            if (tracer_provider) {
                state.tracer = tracer_provider->GetTracer("nestdaq", std::string{NESTDAQ_VERSION});
            }
            state.last_error.clear();
        }
        if (!metric_protocols.empty()) {
            startProcessMetricsThread(local_config.metric_export_interval_ms);
        }

        if (logger_provider) {
            opentelemetry::logs::Provider::SetLoggerProvider(
            opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider> {
                std::shared_ptr<opentelemetry::logs::LoggerProvider>{logger_provider}
            });
            FairLoggerOpenTelemetrySink::setMinSeverity(local_config.min_severity);
            FairLoggerOpenTelemetrySink::initialize();
            LOG(info) << nestDAQMetadataLogBody();
            LOG(info) << fairMQMetadataLogBody(local_config);
        }
        if (meter_provider) {
            opentelemetry::metrics::Provider::SetMeterProvider(
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider> {
                std::shared_ptr<opentelemetry::metrics::MeterProvider>{meter_provider}
            });
        }
        if (tracer_provider) {
            opentelemetry::trace::Provider::SetTracerProvider(
            opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> {
                std::shared_ptr<opentelemetry::trace::TracerProvider>{tracer_provider}
            });
        }
        return NESTDAQ_OTEL_OK;
    } catch (const std::exception &ex) {
        return setLastError(ex.what());
    } catch (...) {
        return setLastError("unknown OpenTelemetry initialization error");
    }
}

auto OpenTelemetryInitializer::lastError() noexcept -> const char *
{
    auto &state = runtimeState();
    std::scoped_lock lock{state.mutex};
    return state.last_error.data();
}

auto OpenTelemetryInitializer::setNestdaqInstanceId(const char *instance_id) -> int
{
    FairLoggerOpenTelemetrySink::setNestdaqInstanceId(isEmpty(instance_id) ? "" : instance_id);
    clearLastError();
    return NESTDAQ_OTEL_OK;
}

auto OpenTelemetryInitializer::setMinSeverity(int32_t severity) -> int
{
    if (!validateSeverity(severity)) {
        return setLastError("severity must be a valid fair::Severity numeric value");
    }
    FairLoggerOpenTelemetrySink::setMinSeverity(severity);
    clearLastError();
    return NESTDAQ_OTEL_OK;
}

auto OpenTelemetryInitializer::shutdown(uint64_t timeout_ms) -> int
{
    try {
        stopProcessMetricsThread();
        auto &runtime_state = runtimeState();
        std::scoped_lock reconfigure_lock{runtime_state.framework_reconfigure_mutex};
        std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> logger_provider;
        std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> meter_provider;
        std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> framework_meter_provider;
        std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> tracer_provider;
        {
            auto &state = runtime_state;
            std::scoped_lock lock{state.mutex};
            logger_provider = std::move(state.logger_provider);
            meter_provider = std::move(state.meter_provider);
            framework_meter_provider = std::move(state.framework_meter_provider);
            tracer_provider = std::move(state.tracer_provider);
            state.logger_provider.reset();
            state.meter_provider.reset();
            state.framework_meter_provider.reset();
            state.tracer_provider.reset();
            state.meter = {};
            state.framework_meter = {};
            state.tracer = {};
            state.double_counters.clear();
            state.double_histograms.clear();
            state.double_gauges.clear();
            state.double_gauge_measurements.clear();
            state.fairmq_messages_per_second_gauge = {};
            state.fairmq_megabytes_per_second_gauge = {};
            state.process_cpu_time_counter = {};
            state.process_cpu_utilization_gauge = {};
            state.process_memory_usage_counter = {};
            state.fairmq_state_gauge = {};
            state.pending_fairmq_throughput_measurements.clear();
            state.exporting_fairmq_throughput_measurements.clear();
            state.pending_process_usage_measurements.clear();
            state.exporting_process_usage_measurements.clear();
            state.pending_fairmq_state_measurements.clear();
            state.exporting_fairmq_state_measurements.clear();
            state.process_cpu_usage_sample = std::nullopt;
            state.page_size = 0;
            state.available_cpu_count = 0.0;
            state.spans.clear();
        }
        FairLoggerOpenTelemetrySink::shutdown();
        if (logger_provider) {
            logger_provider->ForceFlush(timeoutFromMs(timeout_ms));
            logger_provider->Shutdown(timeoutFromMs(timeout_ms));
        }
        if (meter_provider) {
            meter_provider->ForceFlush(timeoutFromMs(timeout_ms));
            meter_provider->Shutdown(timeoutFromMs(timeout_ms));
        }
        if (framework_meter_provider) {
            framework_meter_provider->ForceFlush(timeoutFromMs(timeout_ms));
            framework_meter_provider->Shutdown(timeoutFromMs(timeout_ms));
        }
        if (tracer_provider) {
            tracer_provider->ForceFlush(timeoutFromMs(timeout_ms));
            tracer_provider->Shutdown(timeoutFromMs(timeout_ms));
        }
        installNoopProviders();
        clearLastError();
        return NESTDAQ_OTEL_OK;
    } catch (const std::exception &ex) {
        return setLastError(ex.what());
    } catch (...) {
        return setLastError("unknown OpenTelemetry shutdown error");
    }
}

} // namespace nestdaq

extern "C" {

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_force_flush(uint64_t timeout_ms)
    {
        return nestdaq::OpenTelemetryInitializer::forceFlush(timeout_ms);
    }

    NESTDAQ_OTEL_EXPORT void nestdaq_otel_framework_record_fairmq_state(int64_t state_id,
            const char *state_name)
    {
        nestdaq::OpenTelemetryInitializer::recordFrameworkFairMQState(state_id, state_name);
    }

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_init(const nestdaq_otel_config *config)
    {
        return nestdaq::OpenTelemetryInitializer::initialize(config);
    }

    NESTDAQ_OTEL_EXPORT const char *nestdaq_otel_last_error(void)
    {
        return nestdaq::OpenTelemetryInitializer::lastError();
    }

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_metric_add_double_counter(const char *name,
            double value,
            const char *unit,
            const char *description,
            const nestdaq_otel_attribute *attributes,
            uint64_t attribute_count)
    {
        return nestdaq::OpenTelemetryInitializer::metricAddDoubleCounter(
                   name, value, unit, description, attributes, attribute_count);
    }

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_metric_record_double_histogram(const char *name,
            double value,
            const char *unit,
            const char *description,
            const nestdaq_otel_attribute *attributes,
            uint64_t attribute_count)
    {
        return nestdaq::OpenTelemetryInitializer::metricRecordDoubleHistogram(
                   name, value, unit, description, attributes, attribute_count);
    }

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_metric_record_double_gauge(const char *name,
            double value,
            const char *unit,
            const char *description,
            const nestdaq_otel_attribute *attributes,
            uint64_t attribute_count)
    {
        return nestdaq::OpenTelemetryInitializer::metricRecordDoubleGauge(
                   name, value, unit, description, attributes, attribute_count);
    }

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_set_min_severity(int32_t severity)
    {
        return nestdaq::OpenTelemetryInitializer::setMinSeverity(severity);
    }

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_set_nestdaq_instance_id(const char *instance_id)
    {
        return nestdaq::OpenTelemetryInitializer::setNestdaqInstanceId(instance_id);
    }

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_shutdown(uint64_t timeout_ms)
    {
        return nestdaq::OpenTelemetryInitializer::shutdown(timeout_ms);
    }

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_span_end(uint64_t span_handle)
    {
        return nestdaq::OpenTelemetryInitializer::spanEnd(span_handle);
    }

    NESTDAQ_OTEL_EXPORT int nestdaq_otel_span_set_attribute(uint64_t span_handle,
            const nestdaq_otel_attribute *attribute)
    {
        return nestdaq::OpenTelemetryInitializer::spanSetAttribute(span_handle, attribute);
    }

    NESTDAQ_OTEL_EXPORT uint64_t nestdaq_otel_span_start(const char *name,
            const nestdaq_otel_attribute *attributes,
            uint64_t attribute_count)
    {
        return nestdaq::OpenTelemetryInitializer::spanStart(name, attributes, attribute_count);
    }

}
