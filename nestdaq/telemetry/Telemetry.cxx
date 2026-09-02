/** @file
 *  @brief Implements the OpenTelemetry-unlinked user telemetry facade.
 */

#include "nestdaq/telemetry/Telemetry.h"

#include "nestdaq/telemetry/FairLoggerTelemetryLoader.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace nestdaq::telemetry {

Attribute::Attribute(std::string_view key, std::string_view value)
    : fKey{key}
    , fStringValue{value} {
}

Attribute::Attribute(std::string_view key, const char* value)
    : Attribute{key, value == nullptr ? std::string_view{} : std::string_view{value}} {
}

Attribute::Attribute(std::string_view key, bool value)
    : fKey{key}
    , fType{NESTDAQ_OTEL_ATTRIBUTE_BOOL}
    , fBoolValue{value ? 1U : 0U} {
}

auto Attribute::toOtelAttribute() const noexcept -> nestdaq_otel_attribute {
    return nestdaq_otel_attribute{
        .key = fKey.data(),
        .type = fType,
        .string_value = fStringValue.data(),
        .int_value = fIntValue,
        .uint_value = fUIntValue,
        .double_value = fDoubleValue,
        .bool_value = fBoolValue,
    };
}

auto makeOtelAttributes(const Attribute* attributes, std::size_t attribute_count) -> std::vector<nestdaq_otel_attribute> {
    auto values = std::vector<nestdaq_otel_attribute> {};
    values.reserve(attribute_count);
#if !defined(__clang__) && defined(__GNUC__) && (__GNUC__ < 9)
    for (std::size_t index = 0; index < attribute_count; ++index) {
        values.push_back(attributes[index].toOtelAttribute());
    }
#else
    std::for_each_n(attributes, attribute_count, [&values](const Attribute& attribute) {
        values.push_back(attribute.toOtelAttribute());
    });
#endif
    return values;
}

auto makeOtelAttributes(std::initializer_list<Attribute> attributes) -> std::vector<nestdaq_otel_attribute> {
    return makeOtelAttributes(attributes.begin(), attributes.size());
}

#if __cplusplus >= 202002L
auto makeOtelAttributes(std::span<const Attribute> attributes) -> std::vector<nestdaq_otel_attribute> {
    return makeOtelAttributes(attributes.data(), attributes.size());
}
#endif

namespace {
auto spdlogConsolePatternMutex() -> std::mutex& {
    static auto gSpdlogConsolePatternMutex = std::mutex{};
    return gSpdlogConsolePatternMutex;
}

auto spdlogConsolePatternStorage() -> std::string& {
    static auto gSpdlogConsolePatternStorage = std::string{kDefaultSpdlogConsolePattern};
    return gSpdlogConsolePatternStorage;
}

auto spdlogNativeConsoleEnabledStorage() -> bool& {
    static auto gSpdlogNativeConsoleEnabled = true;
    return gSpdlogNativeConsoleEnabled;
}

auto isValidSpdlogAsyncOverflowPolicy(std::string_view value) -> bool {
    return value == "block" || value == "overrun_oldest" || value == "discard_new";
}

auto normalizeSpdlogAsyncOptions(SpdlogAsyncOptions options) -> SpdlogAsyncOptions {
    if (options.queue_size == 0) {
        options.queue_size = kDefaultSpdlogAsyncQueueSize;
    }
    if (options.thread_count == 0) {
        options.thread_count = kDefaultSpdlogAsyncThreadCount;
    }
    if (!isValidSpdlogAsyncOverflowPolicy(options.overflow_policy)) {
        options.overflow_policy = kDefaultSpdlogAsyncOverflowPolicy;
    }
    return options;
}

auto spdlogAsyncOptionsStorage() -> SpdlogAsyncOptions& {
    static auto gSpdlogAsyncOptions = SpdlogAsyncOptions{};
    return gSpdlogAsyncOptions;
}
} // namespace

auto setSpdlogConsolePattern(std::string_view pattern) -> void {
    const auto kLock = std::scoped_lock{spdlogConsolePatternMutex()};
    spdlogConsolePatternStorage() = pattern;
}

