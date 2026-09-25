/**
 * @file telemetry_library_tests.cxx
 * @brief Catch2 tests for loading `libnestdaq_otel.so` through the runtime loader.
 */

#include <catch2/catch_test_macros.hpp>

#if NESTDAQ_HAVE_SPDLOG
#  include <spdlog/async_logger.h>
#  include <spdlog/spdlog.h>
#endif

#include <fairmq/Version.h>
#include <fairlogger/Logger.h>

#include <nestdaq/telemetry/FairLoggerTelemetryLoader.h>
#include <nestdaq/telemetry/Telemetry.h>

#if NESTDAQ_HAVE_SPDLOG
#  include <nestdaq/telemetry/SpdlogOpenTelemetrySink.h>
#  include <nestdaq/telemetry/SpdlogLogger.h>
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct CoutCapture {
    std::ostringstream output;
    std::streambuf* old_buffer{std::cout.rdbuf(output.rdbuf())};

    CoutCapture() = default;
    CoutCapture(const CoutCapture&) = delete;
    CoutCapture& operator=(const CoutCapture&) = delete;
    CoutCapture(CoutCapture&&) = delete;
    CoutCapture& operator=(CoutCapture&&) = delete;

    ~CoutCapture() {
        std::cout.rdbuf(old_buffer);
    }
};

auto baseConfig() -> nestdaq_otel_config {
    auto config = nestdaq_otel_config{};
    config.size = sizeof(config);
    config.logs = nestdaq::telemetry::makeSignalConfig(
                      "", nestdaq::telemetry::kDefaultLogHttpEndpoint, nestdaq::telemetry::kDefaultGrpcEndpoint, "", 1U);
    config.metrics = nestdaq::telemetry::makeSignalConfig(
                         "", nestdaq::telemetry::kDefaultMetricHttpEndpoint, nestdaq::telemetry::kDefaultGrpcEndpoint, "", 1U);
    config.traces = nestdaq::telemetry::makeSignalConfig(
                        "", nestdaq::telemetry::kDefaultTraceHttpEndpoint, nestdaq::telemetry::kDefaultGrpcEndpoint, "", 1U);
    config.service_name = "nestdaq-test";
    config.service_namespace = "nestdaq";
    config.service_instance_id = "test-instance";
    config.host_name = "test-host";
    config.nestdaq_instance_id = "";
    config.nestdaq_instance_id_status = "unresolved";
    config.fairmq_id = "";
    config.fairmq_device = "";
    config.fairmq_session = "";
    config.fairmq_transport = "";
    config.fairmq_git_version = FAIRMQ_GIT_VERSION;
    config.fairmq_build_type = FAIRMQ_BUILD_TYPE;
    config.fairmq_repo_url = FAIRMQ_REPO_URL;
    config.fairmq_license = FAIRMQ_LICENSE;
    config.fairmq_copyright = FAIRMQ_COPYRIGHT;
    config.min_severity = static_cast<int32_t>(fair::Severity::info);
    config.timeout_ms = nestdaq::telemetry::kDefaultTimeoutMs;
    config.metric_export_interval_ms = nestdaq::telemetry::kDefaultMetricExportIntervalMs;
    return config;
}

auto disabledConfig() -> nestdaq_otel_config {
    return baseConfig();
}

auto logOnlyConfig() -> nestdaq_otel_config {
    auto config = baseConfig();
    config.logs.protocol = "console";
    return config;
}

auto metricsConsoleConfig() -> nestdaq_otel_config {
    auto config = baseConfig();
    config.metrics.protocol = "console";
    config.service_namespace = "nestdaq";
    config.service_instance_id = "test-instance";
    config.nestdaq_instance_id = "sampler-0";
    config.nestdaq_instance_id_status = "resolved";
    config.timeout_ms = 50;
    config.metric_export_interval_ms = 100;
    return config;
}

auto traceConsoleConfig() -> nestdaq_otel_config {
    auto config = baseConfig();
    config.traces.protocol = "console";
    config.service_namespace = "nestdaq";
    config.service_instance_id = "test-instance";
    config.nestdaq_instance_id = "sampler-0";
    config.nestdaq_instance_id_status = "resolved";
    config.timeout_ms = 50;
    return config;
}

