#ifndef DaqService_Plugins_TopologyData_h
#define DaqService_Plugins_TopologyData_h

#include <map>
#include <string>
#include <vector>

namespace daq::service {

// property of FairMQSocket
struct SocketProperty {
    std::string name; // channel name
    std::string type;
    std::string method; // bind or connect
    std::string address;
    std::string transport{"zeromq"};
    int sndBufSize{1000};
    int rcvBufSize{1000};
    int sndKernelSize{0};
    int rcvKernelSize{0};
    int linger{500};
    int rateLogging{1};
    int portRangeMin{22000};
    int portRangeMax{32000};
    bool autoBind{true};
    int numSockets{0};

    // Following fields are used by TopologyConfig.
    bool autoSubChannel{false};
    bool bound{false};
    bool waitForPeerConnection{true};
};

struct LinkProperty {
    std::string myService; // near
    std::string myChannel;
    std::string peerService; // far
    std::string peerChannel;
    std::string options;
};

using MQChannel = std::map<std::string, SocketProperty>;

} // namespae daq::service

#endif
