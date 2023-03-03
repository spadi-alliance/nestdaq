#include <iostream>
#include <iterator>
#include <unordered_map>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <fairmq/FairMQLogger.h>

#include <sw/redis++/redis++.h>

#include "plugins/Constants.h"
#include "plugins/Functions.h"
#include "plugins/ParameterConfigPlugin.h"

using namespace std::literals::string_literals;

static constexpr std::string_view MyClass{"daq::service::ParameterConfigPlugin"};

const std::unordered_set<std::string_view> reservedOptionsString
{   "id",  //
    "transport", //
    "network-interface", //
    "shm-allocation", //
    "session", //
    "config-key", //
    "mq-config", //
    "severity", //
    "file-severity", //
    "verbosity", //
    "log-to-file", //
};

const std::unordered_set<std::string_view> reservedOptionsInt
{   "io-threads", //
    "init-timeout", //
};

const std::unordered_set<std::string_view> reservedOptionsBool
{   "print-channels", //
    "shm-mlock-segment", //
    "shm-zero-segment", //
    "shm-throw-bad-alloc", //
    "shm-monitor", //
};

const std::unordered_set<std::string_view> reservedOptionsSize
{   "shm-segment-size", //
    "ofi-size-hint", //
    "color", //
};

const std::unordered_set<std::string_view> reservedOptionsUint16
{   "shm-segment-id", //
};

const std::unordered_set<std::string_view> reservedOptionsFloat
{   "rate", //
};

