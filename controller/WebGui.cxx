#include <algorithm>
#include <chrono>
#include <iostream>
#include <regex>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/process.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <sw/redis++/redis++.h>

#include <fairmq/States.h>
#include <fairmq/FairMQLogger.h>

#include "plugins/Constants.h"
#include "plugins/Functions.h"
#include "plugins/tools.h"
#include "controller/WebGui.h"

static constexpr std::string_view MyClass{"WebGui"};
constexpr int NStates = static_cast<int>(fair::mq::State::Exiting) + 1;

using namespace std::string_literals;
using namespace std::chrono_literals;

namespace run_info {
static constexpr std::string_view Prefix{"run_info"};
static constexpr std::string_view LatestRunNumber{"latest_run_number"};
static constexpr std::string_view RunNumber{"run_number"};
static constexpr std::string_view WaitDeviceReady{"wait-device-ready"};
static constexpr std::string_view WaitReady{"wait-ready"};
static const std::unordered_set<std::string_view> knownRunInfoList{
    RunNumber,
    WaitDeviceReady,
    WaitReady,
};
}

static const std::unordered_set<std::string_view> knownCommandList{
    fairmq::command::Bind,
    fairmq::command::CompleteInit,
    fairmq::command::Connect,
    fairmq::command::End,
    fairmq::command::InitDevice,
    fairmq::command::InitTask,
    fairmq::command::ResetDevice,
    fairmq::command::ResetTask,
    fairmq::command::Run,
    fairmq::command::Stop,
    daq::command::Exit,
    daq::command::Quit,
    daq::command::Reset,
    daq::command::Start,
};

static const std::vector<std::string> waitDeviceReadyTargets {
    GetStateName(fair::mq::State::DeviceReady),
    GetStateName(fair::mq::State::Ready),
    GetStateName(fair::mq::State::Running),
};

static const std::vector<std::string> waitReadyTargets {
    GetStateName(fair::mq::State::Ready),
    GetStateName(fair::mq::State::Running),
};

//_____________________________________________________________________________
std::string GetRedisDBNumber(const std::string& uri)
{
    //                      scheme    ://host      :port (/db)
    std::regex pattern{R"(^([^:\/?#]+)://([^\/?#]+):(\d+)/?(\d*))"};
    std::smatch matchResult;
    if (std::regex_match(uri, matchResult, pattern)) {
        int count{0};
        for (const auto &s : matchResult) {
            LOG(debug) << count++ << " " << s;
        }
    } else {
        LOG(error) << " std::regex_match failed. uri = " << uri;
    }
    const auto &db = matchResult[4].str();
    return db.empty() ? "0" : db;
}

//_____________________________________________________________________________
bool WebGui::ConnectToRedis(std::string_view redisUri,
                            std::string_view commandChannelName,
                            std::string_view separator)
{
    // setup redis client
    if (redisUri.empty()) {
        throw std::runtime_error("redis server uri is not specified.");
    }
    fClient = std::make_shared<sw::redis::Redis>(redisUri.data());
    if (!fClient) {
        LOG(error) << " failed to connect to redis";
        return false;
    }
    LOG(info) << "connected to redis";
    fChannelName = commandChannelName.data();
    fSeparator = separator.data();
    fClient->command("client", "setname", MyClass.data());

    // E: Enable key-event notification, published with "__keyevent@<db>__" prefix
    // x: Expired events (events generated every time a key expires)
    fClient->command("config", "set", "notify-keyspace-events", "AKE");
    const auto &db = GetRedisDBNumber(redisUri.data());
    fRedisKeyEventChannelName = "__keyevent@"s + db + "__:expired"s;

    fRedisPubSubListenThread = std::thread([this]() {
        SubscribeToRedisPubSub();
    });
    fRedisPubSubListenThread.detach();

    fStatePollThread = std::thread([this]() {
        PollState();
    });
    fStatePollThread.detach();
    return true;
}

