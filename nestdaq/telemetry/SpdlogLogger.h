#pragma once

#include <memory>
#include <string_view>

namespace spdlog {
class logger;
} // namespace spdlog

namespace nestdaq::telemetry {

/**
 * @brief Create a spdlog logger for NestDAQ examples and user devices.
 *
 * The logger is synchronous by default and uses multi-thread-safe sinks. When
 * async mode is enabled through telemetry options, this returns a
 * `spdlog::async_logger` through the same base `spdlog::logger` pointer type.
 * Native console and OTel sinks can be attached to the same logger.
 */
auto createSpdlogLogger(std::string_view name) -> std::shared_ptr<spdlog::logger>;

} // namespace nestdaq::telemetry