auto logsAndMetricsConsoleConfig() -> nestdaq_otel_config {
    auto config = baseConfig();
    config.logs.protocol = "console";
    config.metrics.protocol = "console";
    config.service_namespace = "nestdaq";
    config.service_instance_id = "test-instance";
    config.nestdaq_instance_id = "sampler-0";
    config.nestdaq_instance_id_status = "resolved";
    config.timeout_ms = 50;
    config.metric_export_interval_ms = 100;
    return config;
}

auto extractJsonLog(std::string_view logs, std::string_view root) -> nlohmann::json {
    const auto kMarker = std::string{"{\""} + std::string{root} + "\":";
    const auto kBegin = logs.find(kMarker);
    REQUIRE(kBegin != std::string_view::npos);
    const auto kLineEnd = logs.find('\n', kBegin);
    const auto kJsonText = logs.substr(kBegin, kLineEnd == std::string_view::npos ? logs.size() - kBegin : kLineEnd - kBegin);
    return nlohmann::json::parse(kJsonText);
}

auto countOccurrences(std::string_view haystack, std::string_view needle) -> std::size_t {
    auto count = std::size_t{0};
    auto offset = std::size_t{0};
    while (true) {
        offset = haystack.find(needle, offset);
        if (offset == std::string_view::npos) {
            return count;
        }
        ++count;
        offset += needle.size();
    }
}

} // namespace

TEST_CASE("OpenTelemetry implementation library loads through the telemetry loader", "[telemetry][library]") {
    auto library = nestdaq::telemetry::TelemetryLibrary{};

    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    CHECK(library.initializeWith(disabledConfig()));
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);
}

TEST_CASE("OpenTelemetry implementation library rejects severity values outside FairLogger range", "[telemetry][library]") {
    auto library = nestdaq::telemetry::TelemetryLibrary{};

    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(disabledConfig()));

    CHECK(library.setMinSeverity(static_cast<int32_t>(fair::Severity::fatal)));
    CHECK_FALSE(library.setMinSeverity(-1));
    CHECK_FALSE(library.setMinSeverity(static_cast<int32_t>(fair::Logger::fSeverityNames.size())));

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);
}

TEST_CASE("FairMQ throughput logs are safe when metrics are disabled", "[telemetry][library]") {
    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));

    LOG(info) << "data: in: 123 (4.5 MB) out: 6.7 (8.9 MB)";

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);
}

TEST_CASE("FairLogger severity records OTel fields and original severity attributes", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));

    LOG(warn) << "severity attribute probe";

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    CHECK(kLogs.find("severity attribute probe") != std::string::npos);
    CHECK(kLogs.find("severity_num       : 13") != std::string::npos);
    CHECK(kLogs.find("severity_text      : WARN") != std::string::npos);
    CHECK(kLogs.find("fairlogger.severity.number: 10") != std::string::npos);
    CHECK(kLogs.find("fairlogger.severity.text: WARN") != std::string::npos);
}

TEST_CASE("FairLogger logs include NestDAQ instance id attributes", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));
    REQUIRE(library.setNestdaqInstanceId("sampler-0"));

    LOG(warn) << "nestdaq instance id probe";

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    CHECK(kLogs.find("nestdaq instance id probe") != std::string::npos);
    CHECK(kLogs.find("host.name: test-host") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.id: sampler-0") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.name: sampler") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.index: 0") != std::string::npos);
}

TEST_CASE("FairLogger logs use unresolved resource before NestDAQ instance id is known", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));

    LOG(warn) << "early unresolved nestdaq instance id probe";

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    CHECK(kLogs.find("early unresolved nestdaq instance id probe") != std::string::npos);
    CHECK(kLogs.find("host.name: test-host") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.id.status: unresolved") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.id:") == std::string::npos);
}