//_____________________________________________________________________________
// read/write operation on redis and send the value to the web client
void WebGui::CopyLatestRunNumber(unsigned int connid)
{
    LOG(debug) << __func__ << " websocket connid = " << connid << std::endl;
    std::string name{run_info::Prefix.data() + fSeparator + run_info::RunNumber.data()};
    auto ret = fClient->get(name);
    if (!ret) {
        Send(connid, {R"({ "type": "error", "value": "could not get run number from redis." })"});
        return;
    }
    name = run_info::Prefix.data() + fSeparator + run_info::LatestRunNumber.data();
    fClient->set(name, *ret);

    boost::property_tree::ptree obj;
    obj.put("type", "set latest_run_number");
    obj.put("value", *ret);
    const auto &reply = to_string(obj);
    Send(connid, reply);
}

//_____________________________________________________________________________
// increment operation on redis and send the value to the web client
void WebGui::IncrementRunNumber(unsigned int connid)
{
    LOG(debug) << __func__ << " websocket connid = " << connid << std::endl;
    std::string name{run_info::Prefix.data() + fSeparator + run_info::RunNumber.data()};

    auto newValue = fClient->incr(name);

    boost::property_tree::ptree obj;
    obj.put("type", "set run_number");
    obj.put("value", std::to_string(newValue));
    const auto &reply = to_string(obj);
    Send(connid, reply);
}

//_____________________________________________________________________________
void WebGui::InitializeFunctionList()
{
    AddFunction({
        // function called on new client connection
        // {   "ON_CONNECT",
        //     [this](auto id, const auto &arg) {
        //     }
        // },

        // // function called on a client closed
        // {   "ON_CLOSED",
        //     [this](auto id, const auto &arg) {
        //     }
        // },

        // send command via redis pub/sub channels
        {   "redis-publish", [this](auto id, const auto &arg) {
                RedisPublishDaqCommand(id, arg);
            }
        },

        // read from redis
        {   "redis-get", [this](auto id, const auto &arg) {
                RedisGet(id, arg);
            }
        },

        // write to redis
        {   "redis-set", [this](auto id, const auto &arg) {
                RedisSet(id, arg);
            }
        },

        // increment operation on redis
        {   "redis-incr", [this](auto id, const auto &arg) {
                RedisIncr(id, arg);
            }
        },

    });

}

//_____________________________________________________________________________
void WebGui::PollState()
{
    auto tPrev = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    while (true) {

        auto tNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if ((tNow - tPrev) < fPollIntervalMS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(fPollIntervalMS - (tNow - tPrev)));
            continue;
        }
        tPrev = tNow;

        std::map<std::string, ServiceState> summaryTable;
        const auto &stateKeys = daq::service::scan(*fClient, {daq::service::TopPrefix.data(), "*", "*", daq::service::FairMQStatePrefix.data()}, fSeparator);
        if (stateKeys.empty()) {
            SendStateSummary(summaryTable);
            continue;
        }
        std::vector<sw::redis::OptionalString> stateValues;
        fClient->mget(stateKeys.begin(), stateKeys.end(), std::back_inserter(stateValues));

        const auto &updateTimeKeys = daq::service::scan(*fClient, {daq::service::TopPrefix.data(), "*", "*", daq::service::UpdateTimePrefix.data()}, fSeparator);
        std::vector<sw::redis::OptionalString> updateTimeValues;
        if (!updateTimeKeys.empty()) {
            fClient->mget(updateTimeKeys.begin(), updateTimeKeys.end(), std::back_inserter(updateTimeValues));
        }

        int i=0;
        for (const auto &k : stateKeys) {
            std::vector<std::string> res;
            boost::split(res, k, boost::is_any_of(fSeparator));
            const auto &serviceName = res[1];
            const auto &instName = res[2];
            auto &ss = summaryTable[serviceName];
            auto &inst = ss.instances[instName];
            if (stateValues[i]) {
                inst.state = *stateValues[i];
            } else {
                inst.state = GetStateName(fair::mq::State::Undefined);
            }
            ++i;
        }

        i=0;
        for (const auto &k : updateTimeKeys) {
            std::vector<std::string> res;
            boost::split(res, k, boost::is_any_of(fSeparator));
            const auto &serviceName = res[1];
            const auto &instName = res[2];
            if (summaryTable.count(serviceName)==0) {
                ++i;
                continue;
            }
            auto &ss = summaryTable[serviceName];
            if (ss.instances.count(instName)==0) {
                ++i;
                continue;
            }
            auto &inst = ss.instances[instName];
            if (updateTimeValues[i]) {
                inst.date = *updateTimeValues[i];
            }
            ++i;
        }

        for (auto &[sname, ss] : summaryTable) {
            ss.counts.resize(NStates, 0);
            for (const auto& [instName, inst] : ss.instances) {
                if (!inst.state.empty()) {
                    auto istate = static_cast<int>(fair::mq::GetState(inst.state));
                    if (istate >= NStates) {
                        LOG(error) << __func__ << " bad state id = " << istate << ": service = " << sname << ", instance = " << instName;
                        continue;
                    }
                    ++ss.counts[istate];
                }
                if (!inst.date.empty()) {
                    if (ss.date.empty() || (ss.date < inst.date)) {
                        ss.date = inst.date;
                    }
                }
            }
        }
        SendStateSummary(summaryTable);
    } // while ()
}

