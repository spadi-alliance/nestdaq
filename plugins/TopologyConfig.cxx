#include <algorithm>
#include <cassert>
#include <mutex>
#include <regex>
#include <thread>

#include <boost/algorithm/string.hpp>

#include <sw/redis++/redis++.h>

#include <fairmq/JSONParser.h>
#include <fairmq/SuboptParser.h>

#include "plugins/Constants.h"
#include "plugins/Functions.h"
#include "plugins/tools.h"
#include "plugins/TopologyConfig.h"

static constexpr std::string_view MyClass{"daq::service::TopologyConfig"};

namespace topology {
static constexpr std::string_view Prefix{"topology"};
static constexpr std::string_view EndpointPrefix{"endpoint"};
static constexpr std::string_view LinkPrefix{"link"};

static constexpr std::string_view ChannelPrefix{"channel"};
static constexpr std::string_view PeerPrefix{"peer"};
static constexpr std::string_view SocketPrefix{"socket"};
}

using namespace std::string_literals;
using namespace std::chrono_literals;

//_____________________________________________________________________________
void PrintConfig(const std::map<std::string, std::string> &p, std::string_view name)
{
    std::ostringstream ss;
    ss << " name = " << name << "\n";
    for (const auto &[k, v] : p) {
        ss << " key = " << k << ", value = " << v << "\n";
    }
    LOG(debug) << ss.str();
}

//_____________________________________________________________________________
std::string MakeAddress(const std::string &address, std::string_view peerIP)
{
    // e.g. address = tcp://AAAA:XXXX
    auto posPort = address.find_last_of(":");
    auto posStar = address.find("*");
    auto pos0000 = address.find("0.0.0.0");
    if (address.find("tcp://")==0) {
        if ((posStar!=std::string::npos) || (pos0000!=std::string::npos)) {
            return address.substr(0, 6) + peerIP.data() + address.substr(posPort);
        }
    }
    return address;
}

//_____________________________________________________________________________
// convert a socket property to format of command line option of FairMQ
const std::string ToChannelConfig(const daq::service::SocketProperty& p)
{
    using namespace std::string_literals;
    std::string ret;

//  LOG(debug) << __FUNCTION__ << " address = " << p.address.size() << " " << p.address;
    std::string address;
    if (!p.address.empty() && p.address!="unspecified" && p.address.find(",")==std::string::npos) {
        address = p.address;
    } else {

        if (address.empty() || address=="unspecified") {
            address="unspecified";
            for (auto i=0; i<p.numSockets-1; ++i) {
                address += ",address=unspecified";
            }
        }
    }

    if (p.address.find(",")!=std::string::npos) {
        std::vector<std::string> res;
        boost::split(res, p.address, boost::is_any_of(","));
        if (res.size()<p.numSockets) {
            auto n = p.numSockets - res.size();
            for (auto i=0u; i<n; ++i) {
                res.push_back("unspecified");
            }
        }
        address = boost::join(res, ",address=");
    }

    // Only FairMQ's supported parameters are allowed for channel-config
    ret = "name="s           + p.name                          //
          + ",type="s          + p.type                          //
          + ",method="s        + p.method                        //
          + ",address="s       + address                         //
          + ",transport="s     + p.transport                     //
          + ",rcvBufSize="s    + std::to_string(p.rcvBufSize)    //
          + ",sndBufSize="s    + std::to_string(p.sndBufSize)    //
          + ",rcvKernelSize="s + std::to_string(p.rcvKernelSize) //
          + ",sndKernelSize="s + std::to_string(p.sndKernelSize) //
          + ",linger="s        + std::to_string(p.linger)        //
          + ",rateLogging="s   + std::to_string(p.rateLogging)   //
          + ",portRangeMin="s  + std::to_string(p.portRangeMin)  //
          + ",portRangeMax="s  + std::to_string(p.portRangeMax)  //
          + ",autoBind="s      + std::to_string(p.autoBind);      //

    LOG(debug) << __FUNCTION__ << " ret = " << ret;
    return ret;

}

//_____________________________________________________________________________
// convert hash in redis to struct
template <typename Container>
const daq::service::SocketProperty ToSocketProperty(const Container& c)
{
    daq::service::SocketProperty sp;
    for (const auto &[field, value] : c) {
        //ss << ", " << field << " = " << value;
        if (field=="name") {
            sp.name = value;
        } else if (field=="type") {
            sp.type = value;
        } else if (field=="method") {
            sp.method = value;
        } else if (field=="address") {
            sp.address = value;
        } else if (field=="transport") {
            sp.transport = value;
        } else if (field=="sndBufSize") {
            sp.sndBufSize = std::stoi(value);
        } else if (field=="rcvBufSize") {
            sp.rcvBufSize = std::stoi(value);
        } else if (field=="sndKernelSize") {
            sp.sndKernelSize = std::stoi(value);
        } else if (field=="rcvKernelSize") {
            sp.rcvKernelSize = std::stoi(value);
        } else if (field=="linger") {
            sp.linger = std::stoi(value);
        } else if (field=="rateLogging") {
            sp.rateLogging = std::stoi(value);
        } else if (field=="portRangeMin") {
            sp.portRangeMin = std::stoi(value);
        } else if (field=="portRangeMax") {
            sp.portRangeMax = std::stoi(value);
        } else if (field=="autoBind") {
            const auto& v = boost::to_lower_copy(value);
            sp.autoBind = (v=="1") || (v=="true");
        } else if (field=="numSockets") {
            sp.numSockets = std::stoi(value);
        } else if (field=="autoSubChannel") {
            const auto& v = boost::to_lower_copy(value);
            sp.autoSubChannel = (v=="1") || (v=="true");
        } else if (field=="bound") {
            const auto& v = boost::to_lower_copy(value);
            sp.bound = (v=="1") || (v=="true");
        }
    }
//  if (sp.autoSubChannel) {
//    sp.numSockets = 0;
//  }
    return sp;
}

