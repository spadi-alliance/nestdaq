#pragma once

/**
 * @file WebGui.h
 * @brief Redis-backed command handler for the web DAQ controller.
 */

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// #include <boost/json.hpp> boost.json with gcc8 has a bug
#include <boost/property_tree/ptree.hpp>

namespace sw::redis {
class Redis;
}

struct InstanceState {
    std::string state;
    std::string date;
};

struct ServiceState {
    std::map<std::string, InstanceState> instances;
    std::vector<int> counts;
    std::string date;
};

/**
 * @brief Controller-side command dispatcher and Redis state reader.
 */
class WebGui {
public:
    using ProcessDataFunc    = std::function<void (unsigned int, const boost::property_tree::ptree&)>;
    WebGui() {
        initializeFunctionList();
    }
    WebGui(const WebGui &) = delete;
    WebGui& operator=(const WebGui &) = delete;
    WebGui(WebGui&&) = delete;
    WebGui& operator=(WebGui&&) = delete;
    ~WebGui() {
        send(0, "Disconnected.");
    }

    // add function to the list for processData
    void addFunction(const std::string& command, const ProcessDataFunc& f) {
        fFuncList.emplace(command, f);
    }
    void addFunction(const std::unordered_map<std::string, ProcessDataFunc>& table) {
        fFuncList.insert(table.cbegin(), table.cend());
    }

    bool connectToRedis(std::string_view redis_uri,
                        std::string_view command_channel_name,
                        std::string_view separator);

    // read/write operation on redis and send the value to the web client
    void copyLatestRunNumber(unsigned int conn_id);

    const std::string& getChannelPrefix() const {
        return fChannelName;
    }
    const std::string& getSeparator() const {
        return fSeparator;
    }
    std::shared_ptr<sw::redis::Redis> getRedisClient() {
        return fClient;
    }

    void initializeFunctionList();
    void processData(unsigned int conn_id, const std::string& arg);

    // send message to the web client/clients
    void send(unsigned int conn_id, const std::string& arg) {
        fSend(conn_id, arg);
    }

    // send the list of the client's connection id
    void sendWebSocketIdList(const std::vector<std::pair<unsigned int, std::string>> &v);

    void setPollIntervalMs(uint64_t t) {
        fPollIntervalMs = t;
    }
    void setPostRunCommand(std::string_view value) {
        fPostRunCommand = value.data();
    }
    void setPostStopCommand(std::string_view value) {
        fPostStopCommand = value.data();
    }
    void setPreRunCommand(std::string_view value) {
        fPreRunCommand = value.data();
    }
    void setPreStopCommand(std::string_view value) {
        fPreStopCommand = value.data();
    }
    void setSendFunction(std::function<void (unsigned int, const std::string&)> f) {
        fSend = std::move(f);
    }
    void setTerminateFunction(std::function<void (void)> f) {
        fTerminate = std::move(f);
    }

    // terminate this webgui daq controller
    void terminate() {
        fTerminate();
    }

private:
    // increment operation on redis and send the result to the web client
    void incrementRunNumber(unsigned int conn_id);
    void pollState();
    void processExpiredKey(std::string_view key);
    // read operation on redis (and send the returned value to the web client)
    void readCommandChannel(unsigned int conn_id);
    void readLatestRunNumber(unsigned int conn_id);
    void readRunNumber(unsigned int conn_id);
    void redisGet(unsigned int conn_id, const boost::property_tree::ptree &arg);
    void redisIncr(unsigned int conn_id, const boost::property_tree::ptree &arg);
    // send command via redis pub/sub channels
    void redisPublishDaqCommand(unsigned int conn_id, const boost::property_tree::ptree& arg);
    void redisSet(unsigned int conn_id, const boost::property_tree::ptree& arg);
    void sendStateSummary(const std::map<std::string, ServiceState> &summary_table);
    void subscribeToRedisPubSub();
    void wait(const std::unordered_set<std::string> &services, const std::unordered_set<std::string> &instances, const std::vector<std::string> &wait_state_targets);
    void wait(const std::vector<std::string> &keys, const std::vector<std::string> &wait_state_targets);

    std::mutex fMutex;
    std::unordered_map<std::string, ProcessDataFunc> fFuncList;

    std::function<void (unsigned int, const std::string&)> fSend;
    std::function<void (void)> fTerminate;

    std::string fPreRunCommand;
    std::string fPostRunCommand;
    std::string fPreStopCommand;
    std::string fPostStopCommand;

    // for redis client
    std::string fSeparator;
    std::string fChannelName;
    std::shared_ptr<sw::redis::Redis> fClient;

    std::string fRedisKeyEventChannelName;
    std::thread fRedisPubSubListenThread;
    std::thread fStatePollThread;
    uint64_t fPollIntervalMs{0};

    bool fRecreateTs{false};
};