const std::unordered_set<std::string_view> reservedOptionsVectorString
{   "channel-config", //
};
namespace daq::service {
//_____________________________________________________________________________
auto ParameterConfigPluginProgramOptions() -> fair::mq::Plugin::ProgOptions
{
    namespace bpo = boost::program_options;
    using opt = ParameterConfigPlugin::OptionKey;
    auto options = bpo::options_description(MyClass.data());
    options.add_options()
    (opt::ServerUri.data(), bpo::value<std::string>(), "Redis server URI (if empty, the same URI of the service registry is used.)");
    return options;
}

//_____________________________________________________________________________
ParameterConfigPlugin::ParameterConfigPlugin(std::string_view name,
        const fair::mq::Plugin::Version &version,
        std::string_view maintainer,
        std::string_view homepage,
        fair::mq::PluginServices *pluginServices)
    : fair::mq::Plugin(name.data(), version, maintainer.data(), homepage.data(), pluginServices)
{
    LOG(debug) << MyClass << " hello";
    using opt = ParameterConfigPlugin::OptionKey;
    std::string serverUri;
    if (PropertyExists(opt::ServerUri.data())) {
        serverUri = GetProperty<std::string>(opt::ServerUri.data());
    } else if (PropertyExists(ServiceRegistryUri.data())) {
        serverUri = GetProperty<std::string>(ServiceRegistryUri.data());
    }
    if (!serverUri.empty()) {
        fClient = std::make_shared<sw::redis::Redis>(serverUri);
    }

    SubscribeToDeviceStateChange([this](DeviceState newState) {
        // LOG(debug) << MyClass << " state change: " << GetStateName(newState);
        if (fClient) {
            ReadParameters();
        }
    });
}

//_____________________________________________________________________________
ParameterConfigPlugin::~ParameterConfigPlugin()
{
    UnsubscribeFromDeviceStateChange();
    LOG(debug) << MyClass << " bye";
}

//_____________________________________________________________________________
bool ParameterConfigPlugin::IsReservedOption(std::string_view name) const
{
    if (reservedOptionsString.count(name)>0) {
        return true;
    }
    if (reservedOptionsInt.count(name)>0) {
        return true;
    }
    if (reservedOptionsBool.count(name)>0) {
        return true;
    }
    if (reservedOptionsSize.count(name)>0) {
        return true;
    }
    if (reservedOptionsUint16.count(name)>0) {
        return true;
    }
    if (reservedOptionsFloat.count(name)>0) {
        return true;
    }
    if (reservedOptionsVectorString.count(name)>0) {
        return true;
    }
    return false;
}

//_____________________________________________________________________________
void ParameterConfigPlugin::Parse(std::string_view name, std::string line)
{
    //LOG(debug) << " parameter: field = " << name << ", value = " << line;
    if (IsReservedOption(name)) {
        SetPropertyOfReservedOption(name, line);
    } else if (line.find(",")==std::string::npos) {
        SetPropertyFromString<std::string>(name, line);
    } else if (line.find("=")==std::string::npos) {
        // <value> has "," but doesn't have "=".
        ToArray(name, line);
    } else {
        // <value> has "," and "=".
        ToMap(name, line);
    }
}

//_____________________________________________________________________________
void ParameterConfigPlugin::ReadHash(const std::string& name)
{
    std::unordered_map<std::string, std::string> h;
    fClient->hgetall(name, std::inserter(h, h.begin()));
    std::string prefix = (fKey==name) ? ""s : name.substr(name.find_last_of(fSeparator)+1).data();
    for (const auto &[field, value] : h) {
        auto f = prefix.empty() ? field : (prefix + fSeparator + field);
        Parse(f, value);
    }
}

//_____________________________________________________________________________
void ParameterConfigPlugin::ReadList(const std::string& name)
{
    std::vector<std::string> v;
    fClient->lrange(name, 0, -1, std::back_inserter(v));

    std::string ss;
    for (const auto& x : v) {
        ss += x + ", ";
    }
    LOG(debug) << " list: name = " << name << ", values = " << ss;
    if (!v.empty()) {
        SetProperty(name, v);
    }
}

//_____________________________________________________________________________
void ParameterConfigPlugin::ReadParameters()
{
    //LOG(debug) << MyClass << " " << __FUNCTION__;

    if (fId.empty()) {
        if (PropertyExists("id")>0) {
            fId = GetProperty<std::string>("id");
        } else {
            return;
        }
    }

    if (fSeparator.empty()) {
        if (PropertyExists(Separator.data())>0) {
            fSeparator = GetProperty<std::string>(Separator.data());
        } else {
            return;
        }
    }

    //LOG(debug) << " separator  = " << fSeparator;

    if (fKey.empty()) {
        fKey = ParametersPrefix.data() + fSeparator + fId;
    }

    //LOG(debug) << " parameter config key = " << fKey;
    ReadHash(fKey);

    auto scanKey = fKey + fSeparator + "*";
    //LOG(debug) << " parameter read hash done. scanning additional parameters ... : " << scanKey;
    const auto keys = scan(*fClient, scanKey);
    if (!keys.empty()) {
        LOG(debug) << " additional parameters found.";
        for (const auto & x : keys) {
            auto t = fClient->type(x);
            LOG(debug) << " key = " << x << ", type = " << t;
            if (t=="string") {
                ReadString(x);
            } else if (t=="list") {
                ReadList(x);
            } else if (t=="hash") {
                ReadHash(x);
            } else if (t=="set") {
                ReadSet(x);
            } else if (t=="zset") {
                ReadZset(x);
            }
        }
    }

    //LOG(debug) << MyClass << " " << __FUNCTION__ << " done";
}

//_____________________________________________________________________________
void ParameterConfigPlugin::ReadSet(const std::string& name)
{
    std::unordered_set<std::string> members;
    fClient->smembers(name, std::inserter(members, members.begin()));

    std::string ss;
    for (const auto & x : members) {
        ss += x + ", ";
    }
    LOG(debug) << " set: name = " << name << ", values = " << ss;

    if (!members.empty()) {
        SetProperty(name, members);
    }
}

//_____________________________________________________________________________
void ParameterConfigPlugin::ReadString(const std::string& name)
{
    auto value = fClient->get(name);
    if (!value) {
        return;
    }
    std::string field = name.substr(name.find_last_of(fSeparator)+1).data();
    LOG(debug) << " string: name = " << field << ", value = " << *value;
    Parse(field, *value);
}

//_____________________________________________________________________________
void ParameterConfigPlugin::ReadZset(const std::string& name)
{
    std::unordered_map<std::string, double> m;
    fClient->zrange(name, 0, -1, std::inserter(m, m.end()));
    std::string ss;
    for (const auto & [k, v] : m) {
        ss += "{" + k + ": " + std::to_string(v) + "}, ";
    }
    LOG(debug) << " zset: name = " << name << ", values = " << ss;

    if (!m.empty()) {
        SetProperty(name, m);
    }
}

//_____________________________________________________________________________
void ParameterConfigPlugin::SetPropertyOfReservedOption(std::string_view name, std::string_view value)
{
    if (reservedOptionsString.count(name)>0) {
        SetPropertyFromString<std::string>(name,  value);
        return;
    }

    if (reservedOptionsInt.count(name)>0) {
        SetPropertyFromString<int>(name, value);
        return;
    }

    if (reservedOptionsSize.count(name)>0) {
        SetPropertyFromString<uint64_t>(name, value);
        return;
    }

    if (reservedOptionsBool.count(name)>0) {
        SetPropertyFromString<bool>(name, value);
        return;
    }

    if (reservedOptionsFloat.count(name)>0) {
        SetPropertyFromString<float>(name, value);
        return;
    }

    if (reservedOptionsVectorString.count(name)>0) {
        ToArray(name, value.data());
        return;
    }
}

//_____________________________________________________________________________
void ParameterConfigPlugin::ToArray(std::string_view name, std::string line)
{
    std::vector<std::string> v;
    // remove left-space and right space
    boost::trim_if(line, boost::is_space());
    // split at "," or " "  (ignore empty string)
    boost::split(v, line, boost::is_any_of(", "), boost::token_compress_on);
    std::string ss;
    for (const auto& x : v) {
        ss += x + ", ";
    }
    LOG(debug) << " parameter (vector): field = " << name << ", values = " << ss;
    SetProperty(name.data(), v);
}

//_____________________________________________________________________________
void ParameterConfigPlugin::ToMap(std::string_view name, std::string line)
{
    // Assuming the number of counts of "," and "=" are the same.
    std::vector<std::string> v;
    // remove left-space and right space
    boost::trim_if(line, boost::is_space());
    // split at "," (ignore empty string)
    boost::split(v, line, boost::is_any_of(","), boost::token_compress_on);
    std::unordered_map<std::string, std::string> m;
    std::string ss;
    for (auto& x : v) {
        LOG(debug) << " x = " << x;
        std::vector<std::string> s;
        // remove left-space and right space
        boost::trim_if(x, boost::is_space());
        // split at "=" or " " (ignore empty string)
        boost::split(s, x, boost::is_any_of("= "), boost::token_compress_on);
        if (s.size()==2) {
            m.emplace(s[0], s[1]);
            ss+="{" + s[0] + ": " + s[1] + "}, ";
        }
    }
    LOG(debug) << " parameter (map): field = " << name << ", values = " << ss;
    SetProperty(name.data(), m);
}

} // namespace daq::service