auto getSpdlogConsolePattern() -> std::string {
    const auto kLock = std::scoped_lock{spdlogConsolePatternMutex()};
    return spdlogConsolePatternStorage();
}

auto setSpdlogNativeConsoleEnabled(bool enabled) -> void {
    const auto kLock = std::scoped_lock{spdlogConsolePatternMutex()};
    spdlogNativeConsoleEnabledStorage() = enabled;
}

auto getSpdlogNativeConsoleEnabled() -> bool {
    const auto kLock = std::scoped_lock{spdlogConsolePatternMutex()};
    return spdlogNativeConsoleEnabledStorage();
}

auto setSpdlogAsyncOptions(const SpdlogAsyncOptions& options) -> void {
    const auto kLock = std::scoped_lock{spdlogConsolePatternMutex()};
    spdlogAsyncOptionsStorage() = normalizeSpdlogAsyncOptions(options);
}

auto getSpdlogAsyncOptions() -> SpdlogAsyncOptions {
    const auto kLock = std::scoped_lock{spdlogConsolePatternMutex()};
    return spdlogAsyncOptionsStorage();
}

TelemetrySpan::TelemetrySpan(TelemetryLibrary& telemetry, uint64_t handle) noexcept
    : fTelemetry {
    &telemetry
}
, fHandle{handle} {
}

TelemetrySpan::TelemetrySpan(TelemetrySpan&& other) noexcept
    : fTelemetry {
    other.fTelemetry
}
, fHandle{other.fHandle} {
    other.fTelemetry = nullptr;
    other.fHandle = 0;
}

auto TelemetrySpan::operator=(TelemetrySpan&& other) noexcept -> TelemetrySpan& {
    if (this != &other) {
        end();
        fTelemetry = other.fTelemetry;
        fHandle = other.fHandle;
        other.fTelemetry = nullptr;
        other.fHandle = 0;
    }
    return *this;
}

TelemetrySpan::~TelemetrySpan() {
    end();
}

auto TelemetrySpan::end() noexcept -> void {
    if (fTelemetry != nullptr && fHandle != 0) {
        fTelemetry->spanEnd(fHandle);
        fHandle = 0;
    }
}

auto TelemetrySpan::setAttribute(const nestdaq_otel_attribute& attribute) -> bool {
    return fTelemetry != nullptr && fHandle != 0 && fTelemetry->spanSetAttribute(fHandle, attribute);
}

auto TelemetrySpan::setAttribute(const Attribute& attribute) -> bool {
    const auto kOtelAttribute = attribute.toOtelAttribute();
    return setAttribute(kOtelAttribute);
}

Counter::Counter(TelemetryLibrary* library, std::string_view name, std::string_view unit, std::string_view description)
    : fLibrary{library}
    , fName{name}
    , fUnit{unit}
    , fDescription{description} {
}

auto Counter::add(double value, std::initializer_list<Attribute> attributes) const -> bool {
    if (fLibrary == nullptr) {
        return true;
    }
    const auto kAttrs = makeOtelAttributes(attributes);
    return fLibrary->metricAddDoubleCounter(fName, value, fUnit, fDescription, kAttrs.data(), kAttrs.size());
}

Histogram::Histogram(TelemetryLibrary* library, std::string_view name, std::string_view unit, std::string_view description)
    : fLibrary{library}
    , fName{name}
    , fUnit{unit}
    , fDescription{description} {
}

auto Histogram::record(double value, std::initializer_list<Attribute> attributes) const -> bool {
    if (fLibrary == nullptr) {
        return true;
    }
    const auto kAttrs = makeOtelAttributes(attributes);
    return fLibrary->metricRecordDoubleHistogram(fName, value, fUnit, fDescription, kAttrs.data(), kAttrs.size());
}

Gauge::Gauge(TelemetryLibrary* library, std::string_view name, std::string_view unit, std::string_view description)
    : fLibrary{library}
    , fName{name}
    , fUnit{unit}
    , fDescription{description} {
}

auto Gauge::record(double value, std::initializer_list<Attribute> attributes) const -> bool {
    if (fLibrary == nullptr) {
        return true;
    }
    const auto kAttrs = makeOtelAttributes(attributes);
    return fLibrary->metricRecordDoubleGauge(fName, value, fUnit, fDescription, kAttrs.data(), kAttrs.size());
}

