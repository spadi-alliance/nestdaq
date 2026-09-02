/** @file
 *  @brief Implements the spdlog sink that forwards log records to OpenTelemetry.
 */

#include "nestdaq/telemetry/SpdlogOpenTelemetrySink.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string_view>

#include <opentelemetry/common/timestamp.h>
#include <opentelemetry/logs/log_record.h>
#include <opentelemetry/logs/logger.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/logs/severity.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/semconv/code_attributes.h>
#include <opentelemetry/semconv/incubating/thread_attributes.h>

#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <spdlog/version.h>

namespace nestdaq::telemetry {
namespace {

#define NESTDAQ_STRINGIFY_IMPL(value) #value // NOLINT(cppcoreguidelines-macro-usage): stringify version macros.
#define NESTDAQ_STRINGIFY(value) NESTDAQ_STRINGIFY_IMPL(value) // NOLINT(cppcoreguidelines-macro-usage)

constexpr std::string_view kLoggerName{"spdlog"};
constexpr std::string_view kLibraryName{"spdlog"};
constexpr std::string_view kLibraryVersion{
    NESTDAQ_STRINGIFY(SPDLOG_VER_MAJOR) "." NESTDAQ_STRINGIFY(SPDLOG_VER_MINOR) "." NESTDAQ_STRINGIFY(SPDLOG_VER_PATCH)};
constexpr std::string_view kSchemaUrl;

#undef NESTDAQ_STRINGIFY
#undef NESTDAQ_STRINGIFY_IMPL

auto convertSeverity(spdlog::level::level_enum level) noexcept -> opentelemetry::logs::Severity;
template<typename StringView>
auto toStringView(StringView value) noexcept -> opentelemetry::nostd::string_view;

auto convertSeverity(spdlog::level::level_enum level) noexcept -> opentelemetry::logs::Severity
{
    using opentelemetry::logs::Severity;
    switch (level) {
    case spdlog::level::trace:
        return Severity::kTrace;
    case spdlog::level::debug:
        return Severity::kDebug;
    case spdlog::level::info:
        return Severity::kInfo;
    case spdlog::level::warn:
        return Severity::kWarn;
    case spdlog::level::err:
        return Severity::kError;
    case spdlog::level::critical:
        return Severity::kFatal;
    case spdlog::level::off:
    case spdlog::level::n_levels:
        return Severity::kInvalid;
    }
    return Severity::kInvalid;
}

template<typename StringView>
auto toStringView(StringView value) noexcept -> opentelemetry::nostd::string_view
{
    return {value.data(), value.size()};
}

class SpdlogOpenTelemetrySink final : public spdlog::sinks::base_sink<std::mutex> {
protected:
    auto sink_it_(const spdlog::details::log_msg& msg) -> void override
    {
        try {
            auto provider = opentelemetry::logs::Provider::GetLoggerProvider();
            auto logger = provider->GetLogger(toStringView(kLoggerName),
                                              toStringView(kLibraryName),
                                              toStringView(kLibraryVersion),
                                              toStringView(kSchemaUrl));
            auto log_record = logger->CreateLogRecord();
            if (!log_record) {
                return;
            }

            log_record->SetTimestamp(opentelemetry::common::SystemTimestamp{msg.time});
            log_record->SetObservedTimestamp(opentelemetry::common::SystemTimestamp{std::chrono::system_clock::now()});
            log_record->SetSeverity(convertSeverity(msg.level));
            log_record->SetBody(toStringView(msg.payload));

            log_record->SetAttribute("spdlog.logger.name", toStringView(msg.logger_name));
            log_record->SetAttribute("spdlog.level", toStringView(spdlog::level::to_string_view(msg.level)));
            log_record->SetAttribute(opentelemetry::semconv::thread::kThreadId, static_cast<int64_t>(msg.thread_id));

            if (msg.source.filename != nullptr && !std::string_view{msg.source.filename}.empty()) {
                log_record->SetAttribute(opentelemetry::semconv::code::kCodeFilePath,
                                         toStringView(std::string_view{msg.source.filename}));
            }
            if (msg.source.line > 0) {
                log_record->SetAttribute(opentelemetry::semconv::code::kCodeLineNumber,
                                         static_cast<int64_t>(msg.source.line));
            }
            if (msg.source.funcname != nullptr && !std::string_view{msg.source.funcname}.empty()) {
                log_record->SetAttribute(opentelemetry::semconv::code::kCodeFunctionName,
                                         toStringView(std::string_view{msg.source.funcname}));
            }

            logger->EmitLogRecord(std::move(log_record));
        } catch (const std::exception& ex) {
            std::cerr << "SpdlogOpenTelemetrySink: failed to emit log record: " << ex.what() << '\n';
        } catch (...) {
            std::cerr << "SpdlogOpenTelemetrySink: failed to emit log record\n";
        }
    }

    auto flush_() -> void override {}
};

} // namespace

auto createSpdlogOpenTelemetrySink() -> std::shared_ptr<spdlog::sinks::sink>
{
    return std::make_shared<SpdlogOpenTelemetrySink>();
}

} // namespace nestdaq::telemetry

extern "C" {
    NESTDAQ_OTEL_EXPORT auto nestdaq_otel_create_spdlog_sink() -> std::shared_ptr<spdlog::sinks::sink>
    {
        return nestdaq::telemetry::createSpdlogOpenTelemetrySink();
    }
}