//_____________________________________________________________________________
daq::service::TopologyConfig::TopologyConfig(daq::service::Plugin& plugin)
    : fPlugin(plugin)
{
    try {
        fTopPrefix   = GetProperty<std::string>("top-prefix");
        fServiceName = GetProperty<std::string>(ServiceName.data());
        fId          = GetProperty<std::string>("id");
        fSeparator   = GetProperty<std::string>(Separator.data());
        fMaxTtl      = GetProperty<long long>(MaxTtl.data());

        LOG(debug) << MyClass
                   << " top prefix = " << fTopPrefix
                   << "\n service = " << fServiceName
                   << "\n id = " << fId
                   << "\n separator = " << fSeparator
                   << "\n max ttl = " << fMaxTtl;
    } catch (const std::exception &e) {
        LOG(error) << " exception in " << MyClass << ":" << __LINE__ << " e.what() = " << e.what();
    } catch (...) {
        LOG(error) << " exception in " << MyClass << ":" << __LINE__ << " unknown";
    }
}

//_____________________________________________________________________________
daq::service::TopologyConfig::~TopologyConfig()
{
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::ConfigConnect()
{
    auto findPeerIP = [this](const auto& service, const auto& id) {
        const auto &idFull = join({service, id}, fSeparator);
        const auto& peerHealthKey = join({fTopPrefix, idFull,  HealthPrefix.data()}, fSeparator);
        auto  peerIP = GetClient()->hget(peerHealthKey, "hostIp");
        if (!peerIP) {
            LOG(warn) << " id = " << idFull << " : hostIp not found";
            return ""s;
        } else {
            LOG(warn) << " id = " << idFull << " : hostIp found " << *peerIP;
        }
        return *peerIP;
    };

    auto findAddress = [this, findPeerIP](const auto& service, const auto& id, const auto& channel, const auto& subChannelIndex) {
        const auto &peerIP = findPeerIP(service, id);
        if (peerIP.empty()) {
            return ""s;
        }

        const auto& chFull = join({service, id, topology::SocketPrefix.data(), "chans."s+channel+"."s+subChannelIndex}, fSeparator);
        std::string key = join({fTopPrefix, chFull}, fSeparator);
        // check whether peer address exists
        std::string address;
        int nRetry = 0;
        while (true) {
            auto a = GetClient()->hget(key, "address"s);
            if (a) {
                LOG(warn) << " ch = " << chFull << " : address found " << *a;
                address = *a;
                break;
            }
            LOG(warn) << " ch = " << chFull << " : address not found";
            if (IsCanceled() || nRetry>fMaxRetryToResolveAddress) {
                LOG(warn) << " find address of peer channel = " << chFull << " -> canceled";
                return ""s;
            }
            std::this_thread::sleep_for(1000ms);
            ++nRetry;
        }
        return MakeAddress(address, peerIP);
    };

    auto findAddresses = [this, findPeerIP](const auto& service, const auto& id, const auto& channel) {
        std::vector<std::string> ret;
        const auto &peerIP = findPeerIP(service, id);
        if (peerIP.empty()) {
            return ret;
        }

        const auto &k = join({fTopPrefix, service, id, topology::SocketPrefix.data(), channel}, fSeparator);
        const auto &socketKeys = scan(*GetClient(), k);
        for (const auto &k : socketKeys) {
            int nRetry = 0;
            while (true) {
                auto a = GetClient()->hget(k, "address");
                if (a) {
                    LOG(warn) << " ch = " << k << " : address found " << *a;
                    ret.push_back(MakeAddress(*a, peerIP));
                    break;
                }
                LOG(warn) << " ch = " << k << " : address not found";
                if (IsCanceled() || nRetry>fMaxRetryToResolveAddress) {
                    LOG(warn) << " find address of peer channel = " << k << " -> canceled";
                    break;
                }
                std::this_thread::sleep_for(1000ms);
                ++nRetry;
            }
        }
        return ret;
    };

    //LOG(info) << "connect-config = " <<  fConnectConfig;
    const auto& pt = to_json(fConnectConfig);

    //LOG(info) << " connect-config (JSON) = " << to_string(pt);
    std::vector<std::string> channelConfigOptions;
    for (const auto& child : pt) {
        // child.first is string
        //LOG(info) << " channel name = " << child.first;
        auto myChannelName = child.first;

        auto &sp = fConnectChannels[myChannelName];

        const auto &peer = child.second.get_child("peer");
        std::vector<std::string> peerList;
        if (const auto &s = peer.get_value<std::string>(); !s.empty()) {
            // string
            //LOG(info) << " peer : s = " << s;
            peerList.push_back(s);
        } else {
            // array
            for (const auto &a : peer) {
                const auto &ss = a.second.get_value<std::string>();
                //LOG(info) << " peer (array) : " << ss;
                peerList.push_back(ss);
            }
        }

        std::vector<std::string> addressList;
        for (const auto &p : peerList) {

            int nSeparators = std::count(p.begin(), p.end(), fSeparator[0]);
            bool hasSubChannelIndex = (p.find("[") != std::string::npos);
            if (nSeparators==2) {
                if (hasSubChannelIndex) {
                    // try to match:  "service" : "instance" - "index" : "channel" ["sub_channel_index"]
                    std::regex pattern{"(\\w+)" + fSeparator + "(\\w+)-(\\d+)" + fSeparator + "(\\w+)\\[(\\d+)\\]"};
                    int nMarks = pattern.mark_count();
                    std::smatch matchResults;
                    std::regex_match(p, matchResults, pattern);
                    if (!matchResults.ready() || matchResults.size()!=(nMarks+1)) {
                        LOG(warn) << " failed to match.  \"service\"" + fSeparator + "\"instance\"-\"index\"" + fSeparator + "\"channel\"[\"subChannelIndex\"]";
                        continue;
                    }
                    const auto& service         = matchResults[1].str();
                    const auto& id              = matchResults[2].str() + "-"s + matchResults[3].str();
                    const auto& channel         = matchResults[4].str();
                    const auto& subChannelIndex = matchResults[5].str();

                    const auto& a = findAddress(service, id, channel, subChannelIndex);
                    if (a.empty()) {
                        continue;
                    }
                    addressList.push_back(a);
                } else {
                    // try to match: "service" : "instance" - "index" : "channel"
                    std::regex pattern{"(\\w+)" + fSeparator + "(\\w+)-(\\d+)" + fSeparator +  "(\\w+)"};
                    int nMarks = pattern.mark_count();
                    std::smatch matchResults;
                    std::regex_match(p, matchResults, pattern);
                    if (!matchResults.ready() || matchResults.size()!=(nMarks+1)) {
                        LOG(warn) << " failed to match.  \"service\"" + fSeparator + "\"instance\"-\"index\"" + fSeparator + "\"channel\"";
                        continue;
                    }
                    const auto& service = matchResults[1].str();
                    const auto& id      = matchResults[2].str() + "-"s + matchResults[3].str();
                    const auto& channel = matchResults[4].str();

                    if (!sp.autoSubChannel) {
                        // infer subChannelIndex = 0
                        const auto& a = findAddress(service, id, channel, "0"s);
                        if (a.empty()) {
                            continue;
                        }
                        addressList.push_back(a);
                    } else {
                        // get subChannelIndex (and full key name) from the database
                        const auto &addresses = findAddresses(service, id, channel);
                        addressList.insert(addressList.end(), addresses.begin(), addresses.end());

                    }
                }

            } else if (nSeparators==1) {
                if (hasSubChannelIndex) {
                    std::string service;
                    std::string id;
                    std::string channel;
                    std::string subChannelIndex;

                    // try to match: "instance" - "index" : "channel" ["sub_channel_index"]
                    std::regex pattern{"(\\w+)-(\\d+)" + fSeparator + "(\\w+)\\[(\\d+)\\]"};
                    int nMarks = pattern.mark_count();
                    std::smatch matchResults;
                    std::regex_match(p, matchResults, pattern);

                    if (matchResults.ready() && matchResults.size()==(nMarks+1)) {
                        const auto &instance = matchResults[1].str();
                        const auto &index    = matchResults[2].str();
                        channel              = matchResults[3].str();
                        subChannelIndex      = matchResults[4].str();

                        // infer service name from instance name
                        service = instance;
                        id      = instance + "-"s + index;
                    } else {
                        //LOG(warn) << " failed to match. \"instance\"-\"index\"" + fSeparator + "\"channel\"[\"subChannelIndex\"]";

                        // try to match: "service" : "channel" ["sub_channel_index"]
                        pattern = "(\\w+)" + fSeparator + "(\\w+)\\[(\\d+)\\]";
                        nMarks = pattern.mark_count();
                        std::regex_match(p,  matchResults, pattern);
                        if (!matchResults.ready() || matchResults.size()!=(nMarks+1)) {
                            LOG(warn) << " failed to match. \"service\"" + fSeparator + "\"channel\"[\"subChannelIndex\"]";
                            continue;
                        }

                        service         = matchResults[1].str();
                        channel         = matchResults[2].str();
                        subChannelIndex = matchResults[3].str();

                        // infer instance id from service name
                        id = service + "-0"s;
                    }

                    const auto &a = findAddress(service, id, channel, subChannelIndex);
                    if (a.empty()) {
                        continue;
                    }
                    addressList.push_back(a);
                } else {
                    std::string service;
                    std::string id;
                    std::string channel;

                    // try to match:  "instance" - "index" : "channel"
                    std::regex pattern{"(\\w+)-(\\d+)" + fSeparator + "(\\w+)"};
                    int nMarks = pattern.mark_count();
                    std::smatch matchResults;
                    std::regex_match(p, matchResults, pattern);
                    if (matchResults.ready() && matchResults.size()==(nMarks+1)) {
                        const auto &instance = matchResults[1].str();
                        const auto &index    = matchResults[2].str();
                        channel              = matchResults[3].str();

                        // infer service name
                        service = instance;
                        id      = instance + "-"s + index;
                    } else {
                        //LOG(warn) << " failed to match. \"instance\"-\"index\"" + fSeparator + "\"channel\"";

                        // try to match: "service" : "channel"
                        pattern = "(\\w+)" + fSeparator + "(\\w+)";
                        nMarks = pattern.mark_count();
                        std::regex_match(p,  matchResults, pattern);
                        if (!matchResults.ready() || matchResults.size()!=(nMarks+1)) {
                            LOG(warn) << " failed to match. \"service\"" + fSeparator + "\"channel\"";
                            continue;
                        }

                        service = matchResults[1].str();
                        channel = matchResults[2].str();

                        // infer instance id from service name
                        id = service + "-0"s;
                    }

                    if (!sp.autoSubChannel) {
                        // infer subChannelIndex = 0
                        const auto &a = findAddress(service, id, channel, "0"s);
                        if (a.empty()) {
                            continue;
                        }
                        addressList.push_back(a);
                    } else {
                        // get subChannelIndex (and full key name) from the database
                        const auto &addresses = findAddresses(service, id, channel);
                        addressList.insert(addressList.end(), addresses.begin(), addresses.end());
                    }
                }

            }
        }

        for (const auto& address : addressList) {
            if (!address.empty()) {
                if (sp.address.empty()) {
                    sp.address = address;

                } else {
                    sp.address += ","s + address;
                }
            }
        }

        channelConfigOptions.emplace_back(ToChannelConfig(sp));

    }

    if (channelConfigOptions.empty()) {
        LOG(info) << __FUNCTION__ << " done (empty)";
        return;
    }

    for (const auto &s : channelConfigOptions) {
        LOG(info) << " channel config option = " << s;
    }

    try {
        auto properties = fair::mq::SuboptParser(channelConfigOptions, fServiceName);
        for (const auto & [k, v] : properties) {

            const auto s = fair::mq::PropertyHelper::ConvertPropertyToString(v);
            fCustomChannelProperties[k] = s;
            LOG(debug) << " id = " << fId << " set property : " << k << " " << s;
        }

        SetProperties(properties);
    } catch (const std::exception& e) {
        LOG(error) << MyClass << " error on SetProperty(chans.) : id = " << fId << ": " << e.what();
    } catch (...) {
        LOG(error) << MyClass << " unknown exception on SetProperty(chans.) :";
    }
    LOG(debug) << __FUNCTION__ << " done";
    //LOG(debug) << " after update";
    //PrintConfig(GetPropertiesAsStringStartingWith("channel-config"), "channel-config");
    //PrintConfig(GetPropertiesAsStringStartingWith("chans."), "chans.");

}

//_____________________________________________________________________________
void daq::service::TopologyConfig::Initialize()
{
//  fNSubscribed = -1;
    if (fDefaultChannelProperties.empty()) {
        InitializeDefaultChannelProperties();
    }
    if (!fConnectConfig.empty()) {
        LOG(info) << "connect-config = " <<  fConnectConfig;
        const auto& pt = to_json(fConnectConfig);

        LOG(info) << " connect-config (JSON) = " << to_string(pt);
        for (const auto& child : pt) {
            // child.first is string
            //LOG(info) << " channel name = " << child.first;
            const auto myChannelName = child.first;

            std::unordered_map<std::string, std::string> cont;
            for (const auto &k : {
                        "type", "transport", "sndBufSize", "rcvBufSize", "sndKernelSize", "rcvKernelSize", "linger", "rateLogging", "numSockets", "autoSubChannel"
                    }) {
                if (const auto &v = child.second.get_optional<std::string>(k); v) {
                    cont[k] = *v;
                }
            }
            auto sp = ToSocketProperty(cont);
            sp.name = myChannelName;
            sp.method = "connect"s;
            fConnectChannels.emplace(sp.name, sp);
        }
    }

    auto endpoints = ReadEndpoints();

    for (const auto& k : endpoints) {
        const auto sp = ReadEndpointProperty(k);
        if (sp.method=="bind") {
            fBindChannels.emplace(sp.name, sp);
        } else if (sp.method=="connect") {
            fConnectChannels.emplace(sp.name, sp);
        } else {
            LOG(error) << "MQ channel name = " << sp.name <<  ": unknown method = " << (sp.method.empty() ? "(empty)" : sp.method);
        }
    }

    auto links = ReadLinks();
    for (const auto& k : links) {
        const auto lp = ReadLinkProperty(k);
        const auto kk = lp.myService + fSeparator + lp.myChannel + "," + lp.peerService + fSeparator + lp.peerChannel;
        LOG(debug) << " link = " << kk;
        if (fLinks.count(kk)) {
            fLinks[kk].options += "," + lp.options;
        } else {
            fLinks[kk] = lp;
        }
    }

    std::vector<SocketProperty*> channelList;
    for (auto &[k, v] : fBindChannels) {
        channelList.push_back(&v);
    }
    for (auto &[k, v] : fConnectChannels) {
        channelList.push_back(&v);
    }

    LOG(debug) << MyClass << " " << __FUNCTION__ << " number of channels : bind = " //
               << fBindChannels.size() << ", connect = " << fConnectChannels.size();
    std::vector<std::string> channelConfigOptions;
    for (auto p : channelList) {
        auto &sp = *p;
        std::vector<std::string> peers;
        // check number of peer instances
        for (const auto& [pairName, l] : fLinks) {
            LOG(warn) << __FILE__ << ":" << __LINE__ << "\n"
                      << pairName << " " << l.myService << ":" << l.myChannel << " " << l.peerService << ":" << l.peerChannel
                      << " " << sp.name;
            if ((l.myService!=l.peerService) && (l.myChannel!=sp.name)) {
                continue;
            }
            auto useL = ((l.myService==l.peerService) && (l.peerChannel==sp.name));
            const auto &peerService = (useL) ? l.myService : l.peerService;
            const auto &peerChannel = (useL) ? l.myChannel : l.peerChannel;
            // scan keys by a pattern = "daq_servie:service:*:presence"
            const auto &keys = scan(*GetClient(), {fTopPrefix, peerService, "*", PresencePrefix.data()}, fSeparator);
            LOG(debug) << MyClass << " " << __FUNCTION__ << " scan-service : peer name = " << peerService << ", n peers " << keys.size();
            for (const auto &a: keys) {
                auto k = a.substr(0, a.find_last_of(fSeparator));
                k = join({k, topology::ChannelPrefix.data(), peerChannel}, fSeparator);
                LOG(debug) << " " << k;
                peers.push_back(k);
            }
            if (sp.autoSubChannel) {
                sp.numSockets += keys.size();
            }
        }
        std::sort(peers.begin(), peers.end());
        peers.erase(std::unique(peers.begin(), peers.end()), peers.end());

        LOG(debug) << " channel = " << sp.name << " autoSubChannel set numSockets = " << sp.numSockets;

        if (IsUdsAvailable(peers) && fEnableUds && (sp.method=="bind") && (sp.transport=="zeromq")) {
            sp.address += "ipc://@/tmp/nestdaq/"s + "/" + fServiceName + "/" + fId + "/" + sp.name + "[0]";
            for (auto i=1; i<sp.numSockets; ++i) {
                sp.address += ",ipc://@/tmp/nestdaq/"s + "/" + fServiceName + "/" + fId + "/" + sp.name + "[" + std::to_string(i) + "]";
            }
            //LOG(debug4) << " uds address =  " << sp.address;
        }
        channelConfigOptions.emplace_back(ToChannelConfig(sp));

        WriteChannel(sp, peers);
    }

    try {
        auto properties = fair::mq::SuboptParser(channelConfigOptions, fServiceName);
        for (auto it = properties.begin(); it!=properties.end();) {
            if (fDefaultChannelProperties.count(it->first)>0) {
                it = properties.erase(it);
            } else {
                fCustomChannelProperties[it->first] = fair::mq::PropertyHelper::ConvertPropertyToString(it->second);
                ++it;
            }
        }
        SetProperties(properties);
    } catch (const std::exception& e) {
        LOG(error) << MyClass << " error on SetProperty(chans.) : " << e.what();
    } catch (...) {
        LOG(error) << MyClass << " unknown exception on SetProperty(chans.) :";
    }

    LOG(debug) << MyClass << " Initialize() done";
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::InitializeDefaultChannelProperties()
{
    //PrintConfig(GetPropertiesAsStringStartingWith("channel-config"), "(default) channel-config"); // available in InitializingDevice
    //PrintConfig(GetPropertiesAsStringStartingWith("mq-config"), "(default) mq-config"); // available in InitializingDevice
    std::string idForParser;
    if (PropertyExists("config-key")) {
        idForParser = GetProperty<std::string>("config-key");
    } else if (PropertyExists("id")) {
        idForParser = GetProperty<std::string>("id");
    }

    if (!idForParser.empty()) {
        try {
            if (PropertyExists("mq-config")) {
                auto properties = fair::mq::JSONParser(GetProperty<std::string>("mq-config"), idForParser);
                for (auto &[k, v] : properties) {
                    fDefaultChannelProperties[k] = fair::mq::PropertyHelper::ConvertPropertyToString(v);
                }
            } else if (PropertyExists("channel-config")) {
                auto properties = fair::mq::SuboptParser(GetProperty<std::vector<std::string>>("channel-config"), idForParser);
                for (auto &[k, v] : properties) {
                    LOG(debug) << " property name = " << k;
                    fDefaultChannelProperties[k] = fair::mq::PropertyHelper::ConvertPropertyToString(v);
                }
            }
        } catch (const std::exception& e) {
            LOG(error) << MyClass << " " << __FUNCTION__ << " : " << e.what();
        } catch (...) {
            LOG(error) << MyClass << " " << __FUNCTION__ << " : unknown exception";
        }
    }
    // PrintConfig(fDefaultChannelProperties, "(default) chans.");
}

//_____________________________________________________________________________
bool daq::service::TopologyConfig::IsUdsAvailable(const std::vector<std::string> &peers)
{
    const auto& myIP = fPlugin.GetHealth().ipAddress;
    for (const auto& x : peers) {
        const auto& ip = ReadPeerIP(x);
        if (myIP!=ip) {
            LOG(debug4) << __func__ << " different ip: me =  " << myIP << ", peer = " << ip;
            return false;
        }
    }
    LOG(debug4) << __func__ << " all IP is same";
    return true;
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::OnDeviceStateChange(DeviceState newState)
{
    try {
        switch (newState) {
        case DeviceState::InitializingDevice:
            Initialize();
            break;
        case DeviceState::Bound:
            WriteBindAddress();
            if (IsCanceled()) break;
            WaitBindAddress();
            if (IsCanceled()) break;
            if (!fConnectConfig.empty()) {
                ConfigConnect();
            } else {
                ResolveConnectAddress();
            }
            if (IsCanceled()) break;
            WriteConnectAddress();
            break;
        case DeviceState::ResettingDevice:
            Reset();
            break;
        default:
            break;
        }
    } catch (const std::exception &e) {
        LOG(error) << MyClass << " exception during device state change: " << e.what();
    } catch (...) {
        LOG(error) << MyClass << " exception during device state change: unknow exception";
    }
}

//_____________________________________________________________________________
const daq::service::SocketProperty daq::service::TopologyConfig::ReadEndpointProperty(std::string_view key)
{
    const auto& prefix = join({fTopPrefix, topology::Prefix.data(), topology::EndpointPrefix.data(), fServiceName, ""}, fSeparator);
    LOG(debug) << __FUNCTION__ << " prefix = " << prefix;
    const auto& channelName = key.substr(prefix.size());
    std::unordered_map<std::string, std::string> h;
    GetClient()->hgetall(key, std::inserter(h, h.begin()));
    // std::ostringstream ss;
    // ss << " name = " << channelName;
    SocketProperty sp = ToSocketProperty(h);
    sp.name = channelName;
    return sp;
}

//_____________________________________________________________________________
std::unordered_set<std::string> daq::service::TopologyConfig::ReadEndpoints()
{
    // scan keys by a pattern = "daq_service:topology:endpoint:service:*"
    auto keys = scan(*GetClient(), {fTopPrefix, topology::Prefix.data(), topology::EndpointPrefix.data(), fServiceName, "*"}, fSeparator);

    auto n = keys.size();
    std::ostringstream ss;
    ss << fServiceName << ":" << __func__ << " n keys = " << n << ",";
    if (n>0) {
        for (const auto& a : keys) {
            ss << " " << a;
        }
        LOG(debug) << ss.str();
    } else {
        LOG(warn) << fServiceName << ":" << __func__ << " no endpoint entiries";
    }

    return keys;
}

//_____________________________________________________________________________
const daq::service::LinkProperty daq::service::TopologyConfig::ReadLinkProperty(std::string_view key)
{
    // key = ...:link:service0:channel0,service1:channel1

    auto val = GetClient()->get(key);

    const auto& prefix = join({fTopPrefix, topology::Prefix.data(), topology::LinkPrefix.data(), ""}, fSeparator);
    // LOG(debug) << " ReadLinkProperty prefix = " << prefix;

    // socketPairName = service0:channel0,service1:channel1
    const auto& socketPairName = key.substr(prefix.size());
    std::ostringstream ss;
    ss << " link = " << socketPairName;
    LinkProperty lp;
    const auto comma     = socketPairName.find_first_of(",");
    const auto firstSep  = socketPairName.find_last_of(fSeparator, comma);
    const auto secondSep = socketPairName.find_last_of(fSeparator);
    //  LOG(debug) << " 1st sep = " << firstSep << ", comma = " << comma << ", 2nd sep = " << secondSep;
    const auto &serviceL = socketPairName.substr(0, firstSep);
    const auto &channelL = socketPairName.substr(firstSep+1, comma-(firstSep+1));
    const auto &serviceR = socketPairName.substr(comma+1, secondSep-(comma+1));
    const auto &channelR = socketPairName.substr(secondSep+1);

    // LOG(debug) << " LinkProperty parse result = " << serviceL << " " << channelL << " " << serviceR << " " << channelR;

    if (serviceL==serviceR) {
        lp.myService   = serviceL;
        lp.peerService = serviceR;
        if (channelL < channelR) {
            lp.myChannel   = channelL;
            lp.peerChannel = channelR;
        } else {
            lp.myChannel   = channelR;
            lp.peerChannel = channelL;
        }
    }

    if (serviceL == fServiceName) {
        lp.myService   = serviceL;
        lp.myChannel   = channelL;
        lp.peerService = serviceR;
        lp.peerChannel = channelR;
    } else {
        lp.myService   = serviceR;
        lp.myChannel   = channelR;
        lp.peerService = serviceL;
        lp.peerChannel = channelL;
    }
    lp.options = *val;

    return lp;
}

//_____________________________________________________________________________
std::unordered_set<std::string> daq::service::TopologyConfig::ReadLinks()
{
    auto &r         = *GetClient();
    // scan keys by a pattern = "daq_service:topology:link:service:*,*:*"
    auto keys = scan(r, {fTopPrefix, topology::Prefix.data(), topology::LinkPrefix.data(), fServiceName + "*,*", "*"}, fSeparator);

    // scan keys by a pattern = "daq_service:topology:link:*:*,service:*"
    keys.merge(scan(r, {fTopPrefix, topology::Prefix.data(), topology::LinkPrefix.data(), "*", "*,"+fServiceName, "*"}, fSeparator));

    auto n = keys.size();
    std::ostringstream ss;
    ss << fServiceName << ":" << __func__ << " n keys = " << n << ",";
    if (n>0) {
        for (const auto& a : keys) {
            ss << " " << a;
        }
        LOG(debug) << ss.str();
    } else {
        LOG(warn) << fServiceName << ":" << __func__ << " no link entries";
    }

    return keys;
}

//_____________________________________________________________________________
const std::vector<std::string> daq::service::TopologyConfig::ReadPeerAddress(const std::string& peer)
{
    const auto &peerInstanceKey = peer.substr(0, peer.find(fSeparator+topology::ChannelPrefix.data()));
    const auto &peerHealthKey   = join({peerInstanceKey, HealthPrefix.data()}, fSeparator);
    const auto &peerChannel     = peer.substr(peer.find_last_of(fSeparator)+1);
    auto &r = *GetClient();
    LOG(debug) << "peerInstanceKey = " << peerInstanceKey << ", peerHealthKey =  " << peerHealthKey << ", peerChannel = " << peerChannel;
    auto peerIP = r.hget(peerHealthKey, "hostIp");
    LOG(debug) << "id = " << fId << " peer health = " << peerHealthKey;
    if (!peerIP) {
        LOG(warn) << "id = " << fId << " hostIp not found";
    } else {
        LOG(warn) << "id = " << fId << " hostIp found " << peerIP.value();
    }

    auto scanPattern = join({peerInstanceKey.data(), topology::SocketPrefix.data(), "chans."s + peerChannel.data() + ".*"s}, fSeparator);
    LOG(debug) << MyClass << " " << __FUNCTION__ << " id = " << fId<<  " scanPattern = " << scanPattern;
    auto subSocketKeys = scan(r, scanPattern);
    LOG(debug) << MyClass << " " << __FUNCTION__ << " id = " << fId << " subSokectKeys = " << subSocketKeys.size();
    std::set<std::string> sorted(subSocketKeys.cbegin(), subSocketKeys.cend());

    std::vector<std::string> ret;
    for (const auto &k : sorted) {
        LOG(debug) << MyClass << " " << __FUNCTION__ << " id = " << fId << " k = " << k;
        std::string address;
        int nRetry = 0;
        while (true) {
            auto a = GetClient()->hget(k, "address");
            if (a) {
                address = MakeAddress(*a, peerIP->data());
                break;
            }
            LOG(warn) << " address not found for " << k;
            if (IsCanceled() || nRetry>fMaxRetryToResolveAddress) {
                LOG(warn) << " find address of peer channel = " << k << " -> canceled";
                break;
            }
            std::this_thread::sleep_for(1000ms);
            ++nRetry;
        }
        LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " id = " << fId << " address = " << address;

        ret.push_back(address);
    }
    return ret;
}

//_____________________________________________________________________________
const std::string daq::service::TopologyConfig::ReadPeerIP(const std::string& peer)
{
    const auto &peerInstanceKey = peer.substr(0, peer.find(fSeparator+topology::ChannelPrefix.data()));
    const auto &peerHealthKey   = join({peerInstanceKey, HealthPrefix.data()}, fSeparator);
    auto &r = *GetClient();
    LOG(debug4) << "peerInstanceKey = " << peerInstanceKey << ", peerHealthKey =  " << peerHealthKey;
    auto peerIP = r.hget(peerHealthKey, "hostIp");
    LOG(debug4) << "id = " << fId << " peer health = " << peerHealthKey;
    if (!peerIP) {
        LOG(warn) << "id = " << fId << " hostIp not found";
        return {};
    } else {
        LOG(warn) << "id = " << fId << " hostIp found " << peerIP.value();
    }
    return peerIP.value();
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::Reset()
{
    LOG(debug) << MyClass << " " << __FUNCTION__;
    fBindChannels.clear();
    fConnectChannels.clear();
    for (const auto& [k, v] : fCustomChannelProperties) {
        DeleteProperty(k);
    }
    fCustomChannelProperties.clear();
    Unregister();
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::ResetTtl(sw::redis::Pipeline& pipe)
{
    //LOG(debug) << MyClass << " " << __FUNCTION__ << " num registered = " << fRegisteredKeys.size();
    std::for_each(fRegisteredKeys.cbegin(), fRegisteredKeys.cend(),
    [&pipe, ttl = fMaxTtl](const auto& key) {
        pipe.expire(key, ttl);
    });
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::ResolveConnectAddress()
{
    //LOG(debug) << __PRETTY_FUNCTION__;
    if (fConnectChannels.empty()) {
        return;
    }

    LOG(debug) << __PRETTY_FUNCTION__ << " id = " << fId << " wait done";
    auto &r = *GetClient();

    // list of instances with the same service name
    //     auto sameServices = scan(r, {fTopPrefix, fServiceName, "*", PresencePrefix.data()}, fSeparator);
    //     std::vector<std::string> sortedSameServices;
    //     for (const auto &k : sameServices) {
    //       auto instanceKey = k.substr(0, k.find_last_of(fSeparator));
    //       instanceKey      = instanceKey.substr(instanceKey.find_last_of(fSeparator));
    //       sortedSameServices.push_back(instanceKey);
    //     }
    //     std::sort(sortedSameServices.begin(), sortedSameServices.end());
    //     int myInstanceIndex = 0;
    //     for (const auto &k : sortedSameServices) {
    //       if (k != fServiceName) {
    //         continue;
    //       }
    //       ++myInstanceIndex;
    //     }

    std::unordered_map<std::string, std::vector<std::string>> options;
    for (auto &[name, sp] : fConnectChannels) {
        if (!sp.address.empty() && sp.address!="unspecified") {
            continue;
        }
        LOG(debug) << MyClass << " " << __FUNCTION__ << " id = " << fId << " find peer of " << sp.name << " numSockets = " << sp.numSockets;
        const auto &myInstanceKey = join({fTopPrefix, fServiceName, fId}, fSeparator);
        const auto &myChannelKey  = join({myInstanceKey, topology::ChannelPrefix.data(), sp.name}, fSeparator);

        std::vector<std::string> peers;
        const auto &myPeerKey = join({myChannelKey, topology::PeerPrefix.data()}, fSeparator);
        r.lrange(myPeerKey, 0, -1, std::back_inserter(peers));
        int peerIndex{0};
        SocketProperty res(sp);
        bool is1to1{false};
        for (const auto& p : peers) {
            LOG(debug) << MyClass << " " << __FUNCTION__ << " id = " << fId << " peer of " << name << " : " << p;
            const auto &k = join({p, topology::PeerPrefix.data()}, fSeparator);
            std::vector<std::string> neighbors;
            r.lrange(k, 0, -1, std::back_inserter(neighbors));
            LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " id = " << fId << " n neighbors " << neighbors.size();
            int myIndex = 0; // index viewed from the peer
            // for (const auto& n : neighbors) {
            //   LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " id = " << fId << " neighbor: " << n;
            // }
            for (const auto& n : neighbors) {
                if (n==myChannelKey) {
                    break;
                }
                ++myIndex;
            }
            if (is1to1) {
                if (myIndex!=peerIndex) {
                    ++peerIndex;
                    continue;
                }
            }
            LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " id = " << fId << " myIndex = " << myIndex;
            std::unordered_map<std::string, std::string> h;
            r.hgetall(p, std::inserter(h, h.begin()));
            const auto &peerProperty = ToSocketProperty(h);

            LOG(debug) << "id = " << fId << " numSocket (me) = " << sp.numSockets << ", (peer) = " << peerProperty.numSockets;
            const auto address = ReadPeerAddress(p); //peerHealthKey, *peerIP, peerChannel);
            if ((sp.numSockets<=1) && (peerProperty.numSockets<=1)) {
                is1to1 = true;
                // 1:1 or fan-in/fan-out
                LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " id = " << fId << " 1:1 or fan-in/fan-out ";
                LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__  << " id = " << fId
                           << " peer size = " << peers.size() << " myIndex = " << myIndex << " peerIndex = " << peerIndex
                           << " address.size() = " << address.size();
                if ((myIndex==peerIndex) || (peers.size()==1)) {
                    res.address = address[0];
                    break;
                }
            } else if ((sp.numSockets<=1) && (peerProperty.numSockets>1)) {
                // 1:m
                LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " id = " << fId << " 1:m ";
                res.address = address[myIndex];
            } else if ((sp.numSockets>1) && (peerProperty.numSockets<=1)) {
                // n:1
                LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " id = " << fId << " n:1 ";
                assert(address.size()==1);
                res.address += (res.address.empty()) ? address[0] : ("," + address[0]);
            } else if ((sp.numSockets>1) && (peerProperty.numSockets>1)) {
                // n:m
                LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " id = " << fId << " n:m ";
                assert(address.size()>myIndex);
                res.address += (res.address.empty()) ? address[myIndex] : ("," + address[myIndex]);
            }
            ++peerIndex;
        }
        LOG(debug) << " id = " << fId << " add socket property : " << res.name << " " << res.address;
        options[res.name].emplace_back(ToChannelConfig(res));
    }

//  LOG(debug) << " before update";
//  PrintConfig(GetPropertiesAsStringStartingWith("channel-config"), "channel-config");
//  PrintConfig(GetPropertiesAsStringStartingWith("chans."), "chans.");

    if (options.empty()) {
        return;
    }

    try {
        for (const auto& [name, channelConfig] : options) {
            auto properties = fair::mq::SuboptParser(channelConfig, fServiceName);
            for (const auto & [k, v] : properties) {
                const auto s = fair::mq::PropertyHelper::ConvertPropertyToString(v);
                fCustomChannelProperties[k] = s;
                LOG(debug) << " id = " << fId << " set property : " << k << " " << s;
            }

            SetProperties(properties);
        }
    } catch (const std::exception& e) {
        LOG(error) << MyClass << " error on SetProperty(chans.) : id = " << fId << ": " << e.what();
    } catch (...) {
        LOG(error) << MyClass << " unknown exception on SetProperty(chans.) :";
    }
//  LOG(debug) << " after update";
//  PrintConfig(GetPropertiesAsStringStartingWith("channel-config"), "channel-config");
//  PrintConfig(GetPropertiesAsStringStartingWith("chans."), "chans.");
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::Unregister()
{
    if (!fRegisteredKeys.empty()) {
        auto ndeleted = GetClient()->del(fRegisteredKeys.cbegin(), fRegisteredKeys.cend());
        fRegisteredKeys.clear();
        LOG(debug) << MyClass << " " << __FUNCTION__ << " n deleted = " << ndeleted;
    }
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::WaitBindAddress()
{
    //LOG(debug) << __PRETTY_FUNCTION__;
    if (fConnectChannels.empty()) {
        return;
    }

    auto &r = *GetClient();
    // find bind channels of peers
    std::unordered_set<std::string> channels;
    for (const auto& [name, sp] : fConnectChannels) {
        for (const auto& [lk, lp] : fLinks) {
            LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " connect " << sp.name
                       << ", " << lp.myService << ":" << lp.myChannel
                       << ", " << lp.peerService << ":" << lp.peerChannel;
            if ((fServiceName == lp.myService) && (sp.name == lp.myChannel)) {
                auto k = join({fTopPrefix, lp.peerService, "*", PresencePrefix.data()}, fSeparator);
                LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " : k = " << k;
                auto presenceKeys = scan(r, {fTopPrefix, lp.peerService, "*", PresencePrefix.data()}, fSeparator);
                LOG(debug) << __LINE__ << ": n presene: " << presenceKeys.size();
                for (auto &a : presenceKeys) {
                    auto c =  a.substr(0, a.find_last_of(fSeparator));
                    // e.g.: daq_service:peer-service:peer-instance-id:endpoint:peer-chanenl
                    channels.emplace(join({c, topology::ChannelPrefix.data(), lp.peerChannel}, fSeparator));
                }
            } else if ((fServiceName == lp.peerService) && (sp.name == lp.peerChannel)) {
                auto k = join({fTopPrefix, lp.myService, "*", PresencePrefix.data()}, fSeparator);
                LOG(debug) << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " : k = " << k;
                auto presenceKeys = scan(r, {fTopPrefix, lp.myService, "*", PresencePrefix.data()}, fSeparator);
                LOG(debug) << __LINE__ << ": n presene: " << presenceKeys.size();
                for (auto &a : presenceKeys) {
                    auto c = a.substr(0, a.find_last_of(fSeparator));
                    channels.emplace(join({c, topology::ChannelPrefix.data(), lp.myChannel}, fSeparator));
                }
            }
        }
    }

    for (const auto &c : channels) {
        while (true) {
            LOG(warn) << MyClass << " " << __FUNCTION__ << " wait channel : " << c;
            auto v = r.hget(c, "bound");
            if (v) {
                auto s = boost::to_lower_copy(*v);
                if ((s=="1") || (s=="true")) {
                    break;
                }
            }
            if (IsCanceled()) {
                return;
            }
            std::this_thread::sleep_for(1000ms);
        }
    }
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::WriteAddress(MQChannel &channels, std::function<void (sw::redis::Pipeline&, std::string_view)> f)
{
    auto &r    = *GetClient();
    auto pipe  = r.pipeline();

    std::lock_guard<std::mutex> lock{GetMutex()};
    try {
        for (auto &[name, sp] : channels) {
            auto localKeyPrefix = "chans." + sp.name + ".";
            for (auto index=0; ; ++index) {
                auto localKey = localKeyPrefix + std::to_string(index);
                const auto &chans = GetPropertiesAsStringStartingWith(localKey);
                if (chans.empty()) {
                    break;
                }
                const auto &key = join({fTopPrefix, fServiceName, fId, topology::SocketPrefix.data(), localKey}, fSeparator);
                std::ostringstream ss;
                ss << MyClass << " " << __FUNCTION__ << ":" << __LINE__ << " key = " << key << " :\n";
                std::map<std::string, std::string> h;
                for (const auto &[k, v] : chans) {
                    auto hk = k.substr(k.find_last_of(".")+1);
                    h[hk] = v;
                    ss << " " << hk << ", " << v << "\n";
                }
                LOG(debug1) << ss.str();

                h["numSockets"]     = std::to_string(sp.numSockets);
                h["autoSubChannel"] = std::to_string(sp.autoSubChannel);

                pipe.hset(key, h.cbegin(), h.cend());
                pipe.expire(key, fMaxTtl);
                fRegisteredKeys.push_back(key);

            }
            if (f) {
                f(pipe, name);
            }
        }
        pipe.exec();
    } catch (const std::exception &e) {
        LOG(error) << MyClass << " " << __FUNCTION__ << " caught exception : " << e.what();
    } catch (...) {
        LOG(error) << MyClass << " " << __FUNCTION__ << " caught unknown exception";
    }
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::WriteBindAddress()
{
    //LOG(debug) << __PRETTY_FUNCTION__;
    if (fBindChannels.empty()) {
        return;
    }

    LOG(debug) << MyClass << " write bind address to the registry. (n =  " << fBindChannels.size() << ")";
    WriteAddress(fBindChannels, [this](auto &pipe, auto name) {
        auto channel = join({fTopPrefix, fServiceName, fId, topology::ChannelPrefix.data(), name.data()}, fSeparator);
        pipe.hset(channel, "bound", "1");
        LOG(warn) << MyClass << " " << __FUNCTION__ << " bound channel: " << channel;
    });

    //LOG(debug) << __PRETTY_FUNCTION__ << " done";
}

//_____________________________________________________________________________
void daq::service::TopologyConfig::WriteChannel(SocketProperty &sp, const std::vector<std::string> &peers)
{
    if (peers.empty()) {
        //LOG(debug) << " empty peers";
        return;
    }
    const auto &key = join({fTopPrefix, fServiceName, fId, topology::ChannelPrefix.data(), sp.name}, fSeparator);

    LOG(debug) << MyClass << " " << __FUNCTION__ << " channel : " << sp.name << " : n peers = " << peers.size();
    fPlugin.SetProperty("n-peers:"s+sp.name, std::to_string(peers.size()));

    auto pipe = GetClient()->pipeline();
    pipe.hset(key, {
        std::make_pair("name",           sp.name),
        std::make_pair("type",           sp.type),
        std::make_pair("method",         sp.method),
        std::make_pair("address",        sp.address),
        std::make_pair("transport",      sp.transport),
        std::make_pair("sndBufSize",     std::to_string(sp.sndBufSize)),
        std::make_pair("rcvBufSize",     std::to_string(sp.rcvBufSize)),
        std::make_pair("sndKernelSize",  std::to_string(sp.sndKernelSize)),
        std::make_pair("rcvKernelSize",  std::to_string(sp.rcvKernelSize)),
        std::make_pair("linger",         std::to_string(sp.linger)),
        std::make_pair("rateLogging",    std::to_string(sp.rateLogging)),
        std::make_pair("portRangeMin",   std::to_string(sp.portRangeMin)),
        std::make_pair("portRangeMax",   std::to_string(sp.portRangeMax)),
        std::make_pair("autoBind",       std::to_string(sp.autoBind)),
        std::make_pair("numSockets",     std::to_string(sp.numSockets)),
        std::make_pair("autoSubChannel", std::to_string(sp.autoSubChannel)),
        std::make_pair("bound",          std::to_string(sp.bound)),
    });
    pipe.expire(key, fMaxTtl);

    auto listKey = join({key, topology::PeerPrefix.data()}, fSeparator);
    pipe.rpush(listKey, peers.cbegin(), peers.cend());
    pipe.expire(listKey, fMaxTtl);

    pipe.exec();

    fRegisteredKeys.push_back(key);
    fRegisteredKeys.push_back(listKey);

}

//_____________________________________________________________________________
void daq::service::TopologyConfig::WriteConnectAddress()
{
    // LOG(debug) << __PRETTY_FUNCTION__;
    if (fConnectChannels.empty()) {
        return;
    }

    LOG(debug) << MyClass << " write connect address to the registry. (n =  " << fConnectChannels.size() << ")";
    WriteAddress(fConnectChannels);
    //LOG(debug) << __PRETTY_FUNCTION__ << " done";
}