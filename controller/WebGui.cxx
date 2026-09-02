/** @file
 *  @brief Implements Redis-backed DAQ control and monitoring operations.
 */

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
#include <fairlogger/Logger.h>

#include "plugins/Constants.h"
#include "plugins/Functions.h"
#include "plugins/tools.h"
#include "controller/WebGui.h"

static constexpr std::string_view kMyClass{"WebGui"};
constexpr int kNumberOfStates = static_cast<int>(fair::mq::State::Exiting) + 1;

using namespace std::string_literals;
using namespace std::chrono_literals;

namespace nestdaq::run_info {
static constexpr std::string_view kPrefix{"run_info"};
static constexpr std::string_view kLatestRunNumber{"latest_run_number"};
static constexpr std::string_view kRunNumber{"run_number"};
static constexpr std::string_view kWaitDeviceReady{"wait-device-ready"};
static constexpr std::string_view kWaitReady{"wait-ready"};
static const std::unordered_set<std::string_view> kKnownRunInfoList{
    kRunNumber,
    kWaitDeviceReady,
    kWaitReady,
};
}

static const std::unordered_set<std::string_view> kKnownCommandList{
    nestdaq::fairmq::command::kBind,
    nestdaq::fairmq::command::kCompleteInit,
    nestdaq::fairmq::command::kConnect,
    nestdaq::fairmq::command::kEnd,
    nestdaq::fairmq::command::kInitDevice,
    nestdaq::fairmq::command::kInitTask,
    nestdaq::fairmq::command::kResetDevice,
    nestdaq::fairmq::command::kResetTask,
    nestdaq::fairmq::command::kRun,
    nestdaq::fairmq::command::kStop,
    nestdaq::daq::command::kExit,
    nestdaq::daq::command::kQuit,
    nestdaq::daq::command::kReset,
    nestdaq::daq::command::kStart,
};

static const std::vector<std::string> kWaitDeviceReadyTargets {
    GetStateName(fair::mq::State::DeviceReady),
    GetStateName(fair::mq::State::Ready),
    GetStateName(fair::mq::State::Running),
};

static const std::vector<std::string> kWaitReadyTargets {
    GetStateName(fair::mq::State::Ready),
    GetStateName(fair::mq::State::Running),
};

std::string getRedisDbNumber(const std::string& uri)
{
    //                      scheme    ://host      :port (/db)
    std::regex pattern{R"(^([^:\/?#]+)://([^\/?#]+):(\d+)/?(\d*))"};
    std::smatch match_result;
    if (std::regex_match(uri, match_result, pattern)) {
        int count{0};
        for (const auto &s : match_result) {
            LOG(debug) << count++ << " " << s;
        }
    } else {
        LOG(error) << " std::regex_match failed. uri = " << uri;
    }
    const auto &db = match_result[4].str();
    return db.empty() ? "0" : db;
}

bool WebGui::connectToRedis(std::string_view redis_uri,
                            std::string_view command_channel_name,
                            std::string_view separator)
{
    // setup redis client
    if (redis_uri.empty()) {
        throw std::runtime_error("redis server uri is not specified.");
    }
    fClient = std::make_shared<sw::redis::Redis>(redis_uri.data());
    if (!fClient) {
        LOG(error) << " failed to connect to redis";
        return false;
    }
    LOG(info) << "connected to redis";
    fChannelName = command_channel_name.data();
    fSeparator = separator.data();
    fClient->command("client", "setname", kMyClass.data());

    // E: Enable key-event notification, published with "__keyevent@<db>__" prefix
    // x: Expired events (events generated every time a key expires)
    fClient->command("config", "set", "notify-keyspace-events", "AKE");
    const auto &db = getRedisDbNumber(redis_uri.data());
    fRedisKeyEventChannelName = "__keyevent@"s + db + "__:expired"s;

    fRedisPubSubListenThread = std::thread([this]() {
        subscribeToRedisPubSub();
    });
    fRedisPubSubListenThread.detach();

    fStatePollThread = std::thread([this]() {
        pollState();
    });
    fStatePollThread.detach();
    return true;
}