TEST_CASE("FairLogger logs use resolved resource after NestDAQ instance id reinitialization", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));

    LOG(warn) << "before resolved nestdaq instance id";

    auto resolved_config = logOnlyConfig();
    resolved_config.nestdaq_instance_id = "sampler-0";
    resolved_config.nestdaq_instance_id_status = "resolved";
    REQUIRE(library.initializeWith(resolved_config));
    REQUIRE(library.setNestdaqInstanceId("sampler-0"));

    LOG(warn) << "after resolved nestdaq instance id";

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    CHECK(kLogs.find("before resolved nestdaq instance id") != std::string::npos);
    CHECK(kLogs.find("after resolved nestdaq instance id") != std::string::npos);
    CHECK(kLogs.find("host.name: test-host") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.id.status: unresolved") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.id.status: resolved") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.id: sampler-0") != std::string::npos);
}

TEST_CASE("FairLogger logs omit derived NestDAQ instance fields for non-indexed ids", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));
    REQUIRE(library.setNestdaqInstanceId("sampler-main"));

    LOG(warn) << "nestdaq non indexed instance id probe";

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    CHECK(kLogs.find("nestdaq non indexed instance id probe") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.id: sampler-main") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.name:") == std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.index:") == std::string::npos);
}

TEST_CASE("FairLogger NestDAQ instance id is cleared on shutdown", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));
    REQUIRE(library.setNestdaqInstanceId("sink-1"));
    LOG(warn) << "before instance id clear";
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    auto library_after_shutdown = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library_after_shutdown.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library_after_shutdown.initializeWith(logOnlyConfig()));
    LOG(warn) << "after instance id clear";
    library_after_shutdown.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    CHECK(kLogs.find("before instance id clear") != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.id: sink-1") != std::string::npos);
    const auto kAfter = kLogs.find("after instance id clear");
    REQUIRE(kAfter != std::string::npos);
    CHECK(kLogs.find("nestdaq.instance.id:", kAfter) == std::string::npos);
}

#if NESTDAQ_HAVE_SPDLOG
TEST_CASE("spdlog sink exports logs independently from FairLogger instrumentation", "[telemetry][library][spdlog]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));
    REQUIRE(library.setMinSeverity(static_cast<int32_t>(fair::Severity::fatal)));

    auto logger = spdlog::logger{"otel-spdlog-test", {nestdaq::telemetry::createSpdlogOpenTelemetrySink()}};
    logger.set_level(spdlog::level::trace);
    logger.warn("spdlog warning probe");

    LOG(warn) << "fairlogger warning filtered by fatal threshold";

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    CHECK(kLogs.find("spdlog warning probe") != std::string::npos);
    CHECK(kLogs.find("fairlogger warning filtered by fatal threshold") == std::string::npos);
    CHECK(kLogs.find("severity_num       : 13") != std::string::npos);
    CHECK(kLogs.find("severity_text      : WARN") != std::string::npos);
    CHECK(kLogs.find("spdlog.logger.name: otel-spdlog-test") != std::string::npos);
    CHECK(kLogs.find("spdlog.level: warn") != std::string::npos);
}

TEST_CASE("spdlog logger helper exports through active OpenTelemetry implementation library", "[telemetry][library][spdlog]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));
    nestdaq::telemetry::setActiveTelemetryLibrary(&library);
    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(false);

    auto logger = nestdaq::telemetry::createSpdlogLogger("helper-spdlog-test");
    logger->set_level(spdlog::level::trace);
    logger->info("spdlog helper probe");

    nestdaq::telemetry::setActiveTelemetryLibrary(nullptr);
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    CHECK(kLogs.find("spdlog helper probe") != std::string::npos);
    CHECK(kLogs.find("spdlog.logger.name: helper-spdlog-test") != std::string::npos);
    CHECK(kLogs.find("spdlog.level: info") != std::string::npos);
    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(true);
}

TEST_CASE("spdlog logger helper can attach native console and active telemetry sinks", "[telemetry][library][spdlog]") {
    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));
    nestdaq::telemetry::setActiveTelemetryLibrary(&library);
    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(true);

    auto logger = nestdaq::telemetry::createSpdlogLogger("helper-spdlog-multi-sink-test");

    nestdaq::telemetry::setActiveTelemetryLibrary(nullptr);
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    CHECK(logger->sinks().size() == 2);
}

