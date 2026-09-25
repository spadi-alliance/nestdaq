/**
 * @file fairmq_throughput_log_parser_tests.cxx
 * @brief Catch2 tests for FairMQ throughput log parsing.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <nestdaq/telemetry/FairMQThroughputLogParser.h>

TEST_CASE("FairMQ throughput parser accepts the Device rate log format", "[telemetry][fairmq]")
{
    const auto kSample = nestdaq::telemetry::parseFairMQThroughputLog(
                            "data: in: 1234.5 (6.75 MB) out: 8.25 (0.5 MB)");

    REQUIRE(kSample.has_value());
    CHECK(kSample->channel_name == "data");
    CHECK(kSample->sub_channel_name == "data");
    CHECK_FALSE(kSample->sub_channel_index.has_value());
    CHECK(kSample->messages_per_second_in == Catch::Approx{1234.5});
    CHECK(kSample->megabytes_per_second_in == Catch::Approx{6.75});
    CHECK(kSample->messages_per_second_out == Catch::Approx{8.25});
    CHECK(kSample->megabytes_per_second_out == Catch::Approx{0.5});
}

TEST_CASE("FairMQ throughput parser trims padded channel names", "[telemetry][fairmq]")
{
    const auto kSample = nestdaq::telemetry::parseFairMQThroughputLog(
                            "       pull: in: 1 (2 MB) out: 3 (4 MB)");

    REQUIRE(kSample.has_value());
    CHECK(kSample->channel_name == "pull");
    CHECK(kSample->sub_channel_name == "pull");
    CHECK_FALSE(kSample->sub_channel_index.has_value());
}

TEST_CASE("FairMQ throughput parser splits indexed subchannels", "[telemetry][fairmq]")
{
    const auto kSample = nestdaq::telemetry::parseFairMQThroughputLog(
                            "       data[12]: in: 1 (2 MB) out: 3 (4 MB)");

    REQUIRE(kSample.has_value());
    CHECK(kSample->channel_name == "data");
    CHECK(kSample->sub_channel_name == "data[12]");
    REQUIRE(kSample->sub_channel_index.has_value());
    CHECK(*kSample->sub_channel_index == 12);
}

TEST_CASE("FairMQ throughput parser accepts exponent notation", "[telemetry][fairmq]")
{
    const auto kSample = nestdaq::telemetry::parseFairMQThroughputLog(
                            "push: in: 1.5e+03 (2.5e-01 MB) out: 0 (0 MB)");

    REQUIRE(kSample.has_value());
    CHECK(kSample->messages_per_second_in == Catch::Approx{1500.0});
    CHECK(kSample->megabytes_per_second_in == Catch::Approx{0.25});
    CHECK(kSample->messages_per_second_out == Catch::Approx{0.0});
    CHECK(kSample->megabytes_per_second_out == Catch::Approx{0.0});
}

TEST_CASE("FairMQ throughput parser rejects unrelated logs", "[telemetry][fairmq]")
{
    CHECK_FALSE(nestdaq::telemetry::parseFairMQThroughputLog("fair::mq::Device running...").has_value());
    CHECK_FALSE(nestdaq::telemetry::parseFairMQThroughputLog("data: in: text (1 MB) out: 2 (3 MB)").has_value());
    CHECK_FALSE(nestdaq::telemetry::parseFairMQThroughputLog(": in: 1 (2 MB) out: 3 (4 MB)").has_value());
    CHECK_FALSE(nestdaq::telemetry::parseFairMQThroughputLog("data[x]: in: 1 (2 MB) out: 3 (4 MB)").has_value());
    CHECK_FALSE(nestdaq::telemetry::parseFairMQThroughputLog("data[]: in: 1 (2 MB) out: 3 (4 MB)").has_value());
}
