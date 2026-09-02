/**
 * @file telemetry_options_tests.cxx
 * @brief Catch2 tests for telemetry option parsing and C ABI config mapping.
 */

#include <catch2/catch_test_macros.hpp>

#include <boost/program_options.hpp>
#include <fairmq/ProgOptions.h>
#include <fairlogger/Logger.h>
#include <nestdaq/telemetry/FairLoggerTelemetryLoader.h>

#include <array>
#include <boost/uuid/string_generator.hpp>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto clearTelemetryEnvironment() -> void {
    const auto kNames = std::array{
        "NESTDAQ_OTEL_LIBRARY",
        "NESTDAQ_OTEL_LOG_PROTOCOL",
        "NESTDAQ_OTEL_METRIC_PROTOCOL",
        "NESTDAQ_OTEL_TRACE_PROTOCOL",
        "NESTDAQ_OTEL_LOG_ENDPOINT_HTTP",
        "NESTDAQ_OTEL_LOG_ENDPOINT_GRPC",
        "NESTDAQ_OTEL_METRIC_ENDPOINT_HTTP",
        "NESTDAQ_OTEL_METRIC_ENDPOINT_GRPC",
        "NESTDAQ_OTEL_TRACE_ENDPOINT_HTTP",
        "NESTDAQ_OTEL_TRACE_ENDPOINT_GRPC",
        "NESTDAQ_OTEL_LOG_HEADERS",
        "NESTDAQ_OTEL_METRIC_HEADERS",
        "NESTDAQ_OTEL_TRACE_HEADERS",
        "NESTDAQ_OTEL_LOG_SEVERITY",
        "NESTDAQ_OTEL_LOG_REQUIRED",
        "NESTDAQ_SPDLOG_CONSOLE_PATTERN",
        "NESTDAQ_SPDLOG_NATIVE_CONSOLE",
        "NESTDAQ_SPDLOG_ASYNC",
        "NESTDAQ_SPDLOG_ASYNC_QUEUE_SIZE",
        "NESTDAQ_SPDLOG_ASYNC_THREAD_COUNT",
        "NESTDAQ_SPDLOG_ASYNC_OVERFLOW_POLICY",
    };

    for (const auto* name : kNames) {
        unsetenv(name); // NOLINT(concurrency-mt-unsafe)
    }
}

auto parse(std::vector<std::string> arguments) -> nestdaq::telemetry::TelemetryOptions {
    auto argv = std::vector<char*> {};
    argv.reserve(arguments.size());
    for (auto& argument : arguments) {
        argv.emplace_back(argument.data());
    }
    return nestdaq::telemetry::parseTelemetryOptions(static_cast<int>(argv.size()), argv.data(), "test-service");
}

auto isUuidString(std::string_view value) -> bool {
    try {
        static_cast<void>(boost::uuids::string_generator{}(std::string{value}));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

auto readWithBoostOptions(std::vector<std::string> arguments,
                          std::string_view default_service_name) -> nestdaq::telemetry::TelemetryOptions {
    namespace bpo = boost::program_options;
    auto argv = std::vector<char*> {};
    argv.reserve(arguments.size());
    for (auto& argument : arguments) {
        argv.emplace_back(argument.data());
    }

    auto description = bpo::options_description{"test"};
    nestdaq::telemetry::addTelemetryOptions(description, default_service_name);
    auto vm = bpo::variables_map{};
    bpo::store(bpo::command_line_parser(static_cast<int>(argv.size()), argv.data()).options(description).run(), vm);
    bpo::notify(vm);
    return nestdaq::telemetry::readTelemetryOptions(vm, default_service_name);
}

} // namespace

TEST_CASE("telemetry options keep unified otel library default", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({"test"});

    CHECK(kOptions.library == "libnestdaq_otel.so");
    CHECK(kOptions.log_protocol == "console");
    CHECK(kOptions.metric_protocol.empty());
    CHECK(kOptions.trace_protocol.empty());
    CHECK(kOptions.metric_export_interval_ms == 1000);
    CHECK(kOptions.spdlog_console_pattern == nestdaq::telemetry::kDefaultSpdlogConsolePattern);
    CHECK(kOptions.spdlog_native_console);
    CHECK_FALSE(kOptions.spdlog_async);
    CHECK(kOptions.spdlog_async_queue_size == nestdaq::telemetry::kDefaultSpdlogAsyncQueueSize);
    CHECK(kOptions.spdlog_async_thread_count == nestdaq::telemetry::kDefaultSpdlogAsyncThreadCount);
    CHECK(kOptions.spdlog_async_overflow_policy == nestdaq::telemetry::kDefaultSpdlogAsyncOverflowPolicy);

    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);
    CHECK(kConfig.metric_export_interval_ms == 1000);
}

