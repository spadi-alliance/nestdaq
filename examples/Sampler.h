#ifndef Examples_Sampler_h
#define Exapmles_Sampler_h

#include <string>

#if __has_include(<fairmq/Device.h>)
#include <fairmq/Device.h>  // since v1.4.34
#else 
#include <fairmq/FairMQDevice.h>
#endif

class Sampler : public FairMQDevice
{
  public: 
  Sampler();
  ~Sampler() override = default;

  protected:
    std::string fText;
    uint64_t fMaxIterations;
    uint64_t fNumIterations;

    void Init() override;
    void InitTask() override;
    bool ConditionalRun() override;
    void PostRun() override;
    void PreRun() override;
    void Run() override;

};

#endif