//_____________________________________________________________________________
void WebGui::ProcessData(unsigned int connid,
                         const std::string& arg)
{
    std::lock_guard<std::mutex> lock{fMutex};
    LOG(debug) << __func__ << " websocket connid = " << connid << " : arg =  " << arg;
    const auto &obj = to_json(arg);
    const auto& key = obj.get_optional<std::string>("command");
    if (key) {
        LOG(debug) << __func__ << " key (function) = " << key.get();
        fFuncList[key.get()](connid, obj);
    }
//  for (auto& f : fFuncList) {
//    f(connid, obj);
//  }
}

//_____________________________________________________________________________
void WebGui::ProcessExpiredKey(std::string_view key)
{
    LOG(trace) << __func__ << ":" << __LINE__ << " " << key;
    try {
        if (key.find("presence")!=std::string_view::npos) {
            std::vector<std::string> v;
            boost::split(v, key.data(), boost::is_any_of(":")); // prefix:service:instance:presence
            LOG(trace) << __LINE__ << " v.size() = " << v.size();
            const auto& serviceName = v[1];
            const auto& instName    = v[2];
            const auto& instIndex   = instName.substr(instName.find("-")+1);
            {
                const auto& key = daq::service::join({daq::service::TopPrefix.data(), daq::service::ServiceInstanceIndexPrefix.data(), serviceName}, fSeparator);
                fClient->hdel(key, instIndex);
                LOG(warn) << " delete instance index: key = " << key << ", field = " << instIndex;
            }
        }
    } catch (const std::exception &e) {
        LOG(error) << __func__ << " e.what() = " << e.what();
    } catch (...) {
        LOG(error) << __func__ << " unknown exception";
    }
}

//_____________________________________________________________________________
// read operation on redis and send the value to the web client
void WebGui::ReadLatestRunNumber(unsigned int connid)
{
    LOG(debug) << __func__ << " websocket connid = " << connid;
    std::string name{run_info::Prefix.data() + fSeparator + run_info::LatestRunNumber.data()};
    auto ret = fClient->get(name);
    if (!ret) {
        Send(connid, {R"({ "type": "error", "value": "could not get latest run number from redis." })"});
        return;
    }
    boost::property_tree::ptree obj;
    obj.put("type", "set latest_run_number");
    obj.put("value", *ret);
    const auto &reply = to_string(obj);
    Send(connid, reply);
}

//_____________________________________________________________________________
// read operation on redis and send the value to the web client
void WebGui::ReadRunNumber(unsigned int connid)
{
    LOG(debug) << __func__ << " websocket connid = " << connid;
    std::string name{run_info::Prefix.data() + fSeparator + run_info::RunNumber.data()};
    auto ret = fClient->get(name);
    if (!ret) {
        Send(connid, {R"({ "type": "error", "value": "could not get run number from redis." })"});
        return;
    }
    boost::property_tree::ptree obj;
    obj.put("type", "set run_number");
    obj.put("value", *ret);
    const auto &reply = to_string(obj);
    Send(connid, reply);
}

//_____________________________________________________________________________
void WebGui::RedisGet(unsigned int connid, const boost::property_tree::ptree &arg)
{
    LOG(debug) << __func__ << " websocket connid = " << connid;
    const auto &val = arg.get_optional<std::string>("value");
    if (val) {
        if (*val=="run_number") {
            ReadRunNumber(connid);
            ReadLatestRunNumber(connid);
        }
    }
}

