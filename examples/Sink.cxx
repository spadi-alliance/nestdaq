/** @file
 *  @brief Implements the sample message-consuming NestDAQ device.
 */

#include <chrono>
#include <memory>
#include <thread>
#include <utility>

#include <nestdaq/runDevice.h>

#include "Sink.h"

#if __has_include(<spdlog/spdlog.h>)
#include <spdlog/spdlog.h>
#endif
#if __has_include(<spdlog/spdlog.h>) && __has_include(<nestdaq/telemetry/SpdlogLogger.h>)
#include <nestdaq/telemetry/SpdlogLogger.h>
#elif __has_include(<spdlog/spdlog.h>)
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

static constexpr std::string_view kMyClass{"Sink"};
static constexpr int kMaxDrainRetries{10};
static constexpr std::chrono::milliseconds kDrainRetryInterval{200};

namespace bpo = boost::program_options;

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
Sink::Sink()
{
}

void addCustomOptions(bpo::options_description &options)
{
    using opt = Sink::OptionKey;
    options.add_options()
           (opt::kInputChannelName, bpo::value<std::string>()->default_value(opt::kInputChannelName), "Name of input channel\n")
           //
           (opt::kMultipart, bpo::value<std::string>()->default_value("true"), "Handle multipart message\n");
}

std::unique_ptr<fair::mq::Device> getDevice(const fair::mq::ProgOptions& /*config*/)
{
    return std::make_unique<Sink>();
}

void printConfig(const fair::mq::ProgOptions* config, std::string_view name, std::string_view function_name)
{
    const auto kPrefix = std::string{name};
    auto properties = config->GetPropertiesAsStringStartingWith(kPrefix);
    std::ostringstream message;
    message << function_name << "\n\t " << name << "\n";
    for (const auto &[key, value] : properties) {
        message << "\t key = " << key << ", value = " << value << "\n";
    }
    LOG(debug) << message.str();
}

bool Sink::handleData(fair::mq::MessagePtr &msg, int index)
{
    auto span = nestdaq::telemetry::getTelemetry().startSpan("sink.receive",
    {   {"fairmq.channel.name", fInputChannelName},
        {"fairmq.channel.index", index},
        {"message.size", msg->GetSize()},
        {"message.multipart", false}
    });
    static_cast<void>(span);
    const auto kPtr = static_cast<char*>(msg->GetData());
    std::string payload(kPtr, msg->GetSize());
    LOG(debug) << __FUNCTION__ << " received = " << payload << " [" << index << "] " << fNumMessages;
    fMessagesReceived.add(1, {{"fairmq.channel.name", fInputChannelName},
        {"fairmq.channel.index", index},
        {"message.multipart", false}
    });
    // These receiver metrics demonstrate counting accepted messages, observing
    // payload sizes, and tracking the current total for a single-part stream.
    fMessageSize.record(msg->GetSize(), {{"fairmq.channel.name", fInputChannelName},
        {"fairmq.channel.index", index},
        {"message.multipart", false}
    });
    ++fNumMessages;
    fMessagesTotal.record(fNumMessages, {{"fairmq.channel.name", fInputChannelName}});
    return true;
}

bool Sink::handleMultipartData(fair::mq::Parts &msg_parts, int index)
{
    auto multipart_span = nestdaq::telemetry::getTelemetry().startSpan("sink.receive.multipart",
    {   {"fairmq.channel.name", fInputChannelName},
        {"fairmq.channel.index", index},
        {"message.multipart", true},
        {"message.parts", msg_parts.Size()}
    });
    static_cast<void>(multipart_span);
    for (const auto& msg : msg_parts) {
        auto part_span = nestdaq::telemetry::getTelemetry().startSpan("sink.receive.part",
        {   {"fairmq.channel.name", fInputChannelName},
            {"fairmq.channel.index", index},
            {"message.size", msg->GetSize()},
            {"message.multipart", true}
        });
        static_cast<void>(part_span);
        const auto kPtr = static_cast<char*>(msg->GetData());
        std::string payload(kPtr, msg->GetSize());
        LOG(debug) << __FUNCTION__ << " received = " << payload << " [" << index << "] " << fNumMessages;
        LOG(debug) << payload;
        fMessagesReceived.add(1, {{"fairmq.channel.name", fInputChannelName},
            {"fairmq.channel.index", index},
            {"message.multipart", true}
        });
        // The multipart path uses the same metric names with attributes that
        // distinguish multipart traffic from single-part traffic.
        fMessageSize.record(msg->GetSize(), {{"fairmq.channel.name", fInputChannelName},
            {"fairmq.channel.index", index},
            {"message.multipart", true}
        });
        ++fNumMessages;
        fMessagesTotal.record(fNumMessages, {{"fairmq.channel.name", fInputChannelName}});
    }
    return true;
}