// read/write operation on redis and send the value to the web client
void WebGui::copyLatestRunNumber(unsigned int conn_id)
{
    LOG(debug) << __func__ << " websocket conn_id = " << conn_id << std::endl;
    std::string name{nestdaq::run_info::kPrefix.data() + fSeparator + nestdaq::run_info::kRunNumber.data()};
    auto ret = fClient->get(name);
    if (!ret) {
        send(conn_id, {R"({ "type": "error", "value": "could not get run number from redis." })"});
        return;
    }
    name = nestdaq::run_info::kPrefix.data() + fSeparator + nestdaq::run_info::kLatestRunNumber.data();
    fClient->set(name, *ret);

    boost::property_tree::ptree obj;
    obj.put("type", "set latest_run_number");
    obj.put("value", *ret);
    const auto &reply = toJsonString(obj);
    send(conn_id, reply);
}

// increment operation on redis and send the value to the web client
void WebGui::incrementRunNumber(unsigned int conn_id)
{
    LOG(debug) << __func__ << " websocket conn_id = " << conn_id << std::endl;
    std::string name{nestdaq::run_info::kPrefix.data() + fSeparator + nestdaq::run_info::kRunNumber.data()};

    auto new_value = fClient->incr(name);

    boost::property_tree::ptree obj;
    obj.put("type", "set run_number");
    obj.put("value", std::to_string(new_value));
    const auto &reply = toJsonString(obj);
    send(conn_id, reply);
}

void WebGui::initializeFunctionList()
{
    addFunction({
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
                redisPublishDaqCommand(id, arg);
            }
        },

        // read from redis
        {   "redis-get", [this](auto id, const auto &arg) {
                redisGet(id, arg);
            }
        },

        // write to redis
        {   "redis-set", [this](auto id, const auto &arg) {
                redisSet(id, arg);
            }
        },

        // increment operation on redis
        {   "redis-incr", [this](auto id, const auto &arg) {
                redisIncr(id, arg);
            }
        },

    });

}

