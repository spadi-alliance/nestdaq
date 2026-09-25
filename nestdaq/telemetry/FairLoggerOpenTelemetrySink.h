#pragma once

#include <stdint.h>
#include <string_view>

namespace nestdaq {

/**
 * @brief FairLogger custom sink that forwards log records to OpenTelemetry.
 *
 * The sink is installed by the OpenTelemetry implementation library after a logger provider has
 * been configured. The class exposes only lifecycle controls because FairLogger
 * invokes the actual sink callback internally.
 */
class FairLoggerOpenTelemetrySink {
public:
    FairLoggerOpenTelemetrySink() = delete;

    /** @brief Return the current FairLogger severity threshold used by the sink. */
    static auto getMinSeverity() noexcept -> int32_t;
    /** @brief Install the FairLogger custom sink once for the process. */
    static auto initialize() -> void;
    /** @brief Set the NestDAQ instance id attached to subsequent log records. */
    static auto setNestdaqInstanceId(std::string_view instance_id) -> void;
    /** @brief Set the minimum FairLogger severity exported as OpenTelemetry logs. */
    static auto setMinSeverity(int32_t severity) noexcept -> void;
    /** @brief Remove the custom sink and clear per-process log attributes. */
    static auto shutdown() noexcept -> void;
};

} // namespace nestdaq