TEST_CASE("spdlog logger helper respects native console flag without active telemetry", "[telemetry][library][spdlog]") {
    nestdaq::telemetry::setActiveTelemetryLibrary(nullptr);
    nestdaq::telemetry::setSpdlogAsyncOptions({});

    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(true);
    auto console_logger = nestdaq::telemetry::createSpdlogLogger("helper-spdlog-console-only-test");
    CHECK(console_logger->sinks().size() == 1);

    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(false);
    auto disabled_logger = nestdaq::telemetry::createSpdlogLogger("helper-spdlog-disabled-test");
    CHECK(disabled_logger->sinks().empty());

    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(true);
}

TEST_CASE("spdlog logger helper creates async logger when enabled", "[telemetry][library][spdlog]") {
    nestdaq::telemetry::setActiveTelemetryLibrary(nullptr);
    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(false);
    nestdaq::telemetry::setSpdlogAsyncOptions({
        .enabled = true,
        .queue_size = 256,
        .thread_count = 1,
        .overflow_policy = "block",
    });

    auto logger = nestdaq::telemetry::createSpdlogLogger("helper-spdlog-async-test");

    CHECK(dynamic_cast<spdlog::async_logger*>(logger.get()) != nullptr);
    CHECK(logger->sinks().empty());

    nestdaq::telemetry::setSpdlogAsyncOptions({});
    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(true);
}

