#include <chrono>
#include <string_view>
#include <thread>

#include <fairmq/runFairMQDevice.h>

#include "NullDevice.h"

static constexpr std::string_view MyClass{"NullDevice"};

namespace bpo = boost::program_options;

//_____________________________________________________________________________
void addCustomOptions(bpo::options_description &options)
{
}

//_____________________________________________________________________________
FairMQDevicePtr getDevice(const FairMQProgOptions &)
{
    return new NullDevice;
}

//_____________________________________________________________________________
void NullDevice::Bind()
{
    LOG(info) << __PRETTY_FUNCTION__;
}

//_____________________________________________________________________________
bool NullDevice::ConditionalRun()
{
    LOG(info) << __PRETTY_FUNCTION__;
    return true;
}

//_____________________________________________________________________________
void NullDevice::Connect()
{
    LOG(info) << __PRETTY_FUNCTION__;
}

//_____________________________________________________________________________
void NullDevice::Init()
{
    LOG(info) << __PRETTY_FUNCTION__;
}

//_____________________________________________________________________________
void NullDevice::InitTask()
{
    LOG(info) << __PRETTY_FUNCTION__;
}

//_____________________________________________________________________________
void NullDevice::PostRun()
{
    LOG(info) << __PRETTY_FUNCTION__;
}

//_____________________________________________________________________________
void NullDevice::PreRun()
{
    LOG(info) << __PRETTY_FUNCTION__;
}

//_____________________________________________________________________________
void NullDevice::Reset()
{
    LOG(info) << __PRETTY_FUNCTION__;
}

//_____________________________________________________________________________
void NullDevice::ResetTask()
{
    LOG(info) << __PRETTY_FUNCTION__;
}

//_____________________________________________________________________________
void NullDevice::Run()
{
    LOG(info) << __PRETTY_FUNCTION__;
}
