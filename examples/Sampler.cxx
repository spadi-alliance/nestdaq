#include <sstream>
#include <string>

#include <fairmq/runFairMQDevice.h>

#include "Sampler.h"

namespace bpo = boost::program_options;

//_____________________________________________________________________________
void addCustomOptions(bpo::options_description& options)
{
    options.add_options()
    ("out-chan-name", bpo::value<std::string>()->default_value("data"), "Name of output channel")
    ("text", bpo::value<std::string>()->default_value("Hello"), "Text to send out")
    ("max-iterations", bpo::value<std::string>()->default_value("0"), "Maximum number of iterations of Run/ConditionalRun/OnData (0 - infinite)");

}

//_____________________________________________________________________________
FairMQDevicePtr getDevice(const fair::mq::ProgOptions& /*config*/)
{
    return new Sampler();
}

//_____________________________________________________________________________
void PrintConfig(const fair::mq::ProgOptions* config, std::string_view name, std::string_view funcname)
{
    auto c = config->GetPropertiesAsStringStartingWith(name.data());
    std::ostringstream ss;
    ss << funcname << "\n\t " << name << "\n";
    for (const auto &[k, v] : c) {
        ss << "\t key = " << k << ", value = " << v << "\n";
    }
    LOG(debug) << ss.str();
}

//_____________________________________________________________________________
Sampler::Sampler()
    : fId()
    , fOutputChannelName()
    , fText()
    , fMaxIterations(0)
    , fNumIterations(0)
    , fNumSubChannels(0)
{
    LOG(debug) << "Sampler : hello";
}

//_____________________________________________________________________________
//Sampler::~Sampler()
//{
// unsubscribe to property change
//  fConfig->UnsubscribeAsString("Sampler");
//  LOG(debug) << "Sampler : bye";
//}

//_____________________________________________________________________________
void Sampler::Init()
{
    // subscribe to property change
//  fConfig->SubscribeAsString("Sampler", [](const std::string& key, std::string value){
//    LOG(debug) << "Sampler (subscribe) : key = " << key << ", value = " << value;
//  });
    PrintConfig(fConfig, "channel-config", __PRETTY_FUNCTION__);
    PrintConfig(fConfig, "chans.", __PRETTY_FUNCTION__);
}

//_____________________________________________________________________________
void Sampler::InitTask()
{
    PrintConfig(fConfig, "channel-config", __PRETTY_FUNCTION__);
    PrintConfig(fConfig, "chans.", __PRETTY_FUNCTION__);

    fId = fConfig->GetProperty<std::string>("id");
    fOutputChannelName = fConfig->GetProperty<std::string>("out-chan-name");
    fText = fConfig->GetProperty<std::string>("text");
    fMaxIterations = std::stoull(fConfig->GetProperty<std::string>("max-iterations"));

    fNumSubChannels = GetNumSubChannels(fOutputChannelName);
}

//_____________________________________________________________________________
bool Sampler::ConditionalRun()
{
    for (auto iSubChannel = 0; iSubChannel < fNumSubChannels; ++iSubChannel) {
        auto text = new std::string(fId + "[" + std::to_string(iSubChannel) + "]:" + fText + " : " + std::to_string(fNumIterations));

        // copy
        auto txt = *text;

        FairMQMessagePtr msg(NewMessage(
                                 const_cast<char*>(text->data()),
                                 text->length(),
        [](void * /*data*/, void* object) {
            auto p = reinterpret_cast<std::string*>(object);
            //LOG(debug) << " sent " << *p;
            delete p;
        },
        text
                             )
                            );

        LOG(info) << "Sending \"" << txt << "\"";

        if (Send(msg, fOutputChannelName, iSubChannel) < 0) {
            LOG(warn) << "failed to send. event:  " << fNumIterations << ", sub channel = " << iSubChannel;
            return false;
        }
    }

    ++fNumIterations;
    if (fMaxIterations > 0 && fNumIterations >= fMaxIterations) {
        LOG(info) << "Configured maximum number of iterations reached. Leaving RUNNING state. " << fNumIterations << " / " << fMaxIterations;
        return false;
    }
    LOG(info) << " processed events:  " << fNumIterations;
    return true;
}

//_____________________________________________________________________________
void Sampler::PostRun()
{
    LOG(debug) << __FUNCTION__;
    fNumIterations = 0;
}

//_____________________________________________________________________________
void Sampler::PreRun()
{
    LOG(debug) << __FUNCTION__;
}

//_____________________________________________________________________________
void Sampler::Run()
{
    LOG(debug) << __FUNCTION__;
}