//_____________________________________________________________________________
void WebGui::RedisIncr(unsigned int connid, const boost::property_tree::ptree &arg)
{
    const auto& val = arg.get_optional<std::string>("value");
    if (val) {
        if (*val=="run_number") {
            IncrementRunNumber(connid);
        }
    }
}

//_____________________________________________________________________________
// publish command via redis
void WebGui::RedisPublishDaqCommand(unsigned int connid, const boost::property_tree::ptree& arg)
{
    auto isWaitFlagSet = [this](const auto &s) {
        auto w = fClient->get(run_info::Prefix.data() + fSeparator + s);
        if (!w) {
            return false;
        }
        const auto &v = boost::to_lower_copy(*w);
        return (v == "1") || (v == "true");
    };
    auto toMessage = [&arg](const auto &v) {
        boost::property_tree::ptree cmd;
        cmd.put("command", "change_state");
        cmd.put("value", v);
        cmd.add_child("services", arg.get_child("services"));
        cmd.add_child("instances", arg.get_child("instances"));
        //cmd.put("service", "all");
        //cmd.put("instance", "all");
        return to_string(cmd);
    };

    const auto& arg_str = to_string(arg);
    LOG(debug) << __func__ << " arg = " << arg_str;
    const auto &val = arg.get_optional<std::string>("value");
    if (!val) {
        LOG(error) << " value is missing.";
        return;
    }

    const auto& v= *val;
    if (v == fairmq::command::Run.data()) {
        CopyLatestRunNumber(connid);
    }
    if (knownCommandList.count(v)>0) {
        LOG(debug) << " connid = " << connid;

        try {

            bool waitDeviceReadyFlag = isWaitFlagSet(run_info::WaitDeviceReady.data());
            bool waitReadyFlag       = isWaitFlagSet(run_info::WaitReady.data());
            std::unordered_set<std::string> services;
            for (const auto& x : arg.get_child("services")) {
                services.emplace(x.second. template get_value<std::string>());
            }
            std::unordered_set<std::string> instances;
            for (const auto& x : arg.get_child("instances")) {
                instances.emplace(x.second. template get_value<std::string>());
            }

            // use boost::iequals for case insensitive compare
            if (boost::iequals(v, fairmq::command::Connect)) {
                fClient->publish(fChannelName, toMessage(fairmq::command::Connect));
                if (waitDeviceReadyFlag) {
                    Wait(services, instances, waitDeviceReadyTargets);
                }

            } else if (boost::iequals(v, fairmq::command::InitTask)) {
                if (waitDeviceReadyFlag) {
                    fClient->publish(fChannelName, toMessage(fairmq::command::Connect));
                    Wait(services, instances, waitDeviceReadyTargets);
                }
                fClient->publish(fChannelName, toMessage(fairmq::command::InitTask));
                if (waitReadyFlag) {
                    Wait(services, instances, waitReadyTargets);
                }

            } else if (boost::iequals(v, fairmq::command::Run)) {
                if (waitDeviceReadyFlag) {
                    fClient->publish(fChannelName, toMessage(fairmq::command::Connect));
                    Wait(services, instances, waitDeviceReadyTargets);
                }
                if (waitReadyFlag) {
                    fClient->publish(fChannelName, toMessage(fairmq::command::InitTask));
                    Wait(services, instances, waitReadyTargets);
                }
                LOG(debug) << " pre-run = " << fPreRunCommand;
                boost::process::system(fPreRunCommand.data(), boost::process::std_out > stdout, boost::process::std_err > stderr, boost::process::std_in < stdin);
                fClient->publish(fChannelName, toMessage(fairmq::command::Run));
                LOG(debug) << " post-run = " << fPostRunCommand;
                boost::process::system(fPostRunCommand.data(), boost::process::std_out > stdout, boost::process::std_err > stderr, boost::process::std_in < stdin);

            } else if (boost::iequals(v,  fairmq::command::Stop)) {
                LOG(debug) << " pre-stop = " << fPreStopCommand;
                boost::process::system(fPreStopCommand.data(), boost::process::std_out > stdout, boost::process::std_err > stderr, boost::process::std_in < stdin);
                fClient->publish(fChannelName, toMessage(fairmq::command::Stop));
                LOG(debug) << " post-stop = " << fPostStopCommand;
                boost::process::system(fPostStopCommand.data(), boost::process::std_out > stdout, boost::process::std_err > stderr, boost::process::std_in < stdin);

            } else {
                fClient->publish(fChannelName, toMessage(v));
            }
        } catch (const std::exception &e) {
            LOG(error) << __func__ << " e.what() = " << e.what();
        } catch (...) {
            LOG(error) << __func__ << " unknown exception";
        }
    }

}

