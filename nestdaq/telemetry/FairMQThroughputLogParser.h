#pragma once

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>

namespace nestdaq::telemetry {

/**
 * @brief Parsed FairMQ channel throughput rates from the standard Device log line.
 *
 * The OpenTelemetry FairLogger sink and the legacy Redis metrics plugin share
 * this value type so FairMQ rate log interpretation stays consistent.
 */
struct FairMQThroughputSample {
    std::string channel_name;
    std::string sub_channel_name;
    std::optional<uint64_t> sub_channel_index;
    double messages_per_second_in = 0.0;
    double megabytes_per_second_in = 0.0;
    double messages_per_second_out = 0.0;
    double megabytes_per_second_out = 0.0;
};

/**
 * @brief Parse FairMQ Device throughput log lines.
 *
 * Expected input:
 * `<channel>: in: <msg/s> (<MB/s> MB) out: <msg/s> (<MB/s> MB)` or
 * `<channel>[<index>]: in: <msg/s> (<MB/s> MB) out: <msg/s> (<MB/s> MB)`.
 *
 * The channel field is trimmed because FairMQ pads it with `std::setw()`.
 */
auto parseFairMQThroughputLog(std::string_view line) -> std::optional<FairMQThroughputSample>;

} // namespace nestdaq::telemetry