void WebGui::pollState()
{
    auto t_prev = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    while (true) {

        auto t_now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto kElapsed = static_cast<uint64_t>(t_now - t_prev);
        if (kElapsed < fPollIntervalMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(fPollIntervalMs - kElapsed));
            continue;
        }
        t_prev = t_now;

        std::map<std::string, ServiceState> summary_table;
        const auto &state_keys = nestdaq::daq::service::scan(*fClient, {nestdaq::daq::service::kTopPrefix.data(), "*", "*", nestdaq::daq::service::kFairMQStatePrefix.data()}, fSeparator);
        if (state_keys.empty()) {
            sendStateSummary(summary_table);
            continue;
        }
        std::vector<sw::redis::OptionalString> state_values;
        fClient->mget(state_keys.begin(), state_keys.end(), std::back_inserter(state_values));

        const auto &update_time_keys = nestdaq::daq::service::scan(*fClient, {nestdaq::daq::service::kTopPrefix.data(), "*", "*", nestdaq::daq::service::kUpdateTimePrefix.data()}, fSeparator);
        std::vector<sw::redis::OptionalString> update_time_values;
        if (!update_time_keys.empty()) {
            fClient->mget(update_time_keys.begin(), update_time_keys.end(), std::back_inserter(update_time_values));
        }

        int i=0;
        for (const auto &k : state_keys) {
            std::vector<std::string> res;
            boost::split(res, k, boost::is_any_of(fSeparator));
            const auto &service_name = res[1];
            const auto &inst_name = res[2];
            auto &ss = summary_table[service_name];
            auto &inst = ss.instances[inst_name];
            if (state_values[i]) {
                inst.state = *state_values[i];
            } else {
                inst.state = GetStateName(fair::mq::State::Undefined);
            }
            ++i;
        }

        i=0;
        for (const auto &k : update_time_keys) {
            std::vector<std::string> res;
            boost::split(res, k, boost::is_any_of(fSeparator));
            const auto &service_name = res[1];
            const auto &inst_name = res[2];
            if (summary_table.count(service_name)==0) {
                ++i;
                continue;
            }
            auto &ss = summary_table[service_name];
            if (ss.instances.count(inst_name)==0) {
                ++i;
                continue;
            }
            auto &inst = ss.instances[inst_name];
            if (update_time_values[i]) {
                inst.date = *update_time_values[i];
            }
            ++i;
        }

        for (auto &[sname, ss] : summary_table) {
            ss.counts.resize(kNumberOfStates, 0);
            for (const auto& [inst_name, inst] : ss.instances) {
                if (!inst.state.empty()) {
                    auto istate = static_cast<int>(fair::mq::GetState(inst.state));
                    if (istate >= kNumberOfStates) {
                        LOG(error) << __func__ << " bad state id = " << istate << ": service = " << sname << ", instance = " << inst_name;
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
        sendStateSummary(summary_table);
    } // while ()
}

void WebGui::processData(unsigned int conn_id,
                         const std::string& arg)
{
    std::scoped_lock<std::mutex> lock{fMutex};
    LOG(debug) << __func__ << " websocket conn_id = " << conn_id << " : arg =  " << arg;
    const auto &obj = toJson(arg);
    const auto& key = obj.get_optional<std::string>("command");
    if (key) {
        LOG(debug) << __func__ << " key (function) = " << key.get();
        fFuncList[key.get()](conn_id, obj);
    }
//  for (auto& f : fFuncList) {
//    f(conn_id, obj);
//  }
}

void WebGui::processExpiredKey(std::string_view key)
{
    LOG(trace) << __func__ << ":" << __LINE__ << " " << key;
    try {
        if (key.find("presence")!=std::string_view::npos) {
            const auto kServiceBegin = key.find(':');
            if (kServiceBegin == std::string_view::npos) {
                return;
            }
            const auto kInstanceBegin = key.find(':', kServiceBegin + 1);
            if (kInstanceBegin == std::string_view::npos) {
                return;
            }
            const auto kPresenceBegin = key.find(':', kInstanceBegin + 1);
            if (kPresenceBegin == std::string_view::npos) {
                return;
            }
            const auto kServiceName = std::string{key.substr(kServiceBegin + 1, kInstanceBegin - kServiceBegin - 1)};
            const auto kInstName    = key.substr(kInstanceBegin + 1, kPresenceBegin - kInstanceBegin - 1);
            const auto kIndexBegin  = kInstName.find('-');
            if (kIndexBegin == std::string_view::npos) {
                return;
            }
            const auto kInstIndex   = std::string{kInstName.substr(kIndexBegin + 1)};
            {
                const auto& instance_index_key = nestdaq::daq::service::join({nestdaq::daq::service::kTopPrefix.data(), nestdaq::daq::service::kServiceInstanceIndexPrefix.data(), kServiceName}, fSeparator);
                fClient->hdel(instance_index_key, kInstIndex);
                LOG(warn) << " delete instance index: key = " << instance_index_key << ", field = " << kInstIndex;
            }
        }
    } catch (const std::exception &e) {
        LOG(error) << __func__ << " e.what() = " << e.what();
    } catch (...) {
        LOG(error) << __func__ << " unknown exception";
    }
}

// read operation on redis and send the value to the web client
void WebGui::readLatestRunNumber(unsigned int conn_id)
{
    LOG(debug) << __func__ << " websocket conn_id = " << conn_id;
    std::string name{nestdaq::run_info::kPrefix.data() + fSeparator + nestdaq::run_info::kLatestRunNumber.data()};
    auto ret = fClient->get(name);
    if (!ret) {
        send(conn_id, {R"({ "type": "error", "value": "could not get latest run number from redis." })"});
        return;
    }
    boost::property_tree::ptree obj;
    obj.put("type", "set latest_run_number");
    obj.put("value", *ret);
    const auto &reply = toJsonString(obj);
    send(conn_id, reply);
}

// read operation on redis and send the value to the web client
void WebGui::readRunNumber(unsigned int conn_id)
{
    LOG(debug) << __func__ << " websocket conn_id = " << conn_id;
    std::string name{nestdaq::run_info::kPrefix.data() + fSeparator + nestdaq::run_info::kRunNumber.data()};
    auto ret = fClient->get(name);
    if (!ret) {
        send(conn_id, {R"({ "type": "error", "value": "could not get run number from redis." })"});
        return;
    }
    boost::property_tree::ptree obj;
    obj.put("type", "set run_number");
    obj.put("value", *ret);
    const auto &reply = toJsonString(obj);
    send(conn_id, reply);
}

void WebGui::redisGet(unsigned int conn_id, const boost::property_tree::ptree &arg)
{
    LOG(debug) << __func__ << " websocket conn_id = " << conn_id;
    const auto &val = arg.get_optional<std::string>("value");
    if (val) {
        if (*val=="run_number") {
            readRunNumber(conn_id);
            readLatestRunNumber(conn_id);
        }
    }
}

void WebGui::redisIncr(unsigned int conn_id, const boost::property_tree::ptree &arg)
{
    const auto& val = arg.get_optional<std::string>("value");
    if (val) {
        if (*val=="run_number") {
            incrementRunNumber(conn_id);
        }
    }
}

// publish command via redis
void WebGui::redisPublishDaqCommand(unsigned int conn_id, const boost::property_tree::ptree& arg)
{
    auto is_wait_flag_set = [this](const auto &s) {
        auto w = fClient->get(nestdaq::run_info::kPrefix.data() + fSeparator + s);
        if (!w) {
            return false;
        }
        const auto &v = boost::to_lower_copy(*w);
        return (v == "1") || (v == "true");
    };
    auto to_message = [&arg](const auto &v) {
        boost::property_tree::ptree cmd;
        cmd.put("command", "change_state");
        cmd.put("value", v);
        cmd.add_child("services", arg.get_child("services"));
        cmd.add_child("instances", arg.get_child("instances"));
        //cmd.put("service", "all");
        //cmd.put("instance", "all");
        return toJsonString(cmd);
    };

    const auto& arg_str = toJsonString(arg);
    LOG(debug) << __func__ << " arg = " << arg_str;
    const auto &val = arg.get_optional<std::string>("value");
    if (!val) {
        LOG(error) << " value is missing.";
        return;
    }

    const auto& v= *val;
    if (v == nestdaq::fairmq::command::kRun.data()) {
        copyLatestRunNumber(conn_id);
    }
    if (kKnownCommandList.count(v)>0) {
        LOG(debug) << " conn_id = " << conn_id;

        try {

            bool wait_device_ready_flag = is_wait_flag_set(nestdaq::run_info::kWaitDeviceReady.data());
            bool wait_ready_flag       = is_wait_flag_set(nestdaq::run_info::kWaitReady.data());
            std::unordered_set<std::string> services;
            for (const auto& x : arg.get_child("services")) {
                services.emplace(x.second. template get_value<std::string>());
            }
            std::unordered_set<std::string> instances;
            for (const auto& x : arg.get_child("instances")) {
                instances.emplace(x.second. template get_value<std::string>());
            }

            // use boost::iequals for case insensitive compare
            if (boost::iequals(v, nestdaq::fairmq::command::kConnect)) {
                fClient->publish(fChannelName, to_message(nestdaq::fairmq::command::kConnect));
                if (wait_device_ready_flag) {
                    wait(services, instances, kWaitDeviceReadyTargets);
                }

            } else if (boost::iequals(v, nestdaq::fairmq::command::kInitTask)) {
                if (wait_device_ready_flag) {
                    fClient->publish(fChannelName, to_message(nestdaq::fairmq::command::kConnect));
                    wait(services, instances, kWaitDeviceReadyTargets);
                }
                fClient->publish(fChannelName, to_message(nestdaq::fairmq::command::kInitTask));
                if (wait_ready_flag) {
                    wait(services, instances, kWaitReadyTargets);
                }

            } else if (boost::iequals(v, nestdaq::fairmq::command::kRun)) {
                if (wait_device_ready_flag) {
                    fClient->publish(fChannelName, to_message(nestdaq::fairmq::command::kConnect));
                    wait(services, instances, kWaitDeviceReadyTargets);
                }
                if (wait_ready_flag) {
                    fClient->publish(fChannelName, to_message(nestdaq::fairmq::command::kInitTask));
                    wait(services, instances, kWaitReadyTargets);
                }
                LOG(debug) << " pre-run = " << fPreRunCommand;
                boost::process::system(fPreRunCommand.data(), boost::process::std_out > stdout, boost::process::std_err > stderr, boost::process::std_in < stdin);
                fClient->publish(fChannelName, to_message(nestdaq::fairmq::command::kRun));
                LOG(debug) << " post-run = " << fPostRunCommand;
                boost::process::system(fPostRunCommand.data(), boost::process::std_out > stdout, boost::process::std_err > stderr, boost::process::std_in < stdin);

            } else if (boost::iequals(v,  nestdaq::fairmq::command::kStop)) {
                LOG(debug) << " pre-stop = " << fPreStopCommand;
                boost::process::system(fPreStopCommand.data(), boost::process::std_out > stdout, boost::process::std_err > stderr, boost::process::std_in < stdin);
                fClient->publish(fChannelName, to_message(nestdaq::fairmq::command::kStop));
                LOG(debug) << " post-stop = " << fPostStopCommand;
                boost::process::system(fPostStopCommand.data(), boost::process::std_out > stdout, boost::process::std_err > stderr, boost::process::std_in < stdin);

            } else {
                fClient->publish(fChannelName, to_message(v));
            }
        } catch (const std::exception &e) {
            LOG(error) << __func__ << " e.what() = " << e.what();
        } catch (...) {
            LOG(error) << __func__ << " unknown exception";
        }
    }

}

void WebGui::redisSet(unsigned int conn_id, const boost::property_tree::ptree &arg)
{
    LOG(debug) <<  __func__ << " " << conn_id;
    const auto &name = arg.get_optional<std::string>("name");
    if (name) {
        if (nestdaq::run_info::kKnownRunInfoList.count(*name)>0) {

            auto val = arg.get_optional<std::string>("value");
            if (!val) {
                LOG(error) << kMyClass << " " << __func__ << " parse error ";
                return;
            }
            std::string key{nestdaq::run_info::kPrefix.data() + fSeparator + *name};
            fClient->set(key, *val);
        }
    }
}

void WebGui::sendStateSummary(const std::map<std::string, ServiceState> & summary_table)
{
    static std::map<std::string, ServiceState> gPrevTable;
    bool service_list_changed = false;
    bool instance_list_changed = false;
    if (gPrevTable.size() != summary_table.size()) {
        service_list_changed = true;
        instance_list_changed = true;
    } else {
        for (const auto& [k, v] : summary_table) {
            if (gPrevTable.count(k)==0) {
                service_list_changed = true;
                instance_list_changed = true;
                break;
            }
        }
        if (!service_list_changed) {
            for (const auto& [k, v] : summary_table) {
                const auto& srv = gPrevTable[k];
                if (srv.instances.size()!=v.instances.size()) {
                    instance_list_changed = true;
                    break;
                }
                for (const auto &[instK, instV] : v.instances) {
                    if (srv.instances.count(instK)==0) {
                        instance_list_changed = true;
                        break;
                    }
                }
                if (instance_list_changed) {
                    break;
                }
            }
        }
    }
    gPrevTable = summary_table;
    try {
        boost::property_tree::ptree obj;
        obj.put("type", "state-summary-table");
        obj.put("service_list_changed", service_list_changed);
        obj.put("instance_list_changed", instance_list_changed);
        boost::property_tree::ptree services;
        for (const auto& [service, summary]: summary_table) {
            boost::property_tree::ptree s;
            s.put("service", service);
            s.put("date", summary.date);
            s.put("n_instances", summary.instances.size());
            boost::property_tree::ptree count_list;
            for (auto i=0; i<kNumberOfStates; ++i) {
                boost::property_tree::ptree cnt;
                cnt.put("state-id", i);
                cnt.put("name", fair::mq::GetStateName(static_cast<fair::mq::State>(i)));
                cnt.put("value", summary.counts[i]);
                count_list.push_back(std::make_pair("", cnt));
            }
            s.add_child("counts", count_list);

            boost::property_tree::ptree inst_list;
            for (const auto& [inst_name, istate] : summary.instances) {
                boost::property_tree::ptree inst;
                inst.put("service", service);
                inst.put("instance", inst_name);
                inst.put("state", istate.state);
                inst.put("date", istate.date);
                inst_list.push_back(std::make_pair("", inst));
            }
            s.add_child("instances", inst_list);

            services.push_back(std::make_pair("", s));
        }
        obj.add_child("services", services);
        const auto& str = toJsonString(obj);
        LOG(debug) << __func__ << " obj(state-summary-table) = " << str;
        send(0, str);
    } catch (const std::exception &e) {
        LOG(error) << __func__ << " caught exception: what() = " << e.what();
    } catch (...) {
        LOG(error) << __func__ << " unknown exception";
    }
}

void WebGui::sendWebSocketIdList(const std::vector<std::pair<unsigned int, std::string>> &v)
{
    std::string msg{"WebSocket Connected ID: Date<br>"};

    for (const auto &[id, t] : v) {
        msg += " " + std::to_string(id) + " : " + t + "<br>";
    }
    LOG(debug) << __func__ << " " << msg;
    send(0, msg);
}

void WebGui::subscribeToRedisPubSub()
{
    //std::cout << __func__ << std::endl;
    auto sub = fClient->subscriber();

    sub.on_message([this](auto channel, auto msg) {
        //std::cout << kMyClass << " on_message(MESSAGE): channel = " << channel << ", msg = " << msg << std::endl;
        if (nestdaq::daq::service::kStateChannelName.data() == channel) {
            const auto& obj = toJson(msg) ;
            const auto& cmd_value = obj. template get_optional<std::string>("value");
            if (!cmd_value) {
                LOG(error) << kMyClass << ":" << __LINE__ << " on_message: missing command value";
                return;
            }
        } else if (channel == fRedisKeyEventChannelName) {
            LOG(debug) << kMyClass << " on_message(): expired key = " << msg;
            std::thread t([this, msg = std::move(msg)]() {
                processExpiredKey(msg);
            });
            t.detach();
        }
    });

    LOG(info) << "subscribe to redis pub/sub channel for DAQ state transition command: " << nestdaq::daq::service::kStateChannelName.data();
    LOG(info) << "subscribe to redis key-event : " << fRedisKeyEventChannelName;
    sub.subscribe({std::string(nestdaq::daq::service::kStateChannelName.data()), fRedisKeyEventChannelName});

    while (true) {
        try {
            sub.consume();
        } catch (const sw::redis::TimeoutError &e) {
            // try again.
        } catch (const sw::redis::Error &e) {
            LOG(error) << kMyClass << "::" << __func__ << ": error in consume(): " << e.what();
            break;
        } catch (const std::exception& e) {
            LOG(error) << kMyClass << "::" << __func__ << ": error in consume(): " << e.what();
            break;
        } catch (...) {
            LOG(error) << kMyClass << "::" << __func__ << ": unknown exception";
            break;
        }
    }
    LOG(error) << kMyClass << "::" << __func__ << " exit";
}

void WebGui::wait(const std::vector<std::string> &keys, const std::vector<std::string>& wait_state_targets)
{
    bool done{false};
    while (!done) {
        std::unordered_set<std::string> state_keys;
        for (const auto &k : keys) {
            auto s = nestdaq::daq::service::scan(*fClient, {nestdaq::daq::service::kTopPrefix.data(), k, nestdaq::daq::service::kFairMQStatePrefix.data()}, fSeparator);
            state_keys.merge(s);
        }

        if (state_keys.empty()) {
            return;
        }

        // {
        //     std::string k;
        //     for (const auto &x : state_keys) {
        //         k += x + ", ";
        //     }
        //     LOG(debug) << " state_keys = " << k;
        // }

        std::vector<sw::redis::OptionalString> state_values;
        fClient->mget(state_keys.begin(), state_keys.end(), std::back_inserter(state_values));

        std::vector<std::string> states;
        for (const auto & x : state_values) {
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

        for (const auto &w : wait_state_targets) {
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

void WebGui::wait(const std::unordered_set<std::string> &services, const std::unordered_set<std::string> &instances, const std::vector<std::string> &wait_state_targets)
{

    if (services.count("all")>0) {
        wait({nestdaq::daq::service::join({"*", "*"}, fSeparator)}, wait_state_targets);
    } else if (instances.count("all")>0) {
        for (const auto &service : services) {
            wait({nestdaq::daq::service::join({service, "*"}, fSeparator)}, wait_state_targets);
        }
    } else {
        std::vector<std::string> keys;
        std::transform(instances.begin(), instances.end(), std::back_inserter(keys), [](const auto &x) {
            return x;
        });
        wait(keys, wait_state_targets);
    }
    // LOG(debug) << "wait done";
}