TEST_CASE("spdlog async options follow command line and environment", "[telemetry]") {
    clearTelemetryEnvironment();

    setenv("NESTDAQ_SPDLOG_ASYNC", "true", 1); // NOLINT(concurrency-mt-unsafe)
    setenv("NESTDAQ_SPDLOG_ASYNC_QUEUE_SIZE", "1024", 1); // NOLINT(concurrency-mt-unsafe)
    setenv("NESTDAQ_SPDLOG_ASYNC_THREAD_COUNT", "2", 1); // NOLINT(concurrency-mt-unsafe)
    setenv("NESTDAQ_SPDLOG_ASYNC_OVERFLOW_POLICY", "overrun_oldest", 1); // NOLINT(concurrency-mt-unsafe)

    const auto kEnvOptions = parse({"test"});
    CHECK(kEnvOptions.spdlog_async);
    CHECK(kEnvOptions.spdlog_async_queue_size == 1024);
    CHECK(kEnvOptions.spdlog_async_thread_count == 2);
    CHECK(kEnvOptions.spdlog_async_overflow_policy == "overrun_oldest");

    const auto kCliOptions = parse({
        "test",
        "--spdlog-async=false",
        "--spdlog-async-queue-size",
        "2048",
        "--spdlog-async-thread-count=3",
        "--spdlog-async-overflow-policy=discard_new",
    });
    CHECK_FALSE(kCliOptions.spdlog_async);
    CHECK(kCliOptions.spdlog_async_queue_size == 2048);
    CHECK(kCliOptions.spdlog_async_thread_count == 3);
    CHECK(kCliOptions.spdlog_async_overflow_policy == "discard_new");

    clearTelemetryEnvironment();
}

TEST_CASE("spdlog async options normalize invalid values", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({
        "test",
        "--spdlog-async-queue-size=0",
        "--spdlog-async-thread-count=0",
        "--spdlog-async-overflow-policy=drop_everything",
    });

    CHECK(kOptions.spdlog_async_queue_size == nestdaq::telemetry::kDefaultSpdlogAsyncQueueSize);
    CHECK(kOptions.spdlog_async_thread_count == nestdaq::telemetry::kDefaultSpdlogAsyncThreadCount);
    CHECK(kOptions.spdlog_async_overflow_policy == nestdaq::telemetry::kDefaultSpdlogAsyncOverflowPolicy);
}

TEST_CASE("spdlog native console option follows command line and environment", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kCliOptions = parse({"test", "--spdlog-native-console=false"});
    CHECK_FALSE(kCliOptions.spdlog_native_console);

    setenv("NESTDAQ_SPDLOG_NATIVE_CONSOLE", "off", 1); // NOLINT(concurrency-mt-unsafe)
    const auto kEnvOptions = parse({"test"});
    CHECK_FALSE(kEnvOptions.spdlog_native_console);

    const auto kOverrideOptions = parse({"test", "--spdlog-native-console", "true"});
    CHECK(kOverrideOptions.spdlog_native_console);

    clearTelemetryEnvironment();
}

