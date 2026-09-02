#pragma once

#include <nestdaq/telemetry/OpenTelemetryInitializer.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if __cplusplus >= 202002L
#  include <span>
#endif

namespace spdlog::sinks {
class sink;
} // namespace spdlog::sinks

namespace nestdaq::telemetry {

inline constexpr std::string_view kDefaultSpdlogConsolePattern{"[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v"};
inline constexpr uint32_t kDefaultSpdlogAsyncQueueSize{8192};
inline constexpr uint32_t kDefaultSpdlogAsyncThreadCount{1};
inline constexpr std::string_view kDefaultSpdlogAsyncOverflowPolicy{"block"};

class TelemetryLibrary;

/**
 * @brief Process-wide spdlog async logger settings used by helper loggers.
 */
struct SpdlogAsyncOptions {
    bool enabled{false};
    uint32_t queue_size{kDefaultSpdlogAsyncQueueSize};
    uint32_t thread_count{kDefaultSpdlogAsyncThreadCount};
    std::string overflow_policy{kDefaultSpdlogAsyncOverflowPolicy};
};

namespace detail {
template<typename T>
using RemoveCvref = std::remove_cv_t<std::remove_reference_t<T>>;

template<typename T>
struct IsMetricValue : std::bool_constant<std::is_arithmetic_v<RemoveCvref<T>> &&
    !std::is_same_v<RemoveCvref<T>, bool>> {
};

template<typename T>
inline constexpr bool kIsMetricValue = IsMetricValue<T>::value;

/**
 * @brief Numeric metric value accepted by the convenience metric overloads.
 *
 * Boolean values are intentionally excluded because OpenTelemetry numeric
 * instruments expect counters, histograms, and gauges to carry numeric
 * quantities rather than flags.
 */
#if __cplusplus >= 202002L
template<typename T>
concept MetricValue = kIsMetricValue<T>;
#endif
} // namespace detail

/**
 * @brief Owns one telemetry attribute until it is converted to the C ABI form.
 *
 * `nestdaq_otel_attribute` stores borrowed pointers, so this wrapper keeps
 * string keys and string values alive while user-facing metric and span calls
 * prepare the temporary C ABI arrays passed to @ref TelemetryLibrary.
 */
class Attribute {
public:
    /** @brief Create a string attribute. */
    Attribute(std::string_view key, std::string_view value);
    /** @brief Create a string attribute from a nullable C string. */
    Attribute(std::string_view key, const char* value);
    /** @brief Create a boolean attribute. */
    Attribute(std::string_view key, bool value);

    /** @brief Create a signed integer attribute. */
    template<typename T,
             std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T> &&
                              !std::is_same_v<std::remove_cv_t<T>, bool>,
                              int> = 0>
    Attribute(std::string_view key, T value)
        : fKey{key}
        , fType{NESTDAQ_OTEL_ATTRIBUTE_INT64}
        , fIntValue{static_cast<int64_t>(value)} {
    }

    /** @brief Create an unsigned integer attribute. */
    template<typename T,
             std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T> &&
                              !std::is_same_v<std::remove_cv_t<T>, bool>,
                              int> = 0>
    Attribute(std::string_view key, T value)
        : fKey{key}
        , fType{NESTDAQ_OTEL_ATTRIBUTE_UINT64}
        , fUIntValue{static_cast<uint64_t>(value)} {
    }

    /** @brief Create a floating-point attribute. */
    template<typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    Attribute(std::string_view key, T value)
        : fKey{key}
        , fType{NESTDAQ_OTEL_ATTRIBUTE_DOUBLE}
        , fDoubleValue{static_cast<double>(value)} {
    }

