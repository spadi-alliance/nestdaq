#ifndef Example_Sink_h
#define Example_Sink_h

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#if __has_include(<fairmq/Device.h>)
#include <fairmq/Device.h> // since v1.4.34
#else 
#include <fairmq/FairMQDevice.h>
#endif

class Sink : public FairMQDevice {
public:

  struct OptionKey {
    static constexpr std::string_view InputChannelName{"in"};
    static constexpr std::string_view Multipart{"multipart"};
  };

  Sink() = default;
  Sink(const Sink&) = delete;
  Sink &operator=(const Sink&) = delete;
  ~Sink() = default;

private: 
  bool HandleData(FairMQMessagePtr &msg, int index);
  bool HandleMultipartData(FairMQParts &msgParts, int index);
  void Init() override; 
  void InitTask() override; 
  void PostRun() override;

  std::string fInputChannelName;
  uint64_t fNumMessages{0};

};


#endif