void Sink::Init()
{
#if __has_include(<spdlog/spdlog.h>) && __has_include(<nestdaq/telemetry/SpdlogLogger.h>)
    if (!fLogger) {
        fLogger = nestdaq::telemetry::createSpdlogLogger("Sink");
    }
    fLogger->info("Sink example spdlog log");
#elif __has_include(<spdlog/spdlog.h>)
    if (!fLogger) {
        if (nestdaq::telemetry::getSpdlogNativeConsoleEnabled()) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern(nestdaq::telemetry::getSpdlogConsolePattern());
            fLogger = std::make_shared<spdlog::logger>(
                          "Sink",
                          spdlog::sinks_init_list{std::move(console_sink)});
        }
    }
    if (fLogger) {
        fLogger->info("Sink example spdlog log");
    }
#endif
    printConfig(fConfig, "channel-config", __PRETTY_FUNCTION__);
    printConfig(fConfig, "chans.", __PRETTY_FUNCTION__);

    fNumMessages = 0;
}

void Sink::InitTask()
{
    printConfig(fConfig, "channel-config", __PRETTY_FUNCTION__);
    printConfig(fConfig, "chans.", __PRETTY_FUNCTION__);

    LOG(debug) << kMyClass << " InitTask";
    using opt = OptionKey;

    fInputChannelName = fConfig->GetProperty<std::string>(opt::kInputChannelName);
    LOG(debug) << " input channel = " << fInputChannelName;

    // These instruments show the intended consumer metrics: received message
    // count, payload size distribution, and current total received messages.
    auto telemetry = nestdaq::telemetry::getTelemetry();
    fMessagesReceived = telemetry.counter("examples.sink.messages.received", "{message}", "Messages received by the Sink example");
    fMessageSize = telemetry.histogram("examples.sink.message.size", "By", "Sink example message size");
    fMessagesTotal = telemetry.gauge("examples.sink.messages.total", "{message}", "Total messages received by the Sink example");

    const auto &is_multipart = fConfig->GetProperty<std::string>(opt::kMultipart);
    if (is_multipart=="true" || is_multipart=="1") {
        LOG(warn) << " set multipart data handler";
        OnData(fInputChannelName, &Sink::handleMultipartData);
    } else {
        LOG(warn) << " set data handler";
        OnData(fInputChannelName, &Sink::handleData);
    }

}

void Sink::PostRun()
{
    using opt = OptionKey;
    LOG(debug) << __func__;
    int receive_timeouts = 0;
    while (true) {
        const auto &is_multipart = fConfig->GetProperty<std::string>(opt::kMultipart);
        if (is_multipart=="true" || is_multipart=="1") {
            fair::mq::Parts parts;
            if (Receive(parts, fInputChannelName) <= 0) {
                LOG(debug) << __func__ << " no data received " << receive_timeouts;
                ++receive_timeouts;
                if (receive_timeouts > kMaxDrainRetries) {
                    break;
                }
                std::this_thread::sleep_for(kDrainRetryInterval);
            } else {
                LOG(debug) << __func__ << " print data";
                handleMultipartData(parts, 0);
            }
        } else {
            fair::mq::MessagePtr msg(NewMessage());
            if (Receive(msg, fInputChannelName) <= 0) {
                LOG(debug) << __func__ << " no data received " << receive_timeouts;
                ++receive_timeouts;
                if (receive_timeouts > kMaxDrainRetries) {
                    break;
                }
                std::this_thread::sleep_for(kDrainRetryInterval);
            } else {
                LOG(debug) << __func__ << " print data";
                handleData(msg, 0);
            }
        }
        LOG(debug) << __func__ << " done";
    }
}