TEST_CASE("spdlog async logger helper tolerates multi-thread logging", "[telemetry][library][spdlog]") {
    nestdaq::telemetry::setActiveTelemetryLibrary(nullptr);
    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(false);
    nestdaq::telemetry::setSpdlogAsyncOptions({
        .enabled = true,
        .queue_size = 1024,
        .thread_count = 1,
        .overflow_policy = "block",
    });

    auto logger = nestdaq::telemetry::createSpdlogLogger("helper-spdlog-async-thread-test");
    logger->set_level(spdlog::level::trace);

    auto threads = std::vector<std::thread> {};
    for (auto index = 0; index < 4; ++index) {
        threads.emplace_back([logger, index] {
            for (auto message = 0; message < 25; ++message) {
                logger->info("thread {} message {}", index, message);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    logger->flush();

    CHECK(dynamic_cast<spdlog::async_logger*>(logger.get()) != nullptr);

    nestdaq::telemetry::setSpdlogAsyncOptions({});
    nestdaq::telemetry::setSpdlogNativeConsoleEnabled(true);
}

TEST_CASE("spdlog sink records source location attributes", "[telemetry][library][spdlog]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));

    auto logger = spdlog::logger{"otel-spdlog-source-test", {nestdaq::telemetry::createSpdlogOpenTelemetrySink()}};
    logger.set_level(spdlog::level::trace);
    logger.log(spdlog::source_loc{"source-file.cxx", 123, "source_function"},
               spdlog::level::err,
               "spdlog source probe");

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    CHECK(kLogs.find("spdlog source probe") != std::string::npos);
    CHECK(kLogs.find("severity_num       : 17") != std::string::npos);
    CHECK(kLogs.find("severity_text      : ERROR") != std::string::npos);
    CHECK(kLogs.find("code.file.path: source-file.cxx") != std::string::npos);
    CHECK(kLogs.find("code.line.number: 123") != std::string::npos);
    CHECK(kLogs.find("code.function.name: source_function") != std::string::npos);
    CHECK(kLogs.find("thread.id") != std::string::npos);
}
#endif

TEST_CASE("FairMQ build metadata is logged instead of stored as resource attributes", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logOnlyConfig()));
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kLogs = capture.output.str();
    const auto kNestdaqJson = extractJsonLog(kLogs, "nestdaq");
    const auto kFairmqJson = extractJsonLog(kLogs, "fairmq");

    CHECK(kNestdaqJson["nestdaq"]["version"]["string"].is_string());
    CHECK(kNestdaqJson["nestdaq"]["version"]["major"].is_number_unsigned());
    CHECK(kNestdaqJson["nestdaq"]["version"]["minor"].is_number_unsigned());
    CHECK(kNestdaqJson["nestdaq"]["version"]["patch"].is_number_unsigned());
    CHECK(kNestdaqJson["nestdaq"]["version"]["prerelease"].is_string());
    CHECK(kNestdaqJson["nestdaq"]["build"]["type"].is_string());
    CHECK(kNestdaqJson["nestdaq"]["git"]["commit_count"].is_number_unsigned());
    CHECK(kNestdaqJson["nestdaq"]["git"]["commit_hash"].is_string());
    CHECK(kNestdaqJson["nestdaq"]["git"]["branch"].is_string());
    CHECK(kNestdaqJson["nestdaq"]["git"]["remote_url"].is_string());
    CHECK(kNestdaqJson["nestdaq"]["git"]["commit_date"].is_string());

    CHECK(kFairmqJson["fairmq"]["version"]["string"].is_string());
    CHECK(kFairmqJson["fairmq"]["version"]["major"].is_number_unsigned());
    CHECK(kFairmqJson["fairmq"]["version"]["minor"].is_number_unsigned());
    CHECK(kFairmqJson["fairmq"]["version"]["patch"].is_number_unsigned());
    CHECK(kFairmqJson["fairmq"]["version"]["git"].is_string());
    CHECK(kFairmqJson["fairmq"]["build"]["type"].is_string());
    CHECK(kFairmqJson["fairmq"]["source"]["repo_url"].is_string());
    CHECK(kFairmqJson["fairmq"]["license"].is_string());
    CHECK(kFairmqJson["fairmq"]["copyright"].is_string());

    REQUIRE(kLogs.find("{\"nestdaq\":") != std::string::npos);
    REQUIRE(kLogs.find("{\"fairmq\":") != std::string::npos);
    CHECK(kLogs.find("{\"nestdaq\":") < kLogs.find("{\"fairmq\":"));
    CHECK(kLogs.find("NestDAQ version:") == std::string::npos);
    CHECK(kLogs.find("FairMQ git_version:") == std::string::npos);
    CHECK(kLogs.find("fairmq.git_version") == std::string::npos);
    CHECK(kLogs.find("fairmq.build_type") == std::string::npos);
    CHECK(kLogs.find("fairmq.repo_url") == std::string::npos);
    CHECK(kLogs.find("fairmq.license") == std::string::npos);
    CHECK(kLogs.find("fairmq.copyright") == std::string::npos);
}

TEST_CASE("metrics console initializes and exports resource attributes", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(metricsConsoleConfig()));

    auto telemetry = nestdaq::telemetry::Telemetry{library};
    CHECK(telemetry.addDoubleCounter("probe.counter", 42.0, "1", "probe counter"));
    nestdaq::telemetry::setActiveTelemetryLibrary(&library);
    auto user_telemetry = nestdaq::telemetry::getTelemetry();
    CHECK(user_telemetry.addCounter("user.inferred.counter", 1, "1", "inferred counter"));
    CHECK(user_telemetry.recordHistogram("user.inferred.histogram", uint64_t{4096}, "By", "inferred histogram"));
    CHECK(user_telemetry.recordGauge("user.inferred.gauge", 12.5F, "1", "inferred gauge"));
    CHECK(user_telemetry.counter("user.messages.total", "1", "user messages")
    .add(3, {{"channel", "data"}, {"running", true}, {"partition", uint64_t{2}}}));
    CHECK(user_telemetry.histogram("user.decode.duration", "ms", "user decode duration")
    .record(4.5F, {{"channel", "data"}, {"attempt", int64_t{1}}, {"ratio", 0.5}}));
    CHECK(user_telemetry.gauge("user.queue.depth", "1", "user queue depth")
    .record(uint64_t{1234}, {{"channel", "data"}, {"slot", uint64_t{2}}}));
    CHECK(user_telemetry.gauge("user.queue.depth", "1", "user queue depth")
    .record(9876.5, {{"channel", "data"}, {"slot", uint64_t{2}}}));

    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    nestdaq::telemetry::setActiveTelemetryLibrary(nullptr);
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(kOutput.find("probe.counter") != std::string::npos);
    CHECK(kOutput.find("service.name") != std::string::npos);
    CHECK(kOutput.find("nestdaq-test") != std::string::npos);
    CHECK(kOutput.find("service.namespace") != std::string::npos);
    CHECK(kOutput.find("service.instance.id") != std::string::npos);
    CHECK(kOutput.find("test-instance") != std::string::npos);
    CHECK(kOutput.find("host.name") != std::string::npos);
    CHECK(kOutput.find("test-host") != std::string::npos);
    CHECK(kOutput.find("nestdaq.instance.id") != std::string::npos);
    CHECK(kOutput.find("sampler-0") != std::string::npos);
    CHECK(kOutput.find("nestdaq.instance.id.status") != std::string::npos);
    CHECK(kOutput.find("resolved") != std::string::npos);
    CHECK(kOutput.find("user.messages.total") != std::string::npos);
    CHECK(kOutput.find("user.decode.duration") != std::string::npos);
    CHECK(kOutput.find("user.queue.depth") != std::string::npos);
    CHECK(kOutput.find("user.inferred.counter") != std::string::npos);
    CHECK(kOutput.find("user.inferred.histogram") != std::string::npos);
    CHECK(kOutput.find("user.inferred.gauge") != std::string::npos);
    CHECK(kOutput.find("channel") != std::string::npos);
    CHECK(kOutput.find("data") != std::string::npos);
    CHECK(kOutput.find("running") != std::string::npos);
    CHECK(kOutput.find("partition") != std::string::npos);
    CHECK(kOutput.find("attempt") != std::string::npos);
    CHECK(kOutput.find("ratio") != std::string::npos);
    CHECK(kOutput.find("slot") != std::string::npos);
    CHECK(kOutput.find("9876.5") != std::string::npos);
}

TEST_CASE("user telemetry facade accepts low-level attribute arrays", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(metricsConsoleConfig()));

    auto telemetry = nestdaq::telemetry::Telemetry{library};
    auto attributes = std::array{
        nestdaq_otel_attribute{
            .key = "channel",
            .type = NESTDAQ_OTEL_ATTRIBUTE_STRING,
            .string_value = "data",
            .int_value = 0,
            .uint_value = 0,
            .double_value = 0.0,
            .bool_value = 0,
        },
        nestdaq_otel_attribute{
            .key = "slot",
            .type = NESTDAQ_OTEL_ATTRIBUTE_UINT64,
            .string_value = "",
            .int_value = 0,
            .uint_value = 2,
            .double_value = 0.0,
            .bool_value = 0,
        },
    };
    CHECK(telemetry.addCounter(
              "lowlevel.counter", 1, "1", "low-level counter", attributes.data(), attributes.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(kOutput.find("lowlevel.counter") != std::string::npos);
    CHECK(kOutput.find("channel") != std::string::npos);
    CHECK(kOutput.find("data") != std::string::npos);
    CHECK(kOutput.find("slot") != std::string::npos);
}

#if __cplusplus >= 202002L
TEST_CASE("user telemetry facade accepts C++20 span attributes", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(metricsConsoleConfig()));

    auto telemetry = nestdaq::telemetry::Telemetry{library};
    auto attributes = std::array{
        nestdaq_otel_attribute{
            .key = "channel",
            .type = NESTDAQ_OTEL_ATTRIBUTE_STRING,
            .string_value = "data",
            .int_value = 0,
            .uint_value = 0,
            .double_value = 0.0,
            .bool_value = 0,
        },
        nestdaq_otel_attribute{
            .key = "slot",
            .type = NESTDAQ_OTEL_ATTRIBUTE_UINT64,
            .string_value = "",
            .int_value = 0,
            .uint_value = 2,
            .double_value = 0.0,
            .bool_value = 0,
        },
    };
    CHECK(telemetry.addCounter(
              "span.counter", 1, "1", "span counter", std::span<const nestdaq_otel_attribute> {attributes}));
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(kOutput.find("span.counter") != std::string::npos);
    CHECK(kOutput.find("channel") != std::string::npos);
    CHECK(kOutput.find("data") != std::string::npos);
    CHECK(kOutput.find("slot") != std::string::npos);
}
#endif

TEST_CASE("user telemetry facade is no-op before a backend is registered", "[telemetry][library]") {
    nestdaq::telemetry::setActiveTelemetryLibrary(nullptr);

    auto telemetry = nestdaq::telemetry::getTelemetry();
    CHECK(telemetry.addCounter("unregistered.counter", 1));
    CHECK(telemetry.recordHistogram("unregistered.histogram", 2));
    CHECK(telemetry.recordGauge("unregistered.gauge", 3));
    CHECK(telemetry.counter("unregistered.counter", "1", "unregistered counter").add(1.0));
    CHECK(telemetry.histogram("unregistered.histogram", "ms", "unregistered histogram").record(2.0));
    CHECK(telemetry.gauge("unregistered.gauge", "1", "unregistered gauge").record(3.0));

    auto span = telemetry.startSpan("unregistered-span", {{"component", "test"}});
    CHECK_FALSE(span.setAttribute({"payload.bytes", int64_t{128}}));
}

TEST_CASE("user telemetry facade exports RAII spans and attributes", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(traceConsoleConfig()));
    nestdaq::telemetry::setActiveTelemetryLibrary(&library);

    {
        auto span = nestdaq::telemetry::getTelemetry().startSpan("user-decode", {{"channel", "data"}});
        CHECK(span.setAttribute({"payload.bytes", int64_t{128}}));
        auto moved = std::move(span);
        CHECK(moved.setAttribute({"ok", true}));
    }

    nestdaq::telemetry::setActiveTelemetryLibrary(nullptr);
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(kOutput.find("user-decode") != std::string::npos);
    CHECK(kOutput.find("channel") != std::string::npos);
    CHECK(kOutput.find("data") != std::string::npos);
    CHECK(kOutput.find("payload.bytes") != std::string::npos);
    CHECK(kOutput.find("ok") != std::string::npos);
    CHECK(kOutput.find("host.name") != std::string::npos);
    CHECK(kOutput.find("test-host") != std::string::npos);
    CHECK(kOutput.find("nestdaq.instance.id") != std::string::npos);
    CHECK(kOutput.find("sampler-0") != std::string::npos);
    CHECK(kOutput.find("nestdaq.instance.id.status") != std::string::npos);
    CHECK(kOutput.find("resolved") != std::string::npos);
}

