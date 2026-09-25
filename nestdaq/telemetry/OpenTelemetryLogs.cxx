/** @file
 *  @brief Builds log exporters and processors for the OpenTelemetry implementation library.
 */

#include "nestdaq/telemetry/OpenTelemetryRuntime.h"

#include <memory>

#include <opentelemetry/exporters/ostream/log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_factory.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_options.h>
#include <opentelemetry/sdk/logs/simple_log_record_processor_factory.h>

namespace nestdaq::otel_detail {
namespace {

auto logEndpointGrpc(const nestdaq_otel_config &config) -> const char *
{
    return isEmpty(config.logs.endpoint_grpc) ? kDefaultGrpcEndpoint.data() : config.logs.endpoint_grpc;
}

auto logEndpointHttp(const nestdaq_otel_config &config) -> const char *
{
    return isEmpty(config.logs.endpoint_http) ? kDefaultLogHttpEndpoint.data() : config.logs.endpoint_http;
}

} // namespace

auto createLogExporter(const nestdaq_otel_config &config, Protocol protocol)
-> std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>
{
    switch (protocol) {
    case Protocol::Console:
        return opentelemetry::exporter::logs::OStreamLogRecordExporterFactory::Create();
    case Protocol::OtlpHttp: {
        auto options = opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterOptions{};
        options.url = logEndpointHttp(config);
        options.http_headers = parseHeaders(config.logs.headers);
        options.content_type = config.logs.otlp_http_json == 0
                               ? opentelemetry::exporter::otlp::HttpRequestContentType::kBinary
                               : opentelemetry::exporter::otlp::HttpRequestContentType::kJson;
        options.timeout = timeoutFromMs(config.timeout_ms);
        return opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create(options);
    }
    case Protocol::OtlpGrpc: {
        auto options = opentelemetry::exporter::otlp::OtlpGrpcLogRecordExporterOptions{};
        options.endpoint = logEndpointGrpc(config);
        options.metadata = parseHeaders(config.logs.headers);
        options.timeout = timeoutFromMs(config.timeout_ms);
        return opentelemetry::exporter::otlp::OtlpGrpcLogRecordExporterFactory::Create(options);
    }
    }
    return nullptr;
}

auto createLogProcessor(std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> exporter,
                        Protocol protocol) -> std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor>
{
    if (protocol == Protocol::Console) {
        return opentelemetry::sdk::logs::SimpleLogRecordProcessorFactory::Create(std::move(exporter));
    }
    auto options = opentelemetry::sdk::logs::BatchLogRecordProcessorOptions{};
    return opentelemetry::sdk::logs::BatchLogRecordProcessorFactory::Create(std::move(exporter), options);
}

} // namespace nestdaq::otel_detail
