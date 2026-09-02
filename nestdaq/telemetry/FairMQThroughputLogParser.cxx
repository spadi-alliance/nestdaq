/** @file
 *  @brief Parses FairMQ channel throughput log records.
 */

#include "nestdaq/telemetry/FairMQThroughputLogParser.h"

#include "nestdaq/telemetry/Compat.h"

#include <cmath>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <memory>

namespace nestdaq::telemetry {
namespace {

auto startsWith(std::string_view input, std::string_view literal) noexcept -> bool
{
    return input.size() >= literal.size() && input.substr(0, literal.size()) == literal;
}

auto endsWith(std::string_view input, char suffix) noexcept -> bool
{
    return !input.empty() && input.back() == suffix;
}

auto consumeLiteral(std::string_view &input, std::string_view literal) noexcept -> bool
{
    if (!startsWith(input, literal)) {
        return false;
    }
    input.remove_prefix(literal.size());
    return true;
}

auto consumeSpaces(std::string_view &input) noexcept -> void
{
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())) != 0) {
        input.remove_prefix(1);
    }
}

auto parseDoubleToken(std::string_view &input, double &value) -> bool
{
    consumeSpaces(input);
    const auto kTokenEnd = input.find_first_of(" )");
    if (kTokenEnd == 0 || kTokenEnd == std::string_view::npos) {
        return false;
    }

    const auto kToken = input.substr(0, kTokenEnd);
    if (!compat::parseDouble(kToken, value) || value < 0.0) {
        return false;
    }
    input.remove_prefix(kTokenEnd);
    return true;
}

auto trim(std::string_view value) noexcept -> std::string_view
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

auto parseChannel(std::string_view value, FairMQThroughputSample &sample) -> bool
{
    value = trim(value);
    if (value.empty()) {
        return false;
    }

    sample.sub_channel_name = std::string{value};

    if (!endsWith(value, ']')) {
        sample.channel_name = std::string{value};
        return true;
    }

    const auto kOpenBracket = value.rfind('[');
    if (kOpenBracket == std::string_view::npos || kOpenBracket == 0 || kOpenBracket + 1 >= value.size() - 1) {
        return false;
    }

    const auto kChannelName = trim(value.substr(0, kOpenBracket));
    if (kChannelName.empty()) {
        return false;
    }

    const auto kIndexToken = value.substr(kOpenBracket + 1, value.size() - kOpenBracket - 2);
    uint64_t index = 0;
    if (!compat::parseInteger(kIndexToken, index)) {
        return false;
    }

    sample.channel_name = std::string{kChannelName};
    sample.sub_channel_index = index;
    return true;
}

} // namespace

auto parseFairMQThroughputLog(std::string_view line) -> std::optional<FairMQThroughputSample>
{
    line = trim(line);
    const auto kChannelDelimiter = line.find(": in:");
    if (kChannelDelimiter == std::string_view::npos) {
        return std::nullopt;
    }

    auto input = line.substr(kChannelDelimiter + 2);
    auto sample = FairMQThroughputSample{};
    if (!parseChannel(line.substr(0, kChannelDelimiter), sample)) {
        return std::nullopt;
    }

    if (!consumeLiteral(input, "in:") ||
            !parseDoubleToken(input, sample.messages_per_second_in)) {
        return std::nullopt;
    }
    consumeSpaces(input);
    if (!consumeLiteral(input, "(") ||
            !parseDoubleToken(input, sample.megabytes_per_second_in)) {
        return std::nullopt;
    }
    consumeSpaces(input);
    if (!consumeLiteral(input, "MB)")) {
        return std::nullopt;
    }
    consumeSpaces(input);
    if (!consumeLiteral(input, "out:") ||
            !parseDoubleToken(input, sample.messages_per_second_out)) {
        return std::nullopt;
    }
    consumeSpaces(input);
    if (!consumeLiteral(input, "(") ||
            !parseDoubleToken(input, sample.megabytes_per_second_out)) {
        return std::nullopt;
    }
    consumeSpaces(input);
    if (!consumeLiteral(input, "MB)") || !trim(input).empty()) {
        return std::nullopt;
    }

    return sample;
}

} // namespace nestdaq::telemetry