    /**
     * @brief Return the borrowed C ABI representation.
     *
     * The returned pointers remain valid only while this @ref Attribute object
     * remains alive.
     */
    auto toOtelAttribute() const noexcept -> nestdaq_otel_attribute;

private:
    std::string fKey;
    nestdaq_otel_attribute_type fType{NESTDAQ_OTEL_ATTRIBUTE_STRING};
    std::string fStringValue;
    int64_t fIntValue{0};
    uint64_t fUIntValue{0};
    double fDoubleValue{0.0};
    uint32_t fBoolValue{0};
};

/**
 * @brief Convert owned C++ attributes into the C ABI array representation.
 *
 * The returned array borrows string storage from @p attributes, so callers must
 * pass it to the telemetry backend before the input attributes are destroyed.
 */
auto makeOtelAttributes(const Attribute* attributes, std::size_t attribute_count) -> std::vector<nestdaq_otel_attribute>;

/** @brief Convert an initializer list of attributes into the C ABI representation. */
auto makeOtelAttributes(std::initializer_list<Attribute> attributes) -> std::vector<nestdaq_otel_attribute>;

#if __cplusplus >= 202002L
/** @brief Convert a C++20 attribute span into the C ABI representation. */
auto makeOtelAttributes(std::span<const Attribute> attributes) -> std::vector<nestdaq_otel_attribute>;
#endif

/** @brief Set the process-wide pattern used by native spdlog console fallback sinks. */
auto setSpdlogConsolePattern(std::string_view pattern) -> void;
/** @brief Return the process-wide pattern used by native spdlog console fallback sinks. */
auto getSpdlogConsolePattern() -> std::string;
/** @brief Enable or disable the process-wide native spdlog console sink. */
auto setSpdlogNativeConsoleEnabled(bool enabled) -> void;
/** @brief Return whether helper loggers attach the native spdlog console sink. */
auto getSpdlogNativeConsoleEnabled() -> bool;
/** @brief Set the process-wide spdlog async logger settings. */
auto setSpdlogAsyncOptions(const SpdlogAsyncOptions& options) -> void;
/** @brief Return the process-wide spdlog async logger settings. */
auto getSpdlogAsyncOptions() -> SpdlogAsyncOptions;

/**
 * @brief Movable RAII wrapper for a span handle owned by the OpenTelemetry implementation library.
 *
 * A default-constructed or disabled span is inactive. Destroying an active span
 * calls `end()` exactly once. This wrapper intentionally exposes no
 * OpenTelemetry C++ types so executables can avoid linking OpenTelemetry.
 */
class TelemetrySpan {
public:
    TelemetrySpan() = default;
    /** @brief Wrap a non-zero span handle returned by @ref TelemetryLibrary. */
    TelemetrySpan(TelemetryLibrary& telemetry, uint64_t handle) noexcept;
    TelemetrySpan(const TelemetrySpan&) = delete;
    auto operator=(const TelemetrySpan&) -> TelemetrySpan& = delete;
    TelemetrySpan(TelemetrySpan&& other) noexcept;
    auto operator=(TelemetrySpan&& other) noexcept -> TelemetrySpan&;
    ~TelemetrySpan();

    /** @brief End the span if it is active; safe to call more than once. */
    auto end() noexcept -> void;
    /** @brief Set an attribute on the active span handle. */
    auto setAttribute(const nestdaq_otel_attribute& attribute) -> bool;
    /** @brief Set an attribute on the active span using the C++ wrapper type. */
    auto setAttribute(const Attribute& attribute) -> bool;

private:
    TelemetryLibrary* fTelemetry{nullptr};
    uint64_t fHandle{0};
};

/**
 * @brief User-facing handle for a double counter instrument.
 *
 * A default-constructed counter or a counter created without an active backend
 * is a successful no-op. This lets example and user code keep metric calls even
 * when the OpenTelemetry implementation library is not loaded.
 */
class Counter {
public:
    Counter() = default;
    /** @brief Bind the counter identity to a backend and instrument metadata. */
    Counter(TelemetryLibrary* library, std::string_view name, std::string_view unit, std::string_view description);

    /** @brief Add a double value with optional attributes. */
    auto add(double value, std::initializer_list<Attribute> attributes = {}) const -> bool;

    /** @brief Add an arithmetic value after converting it to double. */
    template<typename T, std::enable_if_t<detail::kIsMetricValue<T>, int> = 0>
    auto add(T value, std::initializer_list<Attribute> attributes = {}) const -> bool {
        return add(static_cast<double>(value), attributes);
    }

private:
    TelemetryLibrary* fLibrary{nullptr};
    std::string fName;
    std::string fUnit;
    std::string fDescription;
};

/**
 * @brief User-facing handle for a double histogram instrument.
 */
class Histogram {
public:
    Histogram() = default;
    /** @brief Bind the histogram identity to a backend and instrument metadata. */
    Histogram(TelemetryLibrary* library, std::string_view name, std::string_view unit, std::string_view description);

    /** @brief Record a double value with optional attributes. */
    auto record(double value, std::initializer_list<Attribute> attributes = {}) const -> bool;

    /** @brief Record an arithmetic value after converting it to double. */
    template<typename T, std::enable_if_t<detail::kIsMetricValue<T>, int> = 0>
    auto record(T value, std::initializer_list<Attribute> attributes = {}) const -> bool {
        return record(static_cast<double>(value), attributes);
    }

private:
    TelemetryLibrary* fLibrary{nullptr};
    std::string fName;
    std::string fUnit;
    std::string fDescription;
};

/**
 * @brief User-facing handle for a double observable gauge instrument.
 *
 * Gauge samples are last-value observations in the user metrics pipeline. The
 * OpenTelemetry implementation library owns the observable instrument and exports the latest
 * value for each attribute set when its reader collects.
 */
class Gauge {
public:
    Gauge() = default;
    /** @brief Bind the gauge identity to a backend and instrument metadata. */
    Gauge(TelemetryLibrary* library, std::string_view name, std::string_view unit, std::string_view description);