//_____________________________________________________________________________
void WebGui::RedisSet(unsigned int connid, const boost::property_tree::ptree &arg)
{
    LOG(debug) <<  __func__ << " " << connid;
    const auto &name = arg.get_optional<std::string>("name");
    if (name) {
        if (run_info::knownRunInfoList.count(*name)>0) {

            auto val = arg.get_optional<std::string>("value");
            if (!val) {
                LOG(error) << MyClass << " " << __func__ << " parse error ";
                return;
            }
            std::string key{run_info::Prefix.data() + fSeparator + *name};
            fClient->set(key, *val);
        }
    }
}

//_____________________________________________________________________________
void WebGui::SendStateSummary(const std::map<std::string, ServiceState> & summaryTable)
{
    static std::map<std::string, ServiceState> prevTable;
    bool serviceListChanged = false;
    bool instanceListChanged = false;
    if (prevTable.size() != summaryTable.size()) {
        serviceListChanged = true;
        instanceListChanged = true;
    } else {
        for (const auto& [k, v] : summaryTable) {
            if (prevTable.count(k)==0) {
                serviceListChanged = true;
                instanceListChanged = true;
                break;
            }
        }
        if (!serviceListChanged) {
            for (const auto& [k, v] : summaryTable) {
                const auto& srv = prevTable[k];
                if (srv.instances.size()!=v.instances.size()) {
                    instanceListChanged = true;
                    break;
                }
                for (const auto &[instK, instV] : v.instances) {
                    if (srv.instances.count(instK)==0) {
                        instanceListChanged = true;
                        break;
                    }
                }
                if (instanceListChanged) {
                    break;
                }
            }
        }
    }
    prevTable = summaryTable;
    try {
        boost::property_tree::ptree obj;
        obj.put("type", "state-summary-table");
        obj.put("service_list_changed", serviceListChanged);
        obj.put("instance_list_changed", instanceListChanged);
        boost::property_tree::ptree services;
        for (const auto& [service, summary]: summaryTable) {
            boost::property_tree::ptree s;
            s.put("service", service);
            s.put("date", summary.date);
            s.put("n_instances", summary.instances.size());
            boost::property_tree::ptree countList;
            for (auto i=0; i<NStates; ++i) {
                boost::property_tree::ptree cnt;
                cnt.put("state-id", i);
                cnt.put("name", fair::mq::GetStateName(static_cast<fair::mq::State>(i)));
                cnt.put("value", summary.counts[i]);
                countList.push_back(std::make_pair("", cnt));
            }
            s.add_child("counts", countList);

            boost::property_tree::ptree instList;
            for (const auto& [instName, istate] : summary.instances) {
                boost::property_tree::ptree inst;
                inst.put("service", service);
                inst.put("instance", instName);
                inst.put("state", istate.state);
                inst.put("date", istate.date);
                instList.push_back(std::make_pair("", inst));
            }
            s.add_child("instances", instList);

            services.push_back(std::make_pair("", s));
        }
        obj.add_child("services", services);
        const auto& str = to_string(obj);
        LOG(debug) << __func__ << " obj(state-summary-table) = " << str;
        Send(0, str);
    } catch (const std::exception &e) {
        LOG(error) << __func__ << " caught exception: what() = " << e.what();
    } catch (...) {
        LOG(error) << __func__ << " unknown exception";
    }
}

//_____________________________________________________________________________
void WebGui::SendWebSocketIdList(const std::vector<std::pair<unsigned int, std::string>> &v)
{
    std::string msg{"WebSocket Connected ID: Date<br>"};

    for (const auto &[id, t] : v) {
        msg += " " + std::to_string(id) + " : " + t + "<br>";
    }
    LOG(debug) << __func__ << " " << msg;
    Send(0, msg.data());
}