TEST_CASE("process metrics export without FairLogger logs or MetricsPlugin", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(metricsConsoleConfig()));

    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(kOutput.find("process.cpu.time") != std::string::npos);
    CHECK(kOutput.find("process.cpu.utilization") != std::string::npos);
    CHECK(kOutput.find("process.memory.usage") != std::string::npos);
    CHECK(kOutput.find("cpu.mode: user") != std::string::npos);
    CHECK(kOutput.find("cpu.mode: system") != std::string::npos);
    CHECK(kOutput.find("unit\t\t: s") != std::string::npos);
    CHECK(kOutput.find("unit\t\t: 1") != std::string::npos);
    CHECK(kOutput.find("unit\t\t: By") != std::string::npos);
    CHECK(kOutput.find("process.cpu.usage_percent") == std::string::npos);
    CHECK(kOutput.find("process.memory.rss_mib") == std::string::npos);
    CHECK(kOutput.find("fairmq.channel.messages_per_second") == std::string::npos);
    CHECK(kOutput.find("data: in:") == std::string::npos);
}

TEST_CASE("user force flush exports no framework metrics when no framework samples are pending", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(metricsConsoleConfig()));

    CHECK(library.forceFlush(nestdaq::telemetry::kDefaultTimeoutMs));
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(kOutput.find("process.cpu.time") == std::string::npos);
    CHECK(kOutput.find("process.cpu.utilization") == std::string::npos);
    CHECK(kOutput.find("process.memory.usage") == std::string::npos);
    CHECK(kOutput.find("process.cpu.usage_percent") == std::string::npos);
    CHECK(kOutput.find("process.memory.rss_mib") == std::string::npos);
    CHECK(kOutput.find("fairmq.channel.messages_per_second") == std::string::npos);
    CHECK(kOutput.find("fairmq.channel.megabytes_per_second") == std::string::npos);
    CHECK(kOutput.find("fairmq.state.id") == std::string::npos);
}