    /** @brief Record a double value with optional attributes. */
    auto record(double value, std::initializer_list<Attribute> attributes = {}) const -> bool;

    /** @brief Record an arithmetic value after converting it to double. */
    template<typename T, std::enable_if_t<detail::kIsMetricValue<T>, int> = 0>
    auto record(T value, std::initializer_list<Attribute> attributes = {}) const -> bool {
        return record(static_cast<double>(value), attributes);
    }

private:
    TelemetryLibrary* fLibrary{nullptr};
    std::string fName;
    std::string fUnit;
    std::string fDescription;
};

/**
 * @brief Thin metrics/traces facade over a loaded @ref TelemetryLibrary.
 *
 * The wrapper forwards calls through the runtime-loaded C ABI. Metrics and
 * traces disabled in the active configuration are treated as no-op operations by
 * the implementation library where possible.
 */
class Telemetry {
public:
    Telemetry() = default;
    /**
     * @brief Bind the facade to a loaded telemetry library.
     *
     * The caller must keep @p library alive longer than this facade and any
     * spans created from it.
     */
    explicit Telemetry(TelemetryLibrary& library) noexcept;
    /** @brief Bind the facade to an optional telemetry library pointer. */
    explicit Telemetry(TelemetryLibrary* library) noexcept;

    /**
     * @brief Add @p value to a double counter instrument.
     *
     * Returns true without exporting when no backend is active.
     */
    auto addDoubleCounter(std::string_view name,
                          double value,
                          std::string_view unit = "",
                          std::string_view description = "",
                          const nestdaq_otel_attribute* attributes = nullptr,
                          std::size_t attribute_count = 0) -> bool;

#if __cplusplus >= 202002L
    /** @brief Add @p value to a double counter instrument with C++20 span attributes. */
    auto addDoubleCounter(std::string_view name,
                          double value,
                          std::string_view unit,
                          std::string_view description,
                          std::span<const nestdaq_otel_attribute> attributes) -> bool {
        return addDoubleCounter(name, value, unit, description, attributes.data(), attributes.size());
    }
#endif

    /** @brief Add an arithmetic value to a counter after converting it to double. */
    template<typename T, std::enable_if_t<detail::kIsMetricValue<T>, int> = 0>
    auto addCounter(std::string_view name,
                    T value,
                    std::string_view unit = "",
                    std::string_view description = "",
                    const nestdaq_otel_attribute* attributes = nullptr,
                    std::size_t attribute_count = 0) -> bool {
        return addDoubleCounter(name, static_cast<double>(value), unit, description, attributes, attribute_count);
    }

#if __cplusplus >= 202002L
    /** @brief Add an arithmetic value to a counter with C++20 span attributes. */
    template<typename T, std::enable_if_t<detail::kIsMetricValue<T>, int> = 0>
    auto addCounter(std::string_view name,
                    T value,
                    std::string_view unit,
                    std::string_view description,
                    std::span<const nestdaq_otel_attribute> attributes) -> bool {
        return addDoubleCounter(name, static_cast<double>(value), unit, description, attributes);
    }
#endif

    /**
     * @brief Record @p value in a double histogram instrument.
     *
     * Returns true without exporting when no backend is active.
     */
    auto recordDoubleHistogram(std::string_view name,
                               double value,
                               std::string_view unit = "",
                               std::string_view description = "",
                               const nestdaq_otel_attribute* attributes = nullptr,
                               std::size_t attribute_count = 0) -> bool;

#if __cplusplus >= 202002L
    /** @brief Record @p value in a double histogram instrument with C++20 span attributes. */
    auto recordDoubleHistogram(std::string_view name,
                               double value,
                               std::string_view unit,
                               std::string_view description,
                               std::span<const nestdaq_otel_attribute> attributes) -> bool {
        return recordDoubleHistogram(name, value, unit, description, attributes.data(), attributes.size());
    }
#endif

    /** @brief Record an arithmetic histogram value after converting it to double. */
    template<typename T, std::enable_if_t<detail::kIsMetricValue<T>, int> = 0>
    auto recordHistogram(std::string_view name,
                         T value,
                         std::string_view unit = "",
                         std::string_view description = "",
                         const nestdaq_otel_attribute* attributes = nullptr,
                         std::size_t attribute_count = 0) -> bool {
        return recordDoubleHistogram(name, static_cast<double>(value), unit, description, attributes, attribute_count);
    }

#if __cplusplus >= 202002L
    /** @brief Record an arithmetic histogram value with C++20 span attributes. */
    template<typename T, std::enable_if_t<detail::kIsMetricValue<T>, int> = 0>
    auto recordHistogram(std::string_view name,
                         T value,
                         std::string_view unit,
                         std::string_view description,
                         std::span<const nestdaq_otel_attribute> attributes) -> bool {
        return recordDoubleHistogram(name, static_cast<double>(value), unit, description, attributes);
    }
#endif