Telemetry::Telemetry(TelemetryLibrary& library) noexcept
    : fLibrary {
    &library
}
{
}

Telemetry::Telemetry(TelemetryLibrary* library) noexcept
    : fLibrary {
    library
}
{
}

auto Telemetry::addDoubleCounter(std::string_view name,
                                 double value,
                                 std::string_view unit,
                                 std::string_view description,
                                 const nestdaq_otel_attribute* attributes,
                                 std::size_t attribute_count) -> bool {
    if (fLibrary == nullptr) {
        return true;
    }
    return fLibrary->metricAddDoubleCounter(name, value, unit, description, attributes, attribute_count);
}

auto Telemetry::recordDoubleHistogram(std::string_view name,
                                      double value,
                                      std::string_view unit,
                                      std::string_view description,
                                      const nestdaq_otel_attribute* attributes,
                                      std::size_t attribute_count) -> bool {
    if (fLibrary == nullptr) {
        return true;
    }
    return fLibrary->metricRecordDoubleHistogram(name, value, unit, description, attributes, attribute_count);
}

auto Telemetry::recordDoubleGauge(std::string_view name,
                                  double value,
                                  std::string_view unit,
                                  std::string_view description,
                                  const nestdaq_otel_attribute* attributes,
                                  std::size_t attribute_count) -> bool {
    if (fLibrary == nullptr) {
        return true;
    }
    return fLibrary->metricRecordDoubleGauge(name, value, unit, description, attributes, attribute_count);
}

auto Telemetry::startSpan(std::string_view name,
                          const nestdaq_otel_attribute* attributes,
                          std::size_t attribute_count) -> TelemetrySpan {
    if (fLibrary == nullptr) {
        return {};
    }
    return TelemetrySpan{*fLibrary, fLibrary->spanStart(name, attributes, attribute_count)};
}

auto Telemetry::counter(std::string_view name,
                        std::string_view unit,
                        std::string_view description) const -> nestdaq::telemetry::Counter {
    return {fLibrary, name, unit, description};
}

auto Telemetry::histogram(std::string_view name,
                          std::string_view unit,
                          std::string_view description) const -> nestdaq::telemetry::Histogram {
    return {fLibrary, name, unit, description};
}

auto Telemetry::gauge(std::string_view name,
                      std::string_view unit,
                      std::string_view description) const -> nestdaq::telemetry::Gauge {
    return {fLibrary, name, unit, description};
}

auto Telemetry::startSpan(std::string_view name, std::initializer_list<Attribute> attributes) -> TelemetrySpan {
    if (fLibrary == nullptr) {
        return {};
    }
    const auto kAttrs = makeOtelAttributes(attributes);
    return TelemetrySpan{*fLibrary, fLibrary->spanStart(name, kAttrs.data(), kAttrs.size())};
}

namespace {
/**
 * @brief Process-wide backend pointer used by user-facing convenience APIs.
 *
 * Ownership remains with the caller that loaded the OpenTelemetry implementation library. Atomic
 * access lets FairMQ callbacks and user code read the active backend without
 * taking locks.
 */
auto activeTelemetryLibrary() noexcept -> std::atomic<TelemetryLibrary*>& {
    static auto gActiveTelemetryLibrary = std::atomic<TelemetryLibrary*> {nullptr};
    return gActiveTelemetryLibrary;
}
} // namespace

auto setActiveTelemetryLibrary(TelemetryLibrary* library) noexcept -> void {
    activeTelemetryLibrary().store(library, std::memory_order_release);
}

auto createActiveSpdlogSink() -> std::shared_ptr<spdlog::sinks::sink> {
    auto* library = activeTelemetryLibrary().load(std::memory_order_acquire);
    if (library == nullptr) {
        return {};
    }
    return library->createSpdlogSink();
}

auto getTelemetry() noexcept -> Telemetry {
    return Telemetry{activeTelemetryLibrary().load(std::memory_order_acquire)};
}

} // namespace nestdaq::telemetry
