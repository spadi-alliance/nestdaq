#pragma once

/**
 * @file Sampler.h
 * @brief Example producer device that sends text payloads through FairMQ.
 */

#include <cstdint>
#include <memory>
#include <string>

#include <fairmq/Device.h>
#include <nestdaq/telemetry/Telemetry.h>

#if __has_include(<spdlog/spdlog.h>)
namespace spdlog {
class logger;
} // namespace spdlog
#endif

class Sampler : public fair::mq::Device
{
public:
    Sampler();
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    Sampler(Sampler&&) = delete;
    Sampler& operator=(Sampler&&) = delete;
    ~Sampler() override = default;

private:
    std::string fId;
    std::string fOutputChannelName;
    std::string fText;
    // Telemetry handles are kept as members to demonstrate sender-side metrics
    // through the NestDAQ facade, without linking this example to the OTel SDK.
    nestdaq::telemetry::Counter fMessagesSent;
    nestdaq::telemetry::Counter fMessagesFailed;
    nestdaq::telemetry::Histogram fMessageSize;
    nestdaq::telemetry::Gauge fIteration;
#if __has_include(<spdlog/spdlog.h>)
    std::shared_ptr<spdlog::logger> fLogger;
#endif
    uint64_t fMaxIterations{0};
    uint64_t fNumIterations{0};
    int fNumSubChannels{0};

    void Init() override;
    void InitTask() override;
    bool ConditionalRun() override;
    void PostRun() override;
    void PreRun() override;
    void Run() override;

};