TEST_CASE("FairMQ throughput metrics export parsed rate log samples", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logsAndMetricsConsoleConfig()));

    LOG(info) << "data: in: 123 (4.5 MB) out: 6.7 (8.9 MB)";

    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(kOutput.find("fairmq.channel.messages_per_second") != std::string::npos);
    CHECK(kOutput.find("fairmq.channel.megabytes_per_second") != std::string::npos);
    CHECK(kOutput.find("fairmq.channel.name") != std::string::npos);
    CHECK(kOutput.find("network.io.direction") != std::string::npos);
}

TEST_CASE("FairMQ throughput metrics are not re-exported without a new log sample", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(logsAndMetricsConsoleConfig()));

    LOG(info) << "data: in: 123 (4.5 MB) out: 6.7 (8.9 MB)";

    const auto kAfterLog = capture.output.str();
    REQUIRE(kAfterLog.find("fairmq.channel.messages_per_second") != std::string::npos);
    REQUIRE(kAfterLog.find("fairmq.channel.megabytes_per_second") != std::string::npos);
    const auto kMessagesCount = countOccurrences(kAfterLog, "fairmq.channel.messages_per_second");
    const auto kMegabytesCount = countOccurrences(kAfterLog, "fairmq.channel.megabytes_per_second");

    CHECK(library.forceFlush(nestdaq::telemetry::kDefaultTimeoutMs));
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(countOccurrences(kOutput, "fairmq.channel.messages_per_second") == kMessagesCount);
    CHECK(countOccurrences(kOutput, "fairmq.channel.megabytes_per_second") == kMegabytesCount);
}

