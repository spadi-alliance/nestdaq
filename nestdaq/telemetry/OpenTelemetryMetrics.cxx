/** @file
 *  @brief Implements user metrics and one-shot framework metrics export.
 */

#include "nestdaq/telemetry/OpenTelemetryRuntime.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <opentelemetry/common/key_value_iterable_view.h>
#include <opentelemetry/context/context.h>
#include <opentelemetry/exporters/ostream/metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/view_registry_factory.h>

#include <sys/resource.h>
#include <unistd.h>

#include "nestdaq/telemetry/FairMQThroughputLogParser.h"

#if __has_include("nestdaq/version.h")
#  include "nestdaq/version.h"
#else
static constexpr std::string_view NESTDAQ_VERSION {"unknown"};
#endif

namespace nestdaq::otel_detail {
namespace {

constexpr auto kFrameworkMetricReaderIntervalMs = uint32_t{24U * 60U * 60U * 1000U};

auto metricEndpointGrpc(const nestdaq_otel_config &config) -> const char *
{
    return isEmpty(config.metrics.endpoint_grpc) ? kDefaultGrpcEndpoint.data() : config.metrics.endpoint_grpc;
}

auto metricEndpointHttp(const nestdaq_otel_config &config) -> const char *
{
    return isEmpty(config.metrics.endpoint_http) ? kDefaultMetricHttpEndpoint.data() : config.metrics.endpoint_http;
}

auto observeFairMQThroughput(opentelemetry::metrics::ObserverResult observer, bool observe_megabytes) noexcept -> void;
auto observeFairMQState(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept -> void;
auto observeProcessCpuTime(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept -> void;
auto observeProcessCpuUtilization(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept
-> void;
auto observeProcessMemoryUsage(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept -> void;
auto readAvailableCpuCount() noexcept -> double;
auto readProcessCpuUsage() noexcept -> std::optional<ProcessCpuUsageSample>;
auto readProcessMemoryUsageBytes(long page_size) -> std::optional<double>;
auto timevalToSeconds(const timeval &value) noexcept -> double;

auto observeFairMQMegabytesPerSecond(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept
-> void
{
    observeFairMQThroughput(observer, true);
}

auto observeFairMQMessagesPerSecond(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept
-> void
{
    observeFairMQThroughput(observer, false);
}

auto observeFairMQThroughput(opentelemetry::metrics::ObserverResult observer, bool observe_megabytes) noexcept -> void
{
    using DoubleObserver = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>;
    if (!opentelemetry::nostd::holds_alternative<DoubleObserver>(observer)) {
        return;
    }

    const auto kResult = opentelemetry::nostd::get<DoubleObserver>(observer);
    if (!kResult) {
        return;
    }

    auto measurements = std::vector<FairMQThroughputMeasurement> {};
    {
        auto &state = runtimeState();
        std::scoped_lock lock{state.mutex};
        measurements = state.exporting_fairmq_throughput_measurements;
    }

    for (const auto &measurement : measurements) {
        auto attributes =
            std::vector<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>> {};
        attributes.reserve(3);
        attributes.emplace_back("fairmq.channel.name",
                                opentelemetry::nostd::string_view{measurement.channel_name});
        attributes.emplace_back("network.io.direction",
                                opentelemetry::nostd::string_view{measurement.direction});
        if (measurement.sub_channel_index) {
            attributes.emplace_back("fairmq.channel.index",
                                    static_cast<int64_t>(*measurement.sub_channel_index));
        }
        kResult->Observe(observe_megabytes ? measurement.megabytes_per_second : measurement.messages_per_second,
                        attributes);
    }
}

auto observeProcessCpuTime(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept -> void
{
    using DoubleObserver = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>;
    if (!opentelemetry::nostd::holds_alternative<DoubleObserver>(observer)) {
        return;
    }

    const auto kResult = opentelemetry::nostd::get<DoubleObserver>(observer);
    if (!kResult) {
        return;
    }

    auto measurements = std::vector<ProcessUsageMeasurement> {};
    {
        auto &state = runtimeState();
        std::scoped_lock lock{state.mutex};
        measurements = state.exporting_process_usage_measurements;
    }

    for (const auto &measurement : measurements) {
        auto user_attributes =
            std::vector<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>> {};
        user_attributes.emplace_back("cpu.mode", opentelemetry::nostd::string_view{"user"});
        kResult->Observe(measurement.cpu_user_seconds, user_attributes);

        auto system_attributes =
            std::vector<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>> {};
        system_attributes.emplace_back("cpu.mode", opentelemetry::nostd::string_view{"system"});
        kResult->Observe(measurement.cpu_system_seconds, system_attributes);
    }
}

auto observeProcessCpuUtilization(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept
-> void
{
    using DoubleObserver = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>;
    if (!opentelemetry::nostd::holds_alternative<DoubleObserver>(observer)) {
        return;
    }

    const auto kResult = opentelemetry::nostd::get<DoubleObserver>(observer);
    if (!kResult) {
        return;
    }

    auto measurements = std::vector<ProcessUsageMeasurement> {};
    {
        auto &state = runtimeState();
        std::scoped_lock lock{state.mutex};
        measurements = state.exporting_process_usage_measurements;
    }

    for (const auto &measurement : measurements) {
        if (measurement.cpu_utilization) {
            kResult->Observe(*measurement.cpu_utilization);
        }
    }
}

auto observeProcessMemoryUsage(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept -> void
{
    using DoubleObserver = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>;
    if (!opentelemetry::nostd::holds_alternative<DoubleObserver>(observer)) {
        return;
    }

    const auto kResult = opentelemetry::nostd::get<DoubleObserver>(observer);
    if (!kResult) {
        return;
    }

    auto measurements = std::vector<ProcessUsageMeasurement> {};
    {
        auto &state = runtimeState();
        std::scoped_lock lock{state.mutex};
        measurements = state.exporting_process_usage_measurements;
    }

    for (const auto &measurement : measurements) {
        kResult->Observe(measurement.memory_usage_bytes);
    }
}

auto observeUserDoubleGauge(opentelemetry::metrics::ObserverResult observer, void *state) noexcept -> void
{
    using DoubleObserver = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>;
    if (!opentelemetry::nostd::holds_alternative<DoubleObserver>(observer) || state == nullptr) {
        return;
    }

    const auto kResult = opentelemetry::nostd::get<DoubleObserver>(observer);
    if (!kResult) {
        return;
    }

    const auto *metric = static_cast<const MetricKey *>(state);
    auto measurements = std::vector<GaugeMeasurement> {};
    {
        auto &runtime = runtimeState();
        std::scoped_lock lock{runtime.mutex};
        for (const auto &[sample, value] : runtime.double_gauge_measurements) {
            if (sample.metric == *metric) {
                auto measurement = GaugeMeasurement{};
                measurement.attributes = sample.attributes;
                measurement.value = value;
                measurements.emplace_back(std::move(measurement));
            }
        }
    }

    for (const auto &measurement : measurements) {
        auto attributes =
            std::vector<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>> {};
        attributes.reserve(measurement.attributes.size());
        for (const auto &attribute : measurement.attributes) {
            auto key = opentelemetry::nostd::string_view{attribute.key};
            switch (attribute.type) {
            case NESTDAQ_OTEL_ATTRIBUTE_STRING:
                attributes.emplace_back(key, opentelemetry::nostd::string_view{attribute.string_value});
                break;
            case NESTDAQ_OTEL_ATTRIBUTE_INT64:
                attributes.emplace_back(key, attribute.int_value);
                break;
            case NESTDAQ_OTEL_ATTRIBUTE_UINT64:
                attributes.emplace_back(key, attribute.uint_value);
                break;
            case NESTDAQ_OTEL_ATTRIBUTE_DOUBLE:
                attributes.emplace_back(key, attribute.double_value);
                break;
            case NESTDAQ_OTEL_ATTRIBUTE_BOOL:
                attributes.emplace_back(key, attribute.bool_value);
                break;
            }
        }
        kResult->Observe(measurement.value, attributes);
    }
}

auto observeFairMQState(opentelemetry::metrics::ObserverResult observer, void * /* state */) noexcept -> void
{
    using DoubleObserver = opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>;
    if (!opentelemetry::nostd::holds_alternative<DoubleObserver>(observer)) {
        return;
    }

    const auto kResult = opentelemetry::nostd::get<DoubleObserver>(observer);
    if (!kResult) {
        return;
    }

    auto measurements = std::vector<FairMQStateMeasurement>{};
    {
        auto &state = runtimeState();
        std::scoped_lock lock{state.mutex};
        measurements = state.exporting_fairmq_state_measurements;
    }

    for (const auto &measurement : measurements) {
        auto attributes =
            std::vector<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>> {};
        attributes.emplace_back("fairmq.state.name", opentelemetry::nostd::string_view{measurement.state_name});
        kResult->Observe(static_cast<double>(measurement.state_id), attributes);
    }
}

auto readAvailableCpuCount() noexcept -> double
{
    const auto kCpuCount = sysconf(_SC_NPROCESSORS_ONLN);
    return kCpuCount > 0 ? static_cast<double>(kCpuCount) : 0.0;
}

auto readProcessCpuUsage() noexcept -> std::optional<ProcessCpuUsageSample>
{
    auto usage = rusage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return std::nullopt;
    }
    return ProcessCpuUsageSample{
        .timestamp = std::chrono::steady_clock::now(),
        .user_seconds = timevalToSeconds(usage.ru_utime),
        .system_seconds = timevalToSeconds(usage.ru_stime),
    };
}

auto readProcessMemoryUsageBytes(long page_size) -> std::optional<double>
{
    if (page_size <= 0) {
        return std::nullopt;
    }

    auto statm = std::ifstream{"/proc/self/statm"};
    auto total_pages = uint64_t{0};
    auto resident_pages = uint64_t{0};
    if (!(statm >> total_pages >> resident_pages)) {
        return std::nullopt;
    }

    return static_cast<double>(resident_pages) * static_cast<double>(page_size);
}

auto timevalToSeconds(const timeval &value) noexcept -> double
{
    return static_cast<double>(value.tv_sec) + (static_cast<double>(value.tv_usec) / 1'000'000.0);
}

} // namespace

auto configureFairMQThroughputMetrics(RuntimeState &state) -> void
{
    if (!state.framework_meter) {
        return;
    }

    state.fairmq_messages_per_second_gauge = state.framework_meter->CreateDoubleObservableGauge(
            "fairmq.channel.messages_per_second",
            "FairMQ channel message rate parsed from Device throughput logs",
            "{message}/s");
    if (state.fairmq_messages_per_second_gauge) {
        state.fairmq_messages_per_second_gauge->AddCallback(observeFairMQMessagesPerSecond, nullptr);
    }

    state.fairmq_megabytes_per_second_gauge = state.framework_meter->CreateDoubleObservableGauge(
            "fairmq.channel.megabytes_per_second",
            "FairMQ channel throughput parsed from Device throughput logs",
            "MB/s");
    if (state.fairmq_megabytes_per_second_gauge) {
        state.fairmq_megabytes_per_second_gauge->AddCallback(observeFairMQMegabytesPerSecond, nullptr);
    }
}

auto configureProcessMetrics(RuntimeState &state) -> void
{
    if (!state.framework_meter) {
        return;
    }

    state.page_size = sysconf(_SC_PAGESIZE);
    state.available_cpu_count = readAvailableCpuCount();
    state.process_cpu_usage_sample = readProcessCpuUsage();

    state.process_cpu_time_counter = state.framework_meter->CreateDoubleObservableCounter(
                                         "process.cpu.time",
                                         "Total CPU seconds broken down by mode",
                                         "s");
    if (state.process_cpu_time_counter) {
        state.process_cpu_time_counter->AddCallback(observeProcessCpuTime, nullptr);
    }

    if (state.available_cpu_count > 0.0) {
        state.process_cpu_utilization_gauge = state.framework_meter->CreateDoubleObservableGauge(
                "process.cpu.utilization",
                "Process CPU utilization normalized by available CPU count",
                "1");
        if (state.process_cpu_utilization_gauge) {
            state.process_cpu_utilization_gauge->AddCallback(observeProcessCpuUtilization, nullptr);
        }
    }

    state.process_memory_usage_counter = state.framework_meter->CreateDoubleObservableUpDownCounter(
            "process.memory.usage",
            "Physical memory in use by the process",
            "By");
    if (state.process_memory_usage_counter) {
        state.process_memory_usage_counter->AddCallback(observeProcessMemoryUsage, nullptr);
    }
}

auto configureFairMQStateMetrics(RuntimeState &state) -> void
{
    if (!state.framework_meter) {
        return;
    }

    state.fairmq_state_gauge = state.framework_meter->CreateDoubleObservableGauge(
                                   "fairmq.state.id",
                                   "FairMQ device state numeric id",
                                   "1");
    if (state.fairmq_state_gauge) {
        state.fairmq_state_gauge->AddCallback(observeFairMQState, nullptr);
    }
}

auto configureFrameworkMetricsProvider(RuntimeState &state) -> void
{
    if (!state.framework_metric_resource || state.framework_metric_protocols.empty()) {
        return;
    }

    auto views = opentelemetry::sdk::metrics::ViewRegistryFactory::Create();
    state.framework_meter_provider = std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> {
        opentelemetry::sdk::metrics::MeterProviderFactory::Create(std::move(views), *state.framework_metric_resource)
    };

    auto config = state.framework_metric_config.toConfig();
    config.metric_export_interval_ms = kFrameworkMetricReaderIntervalMs;
    for (const auto kProtocol : state.framework_metric_protocols) {
        state.framework_meter_provider->AddMetricReader(createMetricReader(createMetricExporter(config, kProtocol), config));
    }

    state.framework_meter = state.framework_meter_provider->GetMeter("nestdaq.framework", std::string{NESTDAQ_VERSION});
    configureProcessMetrics(state);
    configureFairMQThroughputMetrics(state);
    configureFairMQStateMetrics(state);
}

auto createMetricExporter(const nestdaq_otel_config &config, Protocol protocol)
-> std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter>
{
    switch (protocol) {
    case Protocol::Console:
        return opentelemetry::exporter::metrics::OStreamMetricExporterFactory::Create();
    case Protocol::OtlpHttp: {
        auto options = opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions{};
        options.url = metricEndpointHttp(config);
        options.http_headers = parseHeaders(config.metrics.headers);
        options.content_type = config.metrics.otlp_http_json == 0
                               ? opentelemetry::exporter::otlp::HttpRequestContentType::kBinary
                               : opentelemetry::exporter::otlp::HttpRequestContentType::kJson;
        options.timeout = timeoutFromMs(config.timeout_ms);
        return opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create(options);
    }
    case Protocol::OtlpGrpc: {
        auto options = opentelemetry::exporter::otlp::OtlpGrpcMetricExporterOptions{};
        options.endpoint = metricEndpointGrpc(config);
        options.metadata = parseHeaders(config.metrics.headers);
        options.timeout = timeoutFromMs(config.timeout_ms);
        return opentelemetry::exporter::otlp::OtlpGrpcMetricExporterFactory::Create(options);
    }
    }
    return nullptr;
}

auto createMetricReader(std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> exporter,
                        const nestdaq_otel_config &config)
-> std::unique_ptr<opentelemetry::sdk::metrics::MetricReader>
{
    auto options = opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions{};
    options.export_interval_millis = std::chrono::milliseconds{
        config.metric_export_interval_ms == 0 ? kDefaultMetricExportIntervalMs : config.metric_export_interval_ms};
    options.export_timeout_millis = std::chrono::milliseconds{config.timeout_ms};
    return opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(std::move(exporter), options);
}

auto startProcessMetricsThread(uint32_t interval_ms) -> void
{
    stopProcessMetricsThread();
    auto &state = runtimeState();
    {
        std::scoped_lock lock{state.mutex};
        state.stop_process_metrics_thread = false;
        state.process_metrics_interval = std::chrono::milliseconds{
            interval_ms == 0 ? kDefaultMetricExportIntervalMs : interval_ms};
    }

    // CPU utilization needs two process CPU samples. CPU time and memory usage
    // are exported as one-shot process metrics on each successful tick.
    state.process_metrics_thread = std::thread{[] {
            while (true) {
                auto interval = std::chrono::milliseconds{kDefaultMetricExportIntervalMs};
                {
                    auto &runtime = runtimeState();
                    std::scoped_lock lock{runtime.mutex};
                    if (runtime.stop_process_metrics_thread) {
                        return;
                    }
                    interval = runtime.process_metrics_interval;
                }

                std::this_thread::sleep_for(interval);

                auto previous_cpu = std::optional<ProcessCpuUsageSample> {};
                auto page_size = 0L;
                auto available_cpu_count = 0.0;
                {
                    auto &runtime = runtimeState();
                    std::scoped_lock lock{runtime.mutex};
                    if (runtime.stop_process_metrics_thread) {
                        return;
                    }
                    previous_cpu = runtime.process_cpu_usage_sample;
                    page_size = runtime.page_size;
                    available_cpu_count = runtime.available_cpu_count;
                }

                const auto kCurrentCpu = readProcessCpuUsage();
                const auto kCurrentMemoryUsage = readProcessMemoryUsageBytes(page_size);
                if (!kCurrentCpu || !kCurrentMemoryUsage) {
                    continue;
                }

                auto cpu_utilization = std::optional<double> {};
                if (previous_cpu && available_cpu_count > 0.0) {
                    const auto kElapsedSeconds =
                        std::chrono::duration<double> {kCurrentCpu->timestamp - previous_cpu->timestamp}.count();
                    if (kElapsedSeconds > 0.0) {
                        const auto kCpuSeconds =
                            (kCurrentCpu->user_seconds + kCurrentCpu->system_seconds) -
                            (previous_cpu->user_seconds + previous_cpu->system_seconds);
                        cpu_utilization = kCpuSeconds / kElapsedSeconds / available_cpu_count;
                    }
                }
                {
                    auto &runtime = runtimeState();
                    std::scoped_lock lock{runtime.mutex};
                    runtime.process_cpu_usage_sample = kCurrentCpu;
                }
                nestdaq::OpenTelemetryInitializer::recordFrameworkProcessUsage(kCurrentCpu->user_seconds,
                        kCurrentCpu->system_seconds,
                        cpu_utilization,
                        *kCurrentMemoryUsage);
            }
        }};
}

auto stopProcessMetricsThread() -> void
{
    auto &state = runtimeState();
    {
        std::scoped_lock lock{state.mutex};
        state.stop_process_metrics_thread = true;
    }
    if (state.process_metrics_thread.joinable()) {
        state.process_metrics_thread.join();
    }
}

} // namespace nestdaq::otel_detail

namespace nestdaq {

auto OpenTelemetryInitializer::metricAddDoubleCounter(const char *name,
        double value,
        const char *unit,
        const char *description,
        const nestdaq_otel_attribute *attributes,
        uint64_t attribute_count) -> int
{
    using namespace otel_detail;
    if (isEmpty(name)) {
        return setLastError("metric counter name is empty");
    }
    auto attrs = buildAttributes(attributes, attribute_count);
    auto &state = runtimeState();
    std::scoped_lock lock{state.mutex};
    if (!state.meter) {
        state.last_error.clear();
        return NESTDAQ_OTEL_OK;
    }
    auto key = MetricKey{.kind = MetricKind::DoubleCounter,
                         .name = name,
                         .unit = isEmpty(unit) ? "" : unit,
                         .description = isEmpty(description) ? "" : description};
    auto &counter = state.double_counters[key];
    if (!counter) {
        counter = state.meter->CreateDoubleCounter(key.name, key.description, key.unit);
    }
    auto view = opentelemetry::common::KeyValueIterableView<decltype(attrs.values)> {attrs.values};
    counter->Add(value, view);
    state.last_error.clear();
    return NESTDAQ_OTEL_OK;
}

auto OpenTelemetryInitializer::metricRecordDoubleHistogram(const char *name,
        double value,
        const char *unit,
        const char *description,
        const nestdaq_otel_attribute *attributes,
        uint64_t attribute_count) -> int
{
    using namespace otel_detail;
    if (isEmpty(name)) {
        return setLastError("metric histogram name is empty");
    }
    auto attrs = buildAttributes(attributes, attribute_count);
    auto &state = runtimeState();
    std::scoped_lock lock{state.mutex};
    if (!state.meter) {
        state.last_error.clear();
        return NESTDAQ_OTEL_OK;
    }
    auto key = MetricKey{.kind = MetricKind::DoubleHistogram,
                         .name = name,
                         .unit = isEmpty(unit) ? "" : unit,
                         .description = isEmpty(description) ? "" : description};
    auto &histogram = state.double_histograms[key];
    if (!histogram) {
        histogram = state.meter->CreateDoubleHistogram(key.name, key.description, key.unit);
    }
    auto view = opentelemetry::common::KeyValueIterableView<decltype(attrs.values)> {attrs.values};
    histogram->Record(value, view, opentelemetry::context::Context{});
    state.last_error.clear();
    return NESTDAQ_OTEL_OK;
}

auto OpenTelemetryInitializer::metricRecordDoubleGauge(const char *name,
        double value,
        const char *unit,
        const char *description,
        const nestdaq_otel_attribute *attributes,
        uint64_t attribute_count) -> int
{
    using namespace otel_detail;
    if (isEmpty(name)) {
        return setLastError("metric gauge name is empty");
    }
    auto gauge_attributes = buildGaugeAttributes(attributes, attribute_count);
    auto key = MetricKey{.kind = MetricKind::DoubleGauge,
                         .name = name,
                         .unit = isEmpty(unit) ? "" : unit,
                         .description = isEmpty(description) ? "" : description};

    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> new_gauge;
    MetricKey *callback_key = nullptr;
    {
        auto &state = runtimeState();
        std::scoped_lock lock{state.mutex};
        if (!state.meter) {
            state.last_error.clear();
            return NESTDAQ_OTEL_OK;
        }

        auto [gauge, _] = state.double_gauges.try_emplace(key);
        if (!gauge->second.instrument) {
            gauge->second.callback_key = key;
            gauge->second.instrument = state.meter->CreateDoubleObservableGauge(key.name, key.description, key.unit);
            if (gauge->second.instrument) {
                new_gauge = gauge->second.instrument;
                callback_key = &gauge->second.callback_key;
            }
        }

        state.double_gauge_measurements[GaugeSampleKey{.metric = std::move(key),
                                        .attributes = std::move(gauge_attributes)}] = value;
        state.last_error.clear();
    }
    if (new_gauge) {
        new_gauge->AddCallback(observeUserDoubleGauge, callback_key);
    }
    return NESTDAQ_OTEL_OK;
}

auto OpenTelemetryInitializer::recordFairMQThroughput(const telemetry::FairMQThroughputSample &sample) noexcept
-> void
{
    recordFrameworkFairMQThroughput(sample);
}

auto OpenTelemetryInitializer::recordFrameworkFairMQThroughput(const telemetry::FairMQThroughputSample &sample) noexcept
-> void
{
    try {
        {
            auto &state = otel_detail::runtimeState();
            std::scoped_lock reconfigure_lock{state.framework_reconfigure_mutex};
            std::scoped_lock lock{state.mutex};
            if (!state.framework_meter_provider) {
                return;
            }
            state.pending_fairmq_throughput_measurements.emplace_back(otel_detail::FairMQThroughputMeasurement{
                .channel_name = sample.channel_name,
                .sub_channel_name = sample.sub_channel_name,
                .direction = "in",
                .sub_channel_index = sample.sub_channel_index,
                .messages_per_second = sample.messages_per_second_in,
                .megabytes_per_second = sample.megabytes_per_second_in,
            });
            state.pending_fairmq_throughput_measurements.emplace_back(otel_detail::FairMQThroughputMeasurement{
                .channel_name = sample.channel_name,
                .sub_channel_name = sample.sub_channel_name,
                .direction = "out",
                .sub_channel_index = sample.sub_channel_index,
                .messages_per_second = sample.messages_per_second_out,
                .megabytes_per_second = sample.megabytes_per_second_out,
            });
        }
        static_cast<void>(otel_detail::flushFrameworkMetricsIfDirty(otel_detail::kDefaultMetricExportIntervalMs));
    } catch (...) {
    }
}

auto OpenTelemetryInitializer::recordFrameworkProcessUsage(double cpu_user_seconds,
        double cpu_system_seconds,
        std::optional<double> cpu_utilization,
        double memory_usage_bytes) noexcept -> void
{
    try {
        {
            auto &state = otel_detail::runtimeState();
            std::scoped_lock reconfigure_lock{state.framework_reconfigure_mutex};
            std::scoped_lock lock{state.mutex};
            if (!state.framework_meter_provider) {
                return;
            }
            state.pending_process_usage_measurements.emplace_back(otel_detail::ProcessUsageMeasurement{
                .cpu_user_seconds = cpu_user_seconds,
                .cpu_system_seconds = cpu_system_seconds,
                .cpu_utilization = cpu_utilization,
                .memory_usage_bytes = memory_usage_bytes,
            });
        }
        static_cast<void>(otel_detail::flushFrameworkMetricsIfDirty(otel_detail::kDefaultMetricExportIntervalMs));
    } catch (...) {
    }
}

auto OpenTelemetryInitializer::recordFrameworkFairMQState(int64_t state_id, const char *state_name) noexcept -> void
{
    try {
        {
            auto &state = otel_detail::runtimeState();
            std::scoped_lock reconfigure_lock{state.framework_reconfigure_mutex};
            std::scoped_lock lock{state.mutex};
            if (!state.framework_meter_provider) {
                return;
            }
            state.pending_fairmq_state_measurements.emplace_back(otel_detail::FairMQStateMeasurement{
                .state_id = state_id,
                .state_name = otel_detail::isEmpty(state_name) ? "" : state_name,
            });
        }
        static_cast<void>(otel_detail::flushFrameworkMetricsIfDirty(otel_detail::kDefaultMetricExportIntervalMs));
    } catch (...) {
    }
}

} // namespace nestdaq