    /**
     * @brief Record the latest @p value for a double gauge instrument.
     *
     * Returns true without exporting when no backend is active.
     */
    auto recordDoubleGauge(std::string_view name,
                           double value,
                           std::string_view unit = "",
                           std::string_view description = "",
                           const nestdaq_otel_attribute* attributes = nullptr,
                           std::size_t attribute_count = 0) -> bool;

#if __cplusplus >= 202002L
    /** @brief Record the latest @p value for a double gauge instrument with C++20 span attributes. */
    auto recordDoubleGauge(std::string_view name,
                           double value,
                           std::string_view unit,
                           std::string_view description,
                           std::span<const nestdaq_otel_attribute> attributes) -> bool {
        return recordDoubleGauge(name, value, unit, description, attributes.data(), attributes.size());
    }
#endif

    /** @brief Record an arithmetic gauge value after converting it to double. */
    template<typename T, std::enable_if_t<detail::kIsMetricValue<T>, int> = 0>
    auto recordGauge(std::string_view name,
                     T value,
                     std::string_view unit = "",
                     std::string_view description = "",
                     const nestdaq_otel_attribute* attributes = nullptr,
                     std::size_t attribute_count = 0) -> bool {
        return recordDoubleGauge(name, static_cast<double>(value), unit, description, attributes, attribute_count);
    }

#if __cplusplus >= 202002L
    /** @brief Record an arithmetic gauge value with C++20 span attributes. */
    template<typename T, std::enable_if_t<detail::kIsMetricValue<T>, int> = 0>
    auto recordGauge(std::string_view name,
                     T value,
                     std::string_view unit,
                     std::string_view description,
                     std::span<const nestdaq_otel_attribute> attributes) -> bool {
        return recordDoubleGauge(name, static_cast<double>(value), unit, description, attributes);
    }
#endif

    /**
     * @brief Start a span through the active backend.
     *
     * Returns an inactive span when no backend is active or tracing is disabled.
     */
    auto startSpan(std::string_view name,
                   const nestdaq_otel_attribute* attributes = nullptr,
                   std::size_t attribute_count = 0) -> TelemetrySpan;

#if __cplusplus >= 202002L
    /** @brief Start a span through the active backend with C++20 span attributes. */
    auto startSpan(std::string_view name,
                   std::span<const nestdaq_otel_attribute> attributes) -> TelemetrySpan {
        return startSpan(name, attributes.data(), attributes.size());
    }
#endif

    /** @brief Create a reusable counter handle for one instrument identity. */
    auto counter(std::string_view name,
                 std::string_view unit = "",
                 std::string_view description = "") const -> nestdaq::telemetry::Counter;
    /** @brief Create a reusable histogram handle for one instrument identity. */
    auto histogram(std::string_view name,
                   std::string_view unit = "",
                   std::string_view description = "") const -> nestdaq::telemetry::Histogram;
    /** @brief Create a reusable gauge handle for one instrument identity. */
    auto gauge(std::string_view name,
               std::string_view unit = "",
               std::string_view description = "") const -> nestdaq::telemetry::Gauge;

    /** @brief Start a span with C++ attribute wrappers. */
    auto startSpan(std::string_view name,
                   std::initializer_list<Attribute> attributes) -> TelemetrySpan;

private:
    TelemetryLibrary* fLibrary{nullptr};
};

/**
 * @brief Register the process-wide telemetry backend used by @ref getTelemetry.
 *
 * Passing null disables user-facing telemetry calls. The caller owns the
 * library object and must unregister it before destroying the object.
 */
auto setActiveTelemetryLibrary(TelemetryLibrary* library) noexcept -> void;
/**
 * @brief Create an OTel spdlog sink from the active runtime library.
 *
 * Returns null when OTel logging is disabled, the OpenTelemetry implementation library is not
 * active, or the library was built without spdlog support.
 */
auto createActiveSpdlogSink() -> std::shared_ptr<spdlog::sinks::sink>;
/**
 * @brief Return a lightweight facade bound to the currently active backend.
 *
 * If no backend is active, the returned facade performs successful no-op metric
 * calls and returns inactive spans.
 */
auto getTelemetry() noexcept -> Telemetry;

} // namespace nestdaq::telemetry
