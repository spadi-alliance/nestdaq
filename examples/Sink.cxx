#include <chrono>
#include <thread>

#include <fairmq/runFairMQDevice.h>

#include "Sink.h"

static constexpr std::string_view MyClass{"Sink"};

namespace bpo = boost::program_options;

//_____________________________________________________________________________
void addCustomOptions(bpo::options_description &options)
{
  using opt = Sink::OptionKey;
  options.add_options()
  (opt::InputChannelName.data(), bpo::value<std::string>()->default_value(opt::InputChannelName.data()), "Name of input channel\n")
  //
  (opt::Multipart.data(), bpo::value<std::string>()->default_value("true"), "Handle multipart message\n");
}

//_____________________________________________________________________________
FairMQDevicePtr getDevice(const FairMQProgOptions &)
{
 return new Sink; 
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
bool Sink::HandleData(FairMQMessagePtr &msg, int index)
{
  const auto ptr = reinterpret_cast<char*>(msg->GetData());
  std::string s(ptr, ptr+msg->GetSize());
  LOG(debug) << __FUNCTION__ << " received = " << s << " [" << index << "] " << fNumMessages;
  ++fNumMessages;
  return true;
}

//_____________________________________________________________________________
bool Sink::HandleMultipartData(FairMQParts &msgParts, int index)
{
  for (const auto& msg : msgParts) {
    const auto ptr = reinterpret_cast<char*>(msg->GetData());
    std::string s(ptr, ptr+msg->GetSize());
    LOG(debug) << __FUNCTION__ << " received = " << s << " [" << index << "] " << fNumMessages;
    LOG(debug) << s;
    ++fNumMessages;
  }
 return true; 
}

//_____________________________________________________________________________
void Sink::Init()
{
  PrintConfig(fConfig, "channel-config", __PRETTY_FUNCTION__);
  PrintConfig(fConfig, "chans.", __PRETTY_FUNCTION__);

  fNumMessages = 0;
}

//_____________________________________________________________________________
void Sink::InitTask()
{
  PrintConfig(fConfig, "channel-config", __PRETTY_FUNCTION__);
  PrintConfig(fConfig, "chans.", __PRETTY_FUNCTION__);

  LOG(debug) << MyClass << " InitTask";
  using opt = OptionKey;  

  fInputChannelName = fConfig->GetProperty<std::string>(opt::InputChannelName.data());
  LOG(debug) << " input channel = " << fInputChannelName;

  const auto &isMultipart = fConfig->GetProperty<std::string>(opt::Multipart.data());
  if (isMultipart=="true" || isMultipart=="1") {
    LOG(warn) << " set multipart data handler";
    OnData(fInputChannelName, &Sink::HandleMultipartData);
  } else {
   LOG(warn) << " set data handler"; 
   OnData(fInputChannelName, &Sink::HandleData); 
  }

}

//_____________________________________________________________________________
void Sink::PostRun()
{
  using opt = OptionKey;  
  LOG(debug) << __func__;
  int nrecv=0;
  while (true) {
    const auto &isMultipart = fConfig->GetProperty<std::string>(opt::Multipart.data());
    if (isMultipart=="true" || isMultipart=="1") {
      FairMQParts parts;
      if (Receive(parts, fInputChannelName) <= 0) {
        LOG(debug) << __func__ << " no data received " << nrecv;
        ++nrecv;
        if (nrecv>10) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      } else {
        LOG(debug) << __func__ << " print data";
        HandleMultipartData(parts, 0);
      }
    } else {
      FairMQMessagePtr msg(NewMessage());
      if (Receive(msg, fInputChannelName) <= 0) {
        LOG(debug) << __func__ << " no data received " << nrecv;
        ++nrecv;
        if (nrecv>10) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      } else {
        LOG(debug) << __func__ << " print data";
        HandleData(msg, 0);
      }
    }
    LOG(debug) << __func__ << " done";
  }
}