/** @file
 *  @brief Builds trace exporters/processors and manages implementation-owned span handles.
 */

#include "nestdaq/telemetry/OpenTelemetryRuntime.h"

#include <memory>
#include <utility>

#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_options.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>

namespace nestdaq::otel_detail {
namespace {

auto traceEndpointGrpc(const nestdaq_otel_config &config) -> const char *
{
    return isEmpty(config.traces.endpoint_grpc) ? kDefaultGrpcEndpoint.data() : config.traces.endpoint_grpc;
}

auto traceEndpointHttp(const nestdaq_otel_config &config) -> const char *
{
    return isEmpty(config.traces.endpoint_http) ? kDefaultTraceHttpEndpoint.data() : config.traces.endpoint_http;
}

} // namespace

auto createSpanExporter(const nestdaq_otel_config &config, Protocol protocol)
-> std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>
{
    switch (protocol) {
    case Protocol::Console:
        return opentelemetry::exporter::trace::OStreamSpanExporterFactory::Create();
    case Protocol::OtlpHttp: {
        auto options = opentelemetry::exporter::otlp::OtlpHttpExporterOptions{};
        options.url = traceEndpointHttp(config);
        options.http_headers = parseHeaders(config.traces.headers);
        options.content_type = config.traces.otlp_http_json == 0
                               ? opentelemetry::exporter::otlp::HttpRequestContentType::kBinary
                               : opentelemetry::exporter::otlp::HttpRequestContentType::kJson;
        options.timeout = timeoutFromMs(config.timeout_ms);
        return opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create(options);
    }
    case Protocol::OtlpGrpc: {
        auto options = opentelemetry::exporter::otlp::OtlpGrpcExporterOptions{};
        options.endpoint = traceEndpointGrpc(config);
        options.metadata = parseHeaders(config.traces.headers);
        options.timeout = timeoutFromMs(config.timeout_ms);
        return opentelemetry::exporter::otlp::OtlpGrpcExporterFactory::Create(options);
    }
    }
    return nullptr;
}

auto createSpanProcessor(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> exporter,
                         Protocol protocol) -> std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>
{
    if (protocol == Protocol::Console) {
        return opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(std::move(exporter));
    }
    auto options = opentelemetry::sdk::trace::BatchSpanProcessorOptions{};
    return opentelemetry::sdk::trace::BatchSpanProcessorFactory::Create(std::move(exporter), options);
}

} // namespace nestdaq::otel_detail

namespace nestdaq {

auto OpenTelemetryInitializer::spanEnd(uint64_t span_handle) -> int
{
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span;
    {
        auto &state = otel_detail::runtimeState();
        std::scoped_lock lock{state.mutex};
        auto it = state.spans.find(span_handle);
        if (it == state.spans.end()) {
            return otel_detail::setLastError("OpenTelemetry span handle is not active");
        }
        span = it->second;
        state.spans.erase(it);
        state.last_error.clear();
    }
    span->End();
    return NESTDAQ_OTEL_OK;
}

auto OpenTelemetryInitializer::spanSetAttribute(uint64_t span_handle, const nestdaq_otel_attribute *attribute) -> int
{
    if (!otel_detail::validateAttribute(attribute)) {
        return otel_detail::setLastError("OpenTelemetry span attribute is invalid");
    }
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span;
    {
        auto &state = otel_detail::runtimeState();
        std::scoped_lock lock{state.mutex};
        auto it = state.spans.find(span_handle);
        if (it == state.spans.end()) {
            return otel_detail::setLastError("OpenTelemetry span handle is not active");
        }
        span = it->second;
        state.last_error.clear();
    }
    auto storage = otel_detail::AttributeStorage{};
    storage.keys.reserve(1);
    storage.values.reserve(1);
    otel_detail::appendAttribute(storage, *attribute);
    if (!storage.values.empty()) {
        span->SetAttribute(storage.values.front().first, storage.values.front().second);
    }
    return NESTDAQ_OTEL_OK;
}

auto OpenTelemetryInitializer::spanStart(const char *name,
        const nestdaq_otel_attribute *attributes,
        uint64_t attribute_count) -> uint64_t
{
    if (otel_detail::isEmpty(name)) {
        otel_detail::setLastError("OpenTelemetry span name is empty");
        return 0;
    }
    auto attrs = otel_detail::buildAttributes(attributes, attribute_count);
    auto &state = otel_detail::runtimeState();
    std::scoped_lock lock{state.mutex};
    if (!state.tracer) {
        state.last_error.clear();
        return 0;
    }
    auto span = state.tracer->StartSpan(name, attrs.values);
    const auto kHandle = state.next_span_handle.fetch_add(1, std::memory_order_relaxed);
    state.spans.emplace(kHandle, std::move(span));
    state.last_error.clear();
    return kHandle;
}

} // namespace nestdaq
