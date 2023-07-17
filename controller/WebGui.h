#ifndef WebGui_h
#define WebGui_h

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

class WebGui {
public:
    using ProcessDataFunc    = std::function<void (unsigned int, const boost::property_tree::ptree&)>;
    WebGui() {
        InitializeFunctionList();
    }
    WebGui(const WebGui &) = delete;
    WebGui& operator=(const WebGui &) = delete;
    ~WebGui() {
        Send(0, "Disconnected.");
    }

    // add function to the list for ProcessData
    void AddFunction(const std::string& command, ProcessDataFunc f) {
        fFuncList.emplace(command, f);
    }
    void AddFunction(const std::unordered_map<std::string, ProcessDataFunc>& table) {
        fFuncList.insert(table.cbegin(), table.cend());
    }

    // update the list of the client's connection id
    void AddWebSocketId(unsigned int connid);

    bool ConnectToRedis(std::string_view redisUri,
                        std::string_view commandChanenlName,
                        std::string_view separator);

    // read/write operation on redis and send the value to the web client
    void CopyLatestRunNumber(unsigned int connid);

    const std::string& GetChannelPrefix() const {
        return fChannelName;
    }
    const std::list<std::pair<unsigned int, std::string>>& GetWebSocketIdList() const  {
        return fWebSocketIdList;
    }
    const std::string& GetSeparator() const {
        return fSeparator;
    }
    std::shared_ptr<sw::redis::Redis> GetRedisClient() {
        return fClient;
    }


    void InitializeFunctionList();
    void ProcessData(unsigned int connid, const std::string& arg);
    void RemoveWebSocketId(unsigned int connid);
    void SaveRDB(const std::string &runNumber);

    // send message to the web client/clients
    void Send(unsigned int connid, const std::string& arg) {
        fSend(connid, arg);
    }

    // Send the list of the client's connection id
    void SendWebSocketIdList();

    void SetDBDir(std::string_view value) {
        fDBDir = value.data();
    }
    void SetDBFileNameFormat(std::string_view value) {
        fDBFileNameFormat = value.data();
    }
    void SetPollIntervalMS(uint64_t t) {
        fPollIntervalMS = t;
    }
    void SetPostRunCommand(std::string_view value) {
        fPostRunCommand = value.data();
    }
    void SetPostStopCommand(std::string_view value) {
        fPostStopCommand = value.data();
    }
    void SetPreRunCommand(std::string_view value) {
        fPreRunCommand = value.data();
    }
    void SetPreStopCommand(std::string_view value) {
        fPreStopCommand = value.data();
    }
    void SetSaveCommand(std::string_view value) {
        fSaveCommand = value.data();
    }
    void SetSendFunction(std::function<void (unsigned int, const std::string&)> f) {
        fSend = f;
    }
    void SetTerminateFunction(std::function<void (void)> f) {
        fTerminate = f;
    }

    // terminate this webgui daq controller
    void Terminate() {
        fTerminate();
    }

private:
    // increment operation on redis and send the result to the web client
    void IncrementRunNumber(unsigned int connid);
    void PollState();
    void ProcessExpiredKey(std::string_view key);
    // read operation on redis (and send the returned value to the web client)
    void ReadCommandChannel(unsigned int connid);
    void ReadLatestRunNumber(unsigned int connid);
    void ReadRunNumber(unsigned int connid);
    void RedisGet(unsigned int connid, const boost::property_tree::ptree &arg);
    void RedisIncr(unsigned int connid, const boost::property_tree::ptree &arg);
    // send command via redis pub/sub channels
    void RedisPublishDaqCommand(unsigned int connid, const boost::property_tree::ptree& arg);
    void RedisSet(unsigned int connid, const boost::property_tree::ptree& arg);
    void SendStateSummary(const std::map<std::string, ServiceState> &summaryTable);
    void SubscribeToRedisPubSub();
    // write operation on redis
    void WriteRunNumber(unsigned int connid, const boost::property_tree::ptree& arg);

    std::mutex fMutex;
    std::unordered_map<std::string, ProcessDataFunc> fFuncList;
    std::list<std::pair<unsigned int, std::string>> fWebSocketIdList;

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
    uint64_t fPollIntervalMS{0};

    std::string fDBDir;
    std::string fDBFileNameFormat;
    std::string fSaveCommand;
    bool fRecreateTS;
};


#endif