TEST_CASE("spdlog console pattern follows command line and environment", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kCliOptions = parse({"test", "--spdlog-console-pattern=[%n] %v"});
    CHECK(kCliOptions.spdlog_console_pattern == "[%n] %v");

    setenv("NESTDAQ_SPDLOG_CONSOLE_PATTERN", "%l:%v", 1); // NOLINT(concurrency-mt-unsafe)
    const auto kEnvOptions = parse({"test"});
    CHECK(kEnvOptions.spdlog_console_pattern == "%l:%v");

    const auto kOverrideOptions = parse({"test", "--spdlog-console-pattern", "%v"});
    CHECK(kOverrideOptions.spdlog_console_pattern == "%v");

    clearTelemetryEnvironment();
}

TEST_CASE("spdlog console pattern facade stores process setting", "[telemetry]") {
    nestdaq::telemetry::setSpdlogConsolePattern("%v");
    CHECK(nestdaq::telemetry::getSpdlogConsolePattern() == "%v");

    nestdaq::telemetry::setSpdlogConsolePattern(nestdaq::telemetry::kDefaultSpdlogConsolePattern);
    CHECK(nestdaq::telemetry::getSpdlogConsolePattern() == nestdaq::telemetry::kDefaultSpdlogConsolePattern);
}

TEST_CASE("spdlog native console facade stores process setting", "[telemetry]") {
    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(false);
    CHECK_FALSE(nestdaq::telemetry::getSpdlogNativeConsoleEnabled());

    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(true);
    CHECK(nestdaq::telemetry::getSpdlogNativeConsoleEnabled());
}

TEST_CASE("spdlog async facade stores process setting", "[telemetry]") {
    nestdaq::telemetry::setSpdlogAsyncOptions({
        .enabled = true,
        .queue_size = 4096,
        .thread_count = 2,
        .overflow_policy = "discard_new",
    });

    const auto kEnabledOptions = nestdaq::telemetry::getSpdlogAsyncOptions();
    CHECK(kEnabledOptions.enabled);
    CHECK(kEnabledOptions.queue_size == 4096);
    CHECK(kEnabledOptions.thread_count == 2);
    CHECK(kEnabledOptions.overflow_policy == "discard_new");

    nestdaq::telemetry::setSpdlogAsyncOptions({});
    const auto kDefaultOptions = nestdaq::telemetry::getSpdlogAsyncOptions();
    CHECK_FALSE(kDefaultOptions.enabled);
    CHECK(kDefaultOptions.queue_size == nestdaq::telemetry::kDefaultSpdlogAsyncQueueSize);
    CHECK(kDefaultOptions.thread_count == nestdaq::telemetry::kDefaultSpdlogAsyncThreadCount);
    CHECK(kDefaultOptions.overflow_policy == nestdaq::telemetry::kDefaultSpdlogAsyncOverflowPolicy);
}

TEST_CASE("spdlog async options are read through Boost options", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = readWithBoostOptions({
        "test",
        "--spdlog-async=true",
        "--spdlog-async-queue-size=512",
        "--spdlog-async-thread-count=2",
        "--spdlog-async-overflow-policy=overrun_oldest",
    }, "boost-default");

    CHECK(kOptions.spdlog_async);
    CHECK(kOptions.spdlog_async_queue_size == 512);
    CHECK(kOptions.spdlog_async_thread_count == 2);
    CHECK(kOptions.spdlog_async_overflow_policy == "overrun_oldest");
}

