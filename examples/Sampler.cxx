/** @file
 *  @brief Implements the sample message-producing NestDAQ device.
 */

#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <nestdaq/runDevice.h>

#include "Sampler.h"

#if __has_include(<spdlog/spdlog.h>)
#include <spdlog/spdlog.h>
#endif
#if __has_include(<spdlog/spdlog.h>) && __has_include(<nestdaq/telemetry/SpdlogLogger.h>)
#include <nestdaq/telemetry/SpdlogLogger.h>
#elif __has_include(<spdlog/spdlog.h>)
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace bpo = boost::program_options;

void addCustomOptions(bpo::options_description& options)
{
    options.add_options()
           ("out-chan-name", bpo::value<std::string>()->default_value("data"), "Name of output channel")
           ("text", bpo::value<std::string>()->default_value("Hello"), "Text to send out")
           ("max-iterations", bpo::value<std::string>()->default_value("0"), "Maximum number of iterations of Run/ConditionalRun/OnData (0 - infinite)");

}

std::unique_ptr<fair::mq::Device> getDevice(const fair::mq::ProgOptions& /*config*/)
{
    return std::make_unique<Sampler>();
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

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
Sampler::Sampler()
{
    LOG(debug) << "Sampler : hello";
}

//Sampler::~Sampler()
//{
// unsubscribe to property change
//  fConfig->UnsubscribeAsString("Sampler");
//  LOG(debug) << "Sampler : bye";
//}

void Sampler::Init()
{
#if __has_include(<spdlog/spdlog.h>) && __has_include(<nestdaq/telemetry/SpdlogLogger.h>)
    if (!fLogger) {
        fLogger = nestdaq::telemetry::createSpdlogLogger("Sampler");
    }
    fLogger->info("Sampler example spdlog log");
#elif __has_include(<spdlog/spdlog.h>)
    if (!fLogger) {
        if (nestdaq::telemetry::getSpdlogNativeConsoleEnabled()) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern(nestdaq::telemetry::getSpdlogConsolePattern());
            fLogger = std::make_shared<spdlog::logger>(
                          "Sampler",
                          spdlog::sinks_init_list{std::move(console_sink)});
        }
    }
    if (fLogger) {
        fLogger->info("Sampler example spdlog log");
    }
#endif
    // subscribe to property change
//  fConfig->SubscribeAsString("Sampler", [](const std::string& key, std::string value){
//    LOG(debug) << "Sampler (subscribe) : key = " << key << ", value = " << value;
//  });
    printConfig(fConfig, "channel-config", __PRETTY_FUNCTION__);
    printConfig(fConfig, "chans.", __PRETTY_FUNCTION__);
}

void Sampler::InitTask()
{
    printConfig(fConfig, "channel-config", __PRETTY_FUNCTION__);
    printConfig(fConfig, "chans.", __PRETTY_FUNCTION__);

    fId = fConfig->GetProperty<std::string>("id");
    fOutputChannelName = fConfig->GetProperty<std::string>("out-chan-name");
    fText = fConfig->GetProperty<std::string>("text");
    fMaxIterations = std::stoull(fConfig->GetProperty<std::string>("max-iterations"));

    fNumSubChannels = static_cast<int>(GetNumSubChannels(fOutputChannelName));

    // These instruments show the intended producer metrics: successful sends,
    // failed sends, payload size distribution, and run-loop progress.
    auto telemetry = nestdaq::telemetry::getTelemetry();
    fMessagesSent = telemetry.counter("examples.sampler.messages.sent", "{message}", "Messages sent by the Sampler example");
    fMessagesFailed = telemetry.counter("examples.sampler.messages.failed", "{message}", "Messages the Sampler example failed to send");
    fMessageSize = telemetry.histogram("examples.sampler.message.size", "By", "Sampler example message size");
    fIteration = telemetry.gauge("examples.sampler.iteration", "1", "Sampler example iteration number");
}

bool Sampler::ConditionalRun()
{
    for (auto sub_channel_index = 0; sub_channel_index < fNumSubChannels; ++sub_channel_index) {
        auto text = new std::string(fId + "[" + std::to_string(sub_channel_index) + "]:" + fText + " : " + std::to_string(fNumIterations));

        // copy
        auto text_copy = *text;

        fair::mq::MessagePtr msg(NewMessage(
                                     const_cast<char*>(text->data()),
                                     text->length(),
        [](void * /*data*/, void* object) {
            auto payload = static_cast<std::string*>(object);
            //LOG(debug) << " sent " << *payload;
            delete payload; // NOLINT(cppcoreguidelines-owning-memory)
        },
        text
                                 )
                                );

        LOG(info) << "Sending \"" << text_copy << "\"";

        auto span = nestdaq::telemetry::getTelemetry().startSpan("sampler.send",
        {   {"fairmq.channel.name", fOutputChannelName},
            {"fairmq.channel.index", sub_channel_index},
            {"message.size", text->length()}
        });

        if (Send(msg, fOutputChannelName, sub_channel_index) < 0) {
            LOG(warn) << "failed to send. event:  " << fNumIterations << ", sub channel = " << sub_channel_index;
            // Record failures with channel attributes so send-side drops can be
            // separated by FairMQ channel and subchannel.
            fMessagesFailed.add(1, {{"fairmq.channel.name", fOutputChannelName},
                {"fairmq.channel.index", sub_channel_index}
            });
            span.setAttribute({"send.ok", false});
            return false;
        }
        // Record normal send metrics close to the send result to demonstrate
        // how user code attaches operational context to each measurement.
        fMessagesSent.add(1, {{"fairmq.channel.name", fOutputChannelName},
            {"fairmq.channel.index", sub_channel_index}
        });
        fMessageSize.record(text->length(), {{"fairmq.channel.name", fOutputChannelName},
            {"fairmq.channel.index", sub_channel_index}
        });
        span.setAttribute({"send.ok", true});
    }

    ++fNumIterations;
    // The gauge captures the current producer iteration so exported metrics
    // can be correlated with the message stream generated by this example.
    fIteration.record(fNumIterations);
    if (fMaxIterations > 0 && fNumIterations >= fMaxIterations) {
        LOG(info) << "Configured maximum number of iterations reached. Leaving RUNNING state. " << fNumIterations << " / " << fMaxIterations;
        return false;
    }
    LOG(info) << " processed events:  " << fNumIterations;
    return true;
}

void Sampler::PostRun()
{
    LOG(debug) << __FUNCTION__;
    fNumIterations = 0;
}

void Sampler::PreRun()
{
    LOG(debug) << __FUNCTION__;
}

void Sampler::Run()
{
    LOG(debug) << __FUNCTION__;
}
