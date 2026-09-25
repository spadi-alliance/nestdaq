#pragma once

#include <nestdaq/telemetry/OpenTelemetryInitializer.h>

#include <memory>

namespace spdlog::sinks {
class sink;
} // namespace spdlog::sinks

namespace nestdaq::telemetry {

/**
 * @brief Create a spdlog sink that forwards spdlog records to OpenTelemetry.
 *
 * This instrumentation is independent of the FairLogger OpenTelemetry sink.
 * Callers explicitly attach the returned sink to each spdlog logger they want
 * to export. NestDAQ does not modify spdlog's default logger or registry.
 */
auto createSpdlogOpenTelemetrySink() -> std::shared_ptr<spdlog::sinks::sink>;

} // namespace nestdaq::telemetry

extern "C" {
NESTDAQ_OTEL_EXPORT auto nestdaq_otel_create_spdlog_sink() -> std::shared_ptr<spdlog::sinks::sink>;
}