//_____________________________________________________________________________
void WebGui::SubscribeToRedisPubSub()
{
    //std::cout << __func__ << std::endl;
    auto sub = fClient->subscriber();

    sub.on_message([this](auto channel, auto msg) {
        //std::cout << MyClass << " on_message(MESSAGE): channel = " << channel << ", msg = " << msg << std::endl;
        if (daq::service::StateChannelName.data() == channel) {
            const auto& obj = to_json(msg) ;
            const auto& cmdValue = obj. template get_optional<std::string>("value");
            if (!cmdValue) {
                LOG(error) << MyClass << ":" << __LINE__ << " on_message: missing command value";
                return;
            }
        } else if (channel == fRedisKeyEventChannelName) {
            std::cout << MyClass << " on_message(): expired key = " << msg << std::endl;
            std::thread t([this, msg = std::move(msg)]() {
                ProcessExpiredKey(msg);
            });
            t.detach();
        }
    });

    LOG(info) << "subscribe to redis pub/sub channel for DAQ state transition command: " << daq::service::StateChannelName.data();
    LOG(info) << "subscribe to redis key-event : " << fRedisKeyEventChannelName;
    sub.subscribe({std::string(daq::service::StateChannelName.data()), fRedisKeyEventChannelName});

    while (true) {
        try {
            sub.consume();
        } catch (const sw::redis::TimeoutError &e) {
            // try again.
        } catch (const sw::redis::Error &e) {
            LOG(error) << MyClass << "::" << __func__ << ": error in consume(): " << e.what();
            break;
        } catch (const std::exception& e) {
            LOG(error) << MyClass << "::" << __func__ << ": error in consume(): " << e.what();
            break;
        } catch (...) {
            LOG(error) << MyClass << "::" << __func__ << ": unknown exception";
            break;
        }
    }
    LOG(error) << MyClass << "::" << __func__ << " exit";
}

//_____________________________________________________________________________
void WebGui::Wait(const std::vector<std::string> &keys, const std::vector<std::string>& waitStateTargets)
{
    bool done{false};
    while (!done) {
        std::unordered_set<std::string> stateKeys;
        for (const auto &k : keys) {
            auto s = daq::service::scan(*fClient, {daq::service::TopPrefix.data(), k, daq::service::FairMQStatePrefix.data()}, fSeparator);
            stateKeys.merge(s);
        }

        if (stateKeys.empty()) {
            return;
        }

        // {
        //     std::string k;
        //     for (const auto &x : stateKeys) {
        //         k += x + ", ";
        //     }
        //     LOG(debug) << " stateKeys = " << k;
        // }

        std::vector<sw::redis::OptionalString> stateValues;
        fClient->mget(stateKeys.begin(), stateKeys.end(), std::back_inserter(stateValues));

        std::vector<std::string> states;
        for (const auto & x : stateValues) {
            if (!x) {
                continue;
            }
            states.push_back(*x);
        }

        // {
        //      std::string s;
        //      for (const auto &x : states) {
        //          s += x + ", ";
        //      }
        //      LOG(debug) << " states = " << s;
        // }

        for (const auto &w : waitStateTargets) {
            if (std::all_of(states.begin(), states.end(), [&w](const auto &x) {
            return x == w;
        })) {
                done = true;
                break;
            }
        }
        std::this_thread::sleep_for(100ms);
    }
}

//_____________________________________________________________________________
void WebGui::Wait(const std::unordered_set<std::string> &services, const std::unordered_set<std::string> &instances, const std::vector<std::string> &waitStateTargets)
{

    if (services.count("all")>0) {
        Wait({daq::service::join({"*", "*"}, fSeparator)}, waitStateTargets);
    } else if (instances.count("all")>0) {
        for (const auto &service : services) {
            Wait({daq::service::join({service, "*"}, fSeparator)}, waitStateTargets);
        }
    } else {
        std::vector<std::string> keys;
        std::transform(instances.begin(), instances.end(), std::back_inserter(keys), [](const auto &x) {
            return x;
        });
        Wait(keys, waitStateTargets);
    }
    // LOG(debug) << "Wait done";
    return;
}