TEST_CASE("FairMQ state metrics export transitions once", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(metricsConsoleConfig()));

    library.recordFrameworkFairMQState(12, "RUNNING");

    const auto kAfterState = capture.output.str();
    REQUIRE(kAfterState.find("fairmq.state.id") != std::string::npos);
    REQUIRE(kAfterState.find("fairmq.state.name") != std::string::npos);
    REQUIRE(kAfterState.find("RUNNING") != std::string::npos);
    const auto kStateMetricCount = countOccurrences(kAfterState, "fairmq.state.id");

    CHECK(library.forceFlush(nestdaq::telemetry::kDefaultTimeoutMs));
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(countOccurrences(kOutput, "fairmq.state.id") == kStateMetricCount);
}

TEST_CASE("framework metrics flush does not export user metrics", "[telemetry][library]") {
    auto capture = CoutCapture{};

    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(metricsConsoleConfig()));

    auto telemetry = nestdaq::telemetry::Telemetry{library};
    CHECK(telemetry.addDoubleCounter("user.framework_isolation.counter", 1.0, "1", "framework isolation"));

    library.recordFrameworkFairMQState(11, "READY");

    const auto kAfterFrameworkFlush = capture.output.str();
    CHECK(kAfterFrameworkFlush.find("fairmq.state.id") != std::string::npos);
    CHECK(kAfterFrameworkFlush.find("user.framework_isolation.counter") == std::string::npos);

    CHECK(library.forceFlush(nestdaq::telemetry::kDefaultTimeoutMs));
    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);

    const auto kOutput = capture.output.str();
    CHECK(kOutput.find("user.framework_isolation.counter") != std::string::npos);
}

TEST_CASE("disabled metric and trace signals are no-op through loaded implementation library", "[telemetry][library]") {
    auto library = nestdaq::telemetry::TelemetryLibrary{};
    REQUIRE(library.load(NESTDAQ_OTEL_LIBRARY_PATH));
    REQUIRE(library.initializeWith(disabledConfig()));

    auto telemetry = nestdaq::telemetry::Telemetry{library};
    CHECK(telemetry.addDoubleCounter("disabled.counter", 1.0, "1", "disabled counter"));
    CHECK(telemetry.recordDoubleHistogram("disabled.histogram", 2.0, "ms", "disabled histogram"));
    CHECK(telemetry.recordDoubleGauge("disabled.gauge", 3.0, "1", "disabled gauge"));

    auto span = telemetry.startSpan("disabled-span");
    const auto kAttribute = nestdaq_otel_attribute{
        .key = "component",
        .type = NESTDAQ_OTEL_ATTRIBUTE_STRING,
        .string_value = "test",
        .int_value = 0,
        .uint_value = 0,
        .double_value = 0.0,
        .bool_value = 0,
    };
    CHECK_FALSE(span.setAttribute(kAttribute));

    library.shutdownTelemetry(nestdaq::telemetry::kDefaultTimeoutMs);
}