TEST_CASE("spdlog async environment survives Boost option defaults", "[telemetry]") {
    clearTelemetryEnvironment();

    setenv("NESTDAQ_SPDLOG_ASYNC", "true", 1); // NOLINT(concurrency-mt-unsafe)
    setenv("NESTDAQ_SPDLOG_ASYNC_QUEUE_SIZE", "1024", 1); // NOLINT(concurrency-mt-unsafe)
    setenv("NESTDAQ_SPDLOG_ASYNC_THREAD_COUNT", "2", 1); // NOLINT(concurrency-mt-unsafe)
    setenv("NESTDAQ_SPDLOG_ASYNC_OVERFLOW_POLICY", "discard_new", 1); // NOLINT(concurrency-mt-unsafe)

    const auto kOptions = readWithBoostOptions({"daq-webctl"}, "daq-webctl");

    CHECK(kOptions.spdlog_async);
    CHECK(kOptions.spdlog_async_queue_size == 1024);
    CHECK(kOptions.spdlog_async_thread_count == 2);
    CHECK(kOptions.spdlog_async_overflow_policy == "discard_new");

    clearTelemetryEnvironment();
}

TEST_CASE("telemetry command line options populate multi-signal config", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({
        "test",
        "--otel-library=/tmp/libnestdaq_otel.so",
        "--otel-log-protocol=console,otlp-http",
        "--otel-metric-protocol=otlp-http",
        "--otel-trace-protocol=otlp-grpc",
        "--otel-log-endpoint-http=http://collector:4318/v1/logs",
        "--otel-log-endpoint-grpc=collector:4317",
        "--otel-metric-endpoint-http=http://collector:4318/v1/metrics",
        "--otel-metric-endpoint-grpc=collector:4317",
        "--otel-trace-endpoint-http=http://collector:4318/v1/traces",
        "--otel-trace-endpoint-grpc=collector:4317",
        "--otel-log-headers=log-key=log-value",
        "--otel-metric-headers=metric-key=metric-value",
        "--otel-trace-headers=trace-key=trace-value",
        "--otel-log-severity=warn",
        "--otel-timeout-ms=1234",
        "--otel-metric-export-interval-ms=5678",
        "--otel-log-http-json=false",
        "--otel-metric-http-json=true",
        "--otel-trace-http-json=false",
    });

    CHECK(kOptions.library == "/tmp/libnestdaq_otel.so");
    CHECK(kOptions.log_protocol == "console,otlp-http");
    CHECK(kOptions.metric_protocol == "otlp-http");
    CHECK(kOptions.trace_protocol == "otlp-grpc");

    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);
    CHECK(kConfig.size == sizeof(nestdaq_otel_config));
    CHECK(std::string_view{kConfig.logs.protocol} == "console,otlp-http");
    CHECK(std::string_view{kConfig.metrics.protocol} == "otlp-http");
    CHECK(std::string_view{kConfig.traces.protocol} == "otlp-grpc");
    CHECK(std::string_view{kConfig.logs.endpoint_http} == "http://collector:4318/v1/logs");
    CHECK(std::string_view{kConfig.metrics.endpoint_http} == "http://collector:4318/v1/metrics");
    CHECK(std::string_view{kConfig.traces.endpoint_grpc} == "collector:4317");
    CHECK(std::string_view{kConfig.logs.headers} == "log-key=log-value");
    CHECK(std::string_view{kConfig.metrics.headers} == "metric-key=metric-value");
    CHECK(std::string_view{kConfig.traces.headers} == "trace-key=trace-value");
    CHECK(kConfig.min_severity == static_cast<int32_t>(fair::Severity::warn));
    CHECK(kConfig.timeout_ms == 1234);
    CHECK(kConfig.metric_export_interval_ms == 5678);
    CHECK(kConfig.logs.otlp_http_json == 0);
    CHECK(kConfig.metrics.otlp_http_json == 1);
    CHECK(kConfig.traces.otlp_http_json == 0);
}

