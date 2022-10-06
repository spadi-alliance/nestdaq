#ifndef Examples_NullDevice_h
#define Examples_NullDevice_h

#if __has_include(<fairmq/Device.h>)
#include <fairmq/Device.h> // since v1.4.34
#else 
#include <fairmq/FairMQDevice.h>
#endif

class NullDevice : public FairMQDevice
{
public: 
  NullDevice() = default;
  ~NullDevice() override = default;

protected:
  void Bind() override;
  bool ConditionalRun() override;
  void Connect() override;
  void Init() override;
  void InitTask() override;
  void PostRun() override;
  void PreRun() override;
  void Reset() override;
  void ResetTask() override;
  void Run() override; 

};

#endif