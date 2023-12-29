#ifndef DaqService_Plugins_TopologyConfig_h
#define DaqService_Plugins_TopologyConfig_h

//#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <sstream>
#include <map>
#include <unordered_set>
#include <vector>

#include <fairmq/Plugin.h>

#include "plugins/TopologyData.h"
#include "plugins/DaqServicePlugin.h"

// forward declaration
namespace sw::redis {
class Redis;
template <typename Impl> class QueuedRedis;
class PipelineImpl;
using Pipeline = QueuedRedis<PipelineImpl>;
}

namespace daq::service {

class TopologyConfig {
public:
    using DeviceState = fair::mq::Plugin::DeviceState;
    explicit TopologyConfig(daq::service::Plugin &plugin);
    TopologyConfig(const TopologyConfig&) = delete;
    TopologyConfig& operator=(const TopologyConfig&) = delete;
    ~TopologyConfig();

    void ConfigConnect();
    void EnableUds(bool f=true) {
        fEnableUds = f;
    }
    void OnDeviceStateChange(DeviceState newState);
    void Reset();
    void ResetTtl(sw::redis::Pipeline& pipe);
    void SetConnectConfig(std::string_view arg) {
        fConnectConfig = arg.data();
    }
    void SetMaxRetryToResolveAddress(int arg) {
        fMaxRetryToResolveAddress = arg;
    }

private:
    void DeleteProperty(const std::string& key) {
        fPlugin.DeleteProperty(key);
    }
    std::shared_ptr<sw::redis::Redis> GetClient() const {
        return fPlugin.GetClient();
    }
    std::mutex& GetMutex() {
        return fPlugin.GetMutex();
    }
    std::map<std::string, std::string> GetPropertiesAsStringStartingWith(const std::string& q) const {
        return fPlugin.GetPropertiesAsStringStartingWith(q);
    }
    template <typename T> T GetProperty(const std::string& key) const {
        return fPlugin.GetProperty<T>(key);
    }
    void Initialize();
    void InitializeDefaultChannelProperties();
    bool IsCanceled() const {
        return fPlugin.IsCanceled();
    }
    bool IsUdsAvailable(const std::vector<std::string> &peers);
    int PropertyExists(const std::string& key) {
        return fPlugin.PropertyExists(key);
    }
    const SocketProperty ReadEndpointProperty(std::string_view key);
    std::unordered_set<std::string> ReadEndpoints();
    const LinkProperty ReadLinkProperty(std::string_view key);
    std::unordered_set<std::string> ReadLinks();
    const std::vector<std::string> ReadPeerAddress(const std::string& peer);
    const std::string ReadPeerIP(const std::string& peer);
    void ResolveConnectAddress();
    void SetProperties(const fair::mq::Properties &props) {
        fPlugin.SetProperties(props);
    }
    void Unregister();
    void WaitBindAddress();
    void WriteAddress(MQChannel &channels, std::function<void (sw::redis::Pipeline&, std::string_view)> f = nullptr);
    void WriteBindAddress();
    void WriteChannel(SocketProperty &sp, const std::vector<std::string> &peers);
    void WriteConnectAddress();

    daq::service::Plugin &fPlugin;
    std::string fServiceName;
    std::string fId;
    std::string fSeparator;
    std::string fTopPrefix;
    long long   fMaxTtl;
    bool        fEnableUds;
    std::string fConnectConfig;
    int         fMaxRetryToResolveAddress;

    // channel properties configured by command line option or JSON
    std::map<std::string, std::string> fDefaultChannelProperties;

    // channel properties configured by this plugin
    std::map<std::string, std::string> fCustomChannelProperties;

    MQChannel fBindChannels;
    MQChannel fConnectChannels;
    std::map<std::string, LinkProperty> fLinks;

    std::vector<std::string> fRegisteredKeys;
};

} // namespace daq::service

#endif