TEST_CASE("telemetry log severity parsing follows FairLogger severity names", "[telemetry]") {
    using nestdaq::telemetry::parseFairLoggerSeverity;
    using nestdaq::telemetry::severityToFairLoggerValue;

    CHECK_FALSE(parseFairLoggerSeverity("warn").used_fallback);
    CHECK(severityToFairLoggerValue("warn") == static_cast<int32_t>(fair::Severity::warn));
    CHECK(severityToFairLoggerValue("warning") == static_cast<int32_t>(fair::Severity::warn));
    CHECK(severityToFairLoggerValue("WARN") == static_cast<int32_t>(fair::Severity::warn));
    CHECK(severityToFairLoggerValue("fatal") == static_cast<int32_t>(fair::Severity::fatal));
    CHECK(severityToFairLoggerValue("NOLOG") == static_cast<int32_t>(fair::Severity::nolog));

    const auto kUnknownSeverity = parseFairLoggerSeverity("unknown");
    CHECK(kUnknownSeverity.used_fallback);
    CHECK(kUnknownSeverity.value == static_cast<int32_t>(fair::Severity::info));
    CHECK(severityToFairLoggerValue("unknown") == static_cast<int32_t>(fair::Severity::info));
}

TEST_CASE("telemetry service name follows DAQ service option for devices", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptionsWithEquals = parse({"test", "--service-name=Sampler"});
    const auto kConfigWithEquals = nestdaq::telemetry::makeConfig(kOptionsWithEquals);

    CHECK(std::string_view{kConfigWithEquals.service_name} == "sampler");

    const auto kOptionsWithSpace = parse({"test", "--service-name", "Processor"});
    const auto kConfigWithSpace = nestdaq::telemetry::makeConfig(kOptionsWithSpace);

    CHECK(std::string_view{kConfigWithSpace.service_name} == "processor");
}

TEST_CASE("explicit telemetry service name overrides DAQ service option", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptionsAfter = parse({"test", "--service-name=Sampler", "--otel-service-name=NullDevice"});
    const auto kConfigAfter = nestdaq::telemetry::makeConfig(kOptionsAfter);

    CHECK(std::string_view{kConfigAfter.service_name} == "nulldevice");

    const auto kOptionsBefore = parse({"test", "--otel-service-name=explicit", "--service-name=Sampler"});
    const auto kConfigBefore = nestdaq::telemetry::makeConfig(kOptionsBefore);

    CHECK(std::string_view{kConfigBefore.service_name} == "explicit");
}

TEST_CASE("telemetry service name keeps daq-webctl default through Boost options", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = readWithBoostOptions({"daq-webctl"}, "daq-webctl");
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK(std::string_view{kConfig.service_name} == "daq-webctl");
}

TEST_CASE("telemetry service name falls back to executable basename for devices", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({"/opt/nestdaq/bin/Sink"});
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK(std::string_view{kConfig.service_name} == "sink");
}

TEST_CASE("telemetry service namespace defaults to nestdaq", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({"test"});
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK(std::string_view{kConfig.service_namespace} == "nestdaq");
}

TEST_CASE("explicit telemetry service namespace overrides default", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({"test", "--otel-service-namespace=custom"});
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK(std::string_view{kConfig.service_namespace} == "custom");
}

TEST_CASE("telemetry service namespace defaults to nestdaq through Boost options", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = readWithBoostOptions({"daq-webctl"}, "daq-webctl");
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK(std::string_view{kConfig.service_namespace} == "nestdaq");
}

TEST_CASE("telemetry service instance id defaults to a generated uuid", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({"test"});
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK(kOptions.generated_service_instance_id);
    REQUIRE_FALSE(kOptions.service_instance_id.empty());
    CHECK(isUuidString(kOptions.service_instance_id));
    CHECK(std::string_view{kConfig.service_instance_id} == kOptions.service_instance_id);
}

TEST_CASE("telemetry NestDAQ instance id resource starts unresolved", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({"test"});
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK(std::string_view{kConfig.nestdaq_instance_id}.empty());
    CHECK(std::string_view{kConfig.nestdaq_instance_id_status} == "unresolved");
}

TEST_CASE("telemetry host name resource is detected by default", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({"test"});
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK(std::string_view{kConfig.host_name} == kOptions.host_name);
    if (!nestdaq::telemetry::detectHostName().empty()) {
        CHECK_FALSE(kOptions.host_name.empty());
    }
}

TEST_CASE("telemetry service instance id follows plugin uuid option", "[telemetry]") {
    clearTelemetryEnvironment();

    constexpr auto kUuid = std::string_view{"123e4567-e89b-12d3-a456-426614174000"};
    const auto kOptionsWithEquals = parse({"test", "--uuid=123e4567-e89b-12d3-a456-426614174000"});
    const auto kConfigWithEquals = nestdaq::telemetry::makeConfig(kOptionsWithEquals);

    CHECK_FALSE(kOptionsWithEquals.generated_service_instance_id);
    CHECK(std::string_view{kConfigWithEquals.service_instance_id} == kUuid);

    const auto kOptionsWithSpace = parse({"test", "--uuid", "123e4567-e89b-12d3-a456-426614174000"});
    const auto kConfigWithSpace = nestdaq::telemetry::makeConfig(kOptionsWithSpace);

    CHECK_FALSE(kOptionsWithSpace.generated_service_instance_id);
    CHECK(std::string_view{kConfigWithSpace.service_instance_id} == kUuid);
}

TEST_CASE("explicit telemetry service instance id overrides plugin uuid option", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({
        "test",
        "--uuid=123e4567-e89b-12d3-a456-426614174000",
        "--otel-service-instance-id=explicit-instance",
    });
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK_FALSE(kOptions.generated_service_instance_id);
    CHECK(std::string_view{kConfig.service_instance_id} == "explicit-instance");
}

TEST_CASE("telemetry service instance id is generated through Boost options", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = readWithBoostOptions({"daq-webctl"}, "daq-webctl");
    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);

    CHECK(kOptions.generated_service_instance_id);
    REQUIRE_FALSE(kOptions.service_instance_id.empty());
    CHECK(isUuidString(kOptions.service_instance_id));
    CHECK(std::string_view{kConfig.service_instance_id} == kOptions.service_instance_id);
}

TEST_CASE("generated telemetry uuid populates missing FairMQ uuid property", "[telemetry]") {
    clearTelemetryEnvironment();

    auto options = parse({"test"});
    auto config = fair::mq::ProgOptions{};

    nestdaq::telemetry::setGeneratedUuidProperty(config, options);

    REQUIRE(config.Count("uuid") == 1);
    CHECK(config.GetProperty<std::string>("uuid") == options.service_instance_id);
}

TEST_CASE("generated telemetry uuid does not overwrite FairMQ uuid property", "[telemetry]") {
    clearTelemetryEnvironment();

    constexpr auto kExistingUuid = std::string_view{"123e4567-e89b-12d3-a456-426614174000"};
    auto options = parse({"test"});
    auto config = fair::mq::ProgOptions{};
    config.SetProperty<std::string>("uuid", std::string{kExistingUuid});

    nestdaq::telemetry::setGeneratedUuidProperty(config, options);

    CHECK(config.GetProperty<std::string>("uuid") == kExistingUuid);
}

TEST_CASE("empty telemetry protocol disables the selected signal", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({
        "test",
        "--otel-log-protocol",
        "--otel-metric-protocol",
        "--otel-trace-protocol",
    });

    const auto kConfig = nestdaq::telemetry::makeConfig(kOptions);
    CHECK(std::string_view{kConfig.logs.protocol}.empty());
    CHECK(std::string_view{kConfig.metrics.protocol}.empty());
    CHECK(std::string_view{kConfig.traces.protocol}.empty());
}

TEST_CASE("removed otel-log-library option is ignored", "[telemetry]") {
    clearTelemetryEnvironment();

    const auto kOptions = parse({"test", "--otel-log-library=/tmp/old.so"});

    CHECK(kOptions.library == "libnestdaq_otel.so");
}
