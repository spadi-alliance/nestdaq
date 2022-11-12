#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_set>

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/optional.hpp>

#include <sw/redis++/redis++.h>

#include <fairmq/States.h>
#include <fairmq/FairMQLogger.h>

#include "plugins/Constants.h"
#include "plugins/tools.h"
#include "controller/WebGui.h"

static constexpr std::string_view MyClass{"WebGui"};
constexpr int NStates = static_cast<int>(fair::mq::State::Exiting) + 1;

using namespace std::string_literals;

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

//_____________________________________________________________________________
std::string GetRedisDBNumber(const std::string& uri)
{
  //                      scheme    ://host      :port (/db)
  std::regex pattern{R"(^([^:\/?#]+)://([^\/?#]+):(\d+)/?(\d*))"};
  std::smatch matchResult;
  if (std::regex_match(uri, matchResult, pattern)) {
    int count{0};
    for (const auto &s : matchResult) {
      std::cout << count++ << " " << s << std::endl;
    }
  } else {
    std::cerr << MyClass << ":" << __LINE__ << " " << __func__ << " std::regex_match failed. uri = " << uri << std::endl;
  }
  const auto &db = matchResult[4].str();
  return db.empty() ? "0" : db;
}

//_____________________________________________________________________________
void ShowInstanceState(std::string_view service, std::string_view id, const InstanceState& inst) 
{
  // id is short style
  std::cout << " " << service << ":" << id << " " << inst.state << " " << inst.date << std::endl;
}

//_____________________________________________________________________________
void ShowServiceState(const std::map<std::string, ServiceState>& deviceStates)
{
  for (const auto& [serviceName, s] : deviceStates) {
    for (const auto& [id, inst] : s.instances) {
      ShowInstanceState(serviceName, id, inst);
    }
  }
}

//_____________________________________________________________________________
void WebGui::AddWebSocketId(unsigned int connid)
{
  auto d = date();
  fWebSocketIdList.emplace_back(connid, d);
  std::string msg{"My WebSocket Connection ID: "};
  msg += std::to_string(connid) + " (Date: " + d + ")";
  std::cout <<  __func__ << " " << msg << std::endl;
  Send(connid, msg.data());
}

//_____________________________________________________________________________
bool WebGui::ConnectToRedis(std::string_view redisUri,
                            std::string_view commandChannelName, 
                            std::string_view separator)
{
  std::cout << __func__ << std::endl;
  // setup redis client 
  if (redisUri.empty()) {
      throw std::runtime_error("redis server uri is not specified.");
  }
  fClient = std::make_shared<sw::redis::Redis>(redisUri.data());
  if (!fClient) {
    std::cerr << " failed to connect to redis" << std::endl;
    return false;
  }
  fChannelName = commandChannelName.data();
  fSeparator = separator.data();
  fClient->command("client", "setname", MyClass.data());

  // E: Enable key-event notification, published with "__keyevent@<db>__" prefix
  // x: Expired events (events generated every time a key expires)
  fClient->command("config", "set", "notify-keyspace-events", "Ex");
  const auto &db = GetRedisDBNumber(redisUri.data()); 
  fRedisKeyEventChannelName = "__keyevent@"s + db + "__:expired"s;
  std::cout << " keyevent = " << fRedisKeyEventChannelName << std::endl;

  fStateListenThread = std::thread([this]() {
    SubscribeToRedisPubSub();
  });
  fStateListenThread.detach();
  return true;
}

//_____________________________________________________________________________
// read/write operation on redis and send the value to the web client
void WebGui::CopyLatestRunNumber(unsigned int connid)
{
  std::cout << __func__ << " connid = " << connid << std::endl;
  std::string name{"run_info" + fSeparator + "run_number"};
  auto ret = fClient->get(name);
  if (!ret) {
    Send(connid, {R"({ "type": "error", "value": "could not get run number from redis." })"});
    return;
  }
  name = "run_info" + fSeparator + "latest_run_number";
  fClient->set(name, *ret);

  if (!fDBDir.empty()) {
    SaveRDB(*ret);
  }

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
  std::cout << __func__ << " connid = " << connid << std::endl;
  std::string name{"run_info" + fSeparator + "run_number"};
  
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
    {"ON_CONNECT",
      [this](auto id, const auto &arg){
        AddWebSocketId(id);
        SendWebSocketIdList();
      }
    },

    // function called on a client closed
    {"ON_CLOSED",
      [this](auto id, const auto &arg) {
        RemoveWebSocketId(id);
        SendWebSocketIdList();
      }
    },
    
    {"resetStateList", [this](auto id, const auto &arg) {
      std::lock_guard<std::mutex> lock{fStateMutex};
      fDeviceState.clear();
    } },

    // send command via redis pub/sub channels
    {"redis-publish", [this](auto id, const auto &arg) { RedisPublishDaqCommand(id, arg); } }, 

    // read from redis
    {"redis-get", [this](auto id, const auto &arg) { RedisGet(id, arg); } },

     // write to redis
    {"redis-set", [this](auto id, const auto &arg) { RedisSet(id, arg); } },

    // increment operation on redis 
    {"redis-incr", [this](auto id, const auto &arg) { RedisIncr(id, arg); } }, 

    // update local variables and send them to the web client
    {"commandTargetServiceSelect", [this](auto id, const auto &arg) { SetCommandTargetService(id, arg); } },

    // update local variables with the selected values by the web client 
    {"commandTargetInstanceSelect", [this](auto id, const auto &arg) { SetCommandTargetInstance(id, arg); } },

  });

}

//_____________________________________________________________________________
void WebGui::MakeTargetList(unsigned int connid)
{
  LOG(debug) << "make target list";
  //std::cout << __func__ << std::endl; 
  decltype(fDeviceState) devStateList;
  {
    // copy the current list
    std::lock_guard<std::mutex> lock{fStateMutex};
    devStateList = fDeviceState;
  }

  // strip prefix and instance-id to obtain a list of service name
  auto &targetServices  = fCommandTargetService[connid];
  auto &targetInstances = fCommandTargetInstance[connid]; 
  targetServices.clear();
  targetInstances.clear();

  for (const auto &[serviceName, ss] : devStateList) {
    targetServices.emplace(serviceName, true);
    for (const auto &[instName, inst] : ss.instances) {
     // insert long instance name = "service:id"
     targetInstances.emplace(serviceName + fSeparator + instName, true); 
    }
  }

  {
    boost::property_tree::ptree obj;
    obj.put("type", "selected services");
    boost::property_tree::ptree arr;
    for (const auto &[name, flag] : targetServices) {
      boost::property_tree::ptree e;
      e.put("name", name);
      e.put("selected", flag);
      arr.push_back(std::make_pair("", e));
    }
    obj.add_child("services", arr);
    const auto &reply = to_string(obj);
    //std::cout << __func__ << " " <<  reply << std::endl;
    Send(connid, reply);
  }

  {
    boost::property_tree::ptree obj;
    obj.put("type", "selected instances");
    boost::property_tree::ptree arr;
    for (const auto &[name, flag] : targetInstances) {
      boost::property_tree::ptree e;
      e.put("name", name); // long instance name
      e.put("selected", flag);
      arr.push_back(std::make_pair("", e));
    }
    obj.add_child("instances", arr);
    const auto &reply = to_string(obj);
    //std::cout << __func__ << " " << reply << std::endl;
    Send(connid, reply);
  }
}

//_____________________________________________________________________________
void WebGui::ParseDaqState(std::string_view msg) 
{
  const auto& obj = to_json(msg);
  const auto& service = obj.get_optional<std::string>("service");
  if (!service) {
    std::cerr << MyClass << ":" << __LINE__ << " " << __func__ << " service name not found" << std::endl;
    return;
  }
  const auto& id = obj.get_optional<std::string>("id");
  if (!id) {
    std::cerr << MyClass << ":" << __LINE__ << " " << __func__ << " instance id not found" << std::endl;
    return;
  }

  const auto& state = obj.get_optional<std::string>("state");
  if (!state) {
    std::cerr << MyClass << ":" << __LINE__ << " " << __func__ << " state not found" << std::endl;
    return;
  }

  const auto& date = obj.get_optional<std::string>("date");
  if (!date) {
    std::cerr << MyClass << ":" << __LINE__ << " " << __func__ << " date not found" << std::endl;
    return;
  }

  std::lock_guard<std::mutex> lock{fStateMutex};
  auto &ss = fDeviceState[*service];
  if (ss.counts.size() < NStates) {
    ss.counts.resize(NStates, 0);
  }

  // id <-- short name
  auto &inst = ss.instances[*id];
  if (!inst.state.empty()) {
    auto oldState = static_cast<int>(fair::mq::GetState(inst.state));
    --ss.counts[oldState];
  }
  auto newState = static_cast<int>(fair::mq::GetState(*state));
  ++ss.counts[newState];
  inst.state = *state;
  inst.date  = *date;


  if (ss.date.empty() || (ss.date < *date)) {
    ss.date = *date;
  }

  SendServiceStateSummary(*service, ss);
  SendInstanceState(*service, *id, inst);

  //ShowInstanceState(*service, *id, inst);
}

//_____________________________________________________________________________
void WebGui::ProcessData(unsigned int connid, 
                         const std::string& arg)
{
  std::lock_guard<std::mutex> lock{fMutex};
  //std::cout << __FILE__ << ":" << __LINE__ << " " << __func__ << " connid = " << connid << " : Get msg " << arg << std::endl;
  const auto &obj = to_json(arg);
  const auto& key = obj.get_optional<std::string>("command");
  if (key) {
    //std::cout << __func__ << " key = " << key.get() << std::endl;
    fFuncList[key.get()](connid, obj);
  }
//  for (auto& f : fFuncList) {
//    f(connid, obj);
//  }
}

//_____________________________________________________________________________
void WebGui::ProcessExpiredKey(std::string_view key) 
{
  //std::cout << __func__ << ":" << __LINE__ << " " << key << std::endl;
  if (key.find("presence")!=std::string_view::npos) {
    std::vector<std::string> v;
    boost::split(v, key.data(), boost::is_any_of(":")); // prefix:service:instance:presence
    //std::cout << __LINE__ << " v.size() = " << v.size() << std::endl;
    const auto& serviceName = v[1];
    const auto& instName    = v[2];

    std::lock_guard<std::mutex> lock{fStateMutex};
    if (fDeviceState.count(serviceName)==0) {
      std::cerr << __func__ << " unknown service expired : service = " << serviceName << " key = " << key << std::endl;
      return;
    }
    auto &ss = fDeviceState[serviceName];
    if (ss.counts.size() < NStates) {
      ss.counts.resize(NStates, 0);
    }
    if (ss.instances.count(instName)==0) {
      std::cerr << __func__ << " unknown instance expired : instance = " << instName << " key = " << key << std::endl;
      return;
    }
    auto &inst = ss.instances[instName];
    auto oldState = static_cast<int>(fair::mq::GetState(inst.state));
    --ss.counts[oldState];
    ++ss.counts[static_cast<int>(fair::mq::State::Exiting)];
    inst.state = "Exiting";
    inst.date  = date();
    ss.date    = inst.date;

    SendServiceStateSummary(serviceName, ss);
    SendInstanceState(serviceName, instName, inst);
  }
}



//_____________________________________________________________________________
// read operation on redis and send the value to the web client
void WebGui::ReadLatestRunNumber(unsigned int connid) 
{
  std::cout << __func__ << " connid = " << connid << std::endl;
  std::string name{"run_info" + fSeparator + "latest_run_number"};
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
  std::cout << __func__ << " connid = " << connid << std::endl;
  std::string name{"run_info" + fSeparator + "run_number"};
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
  std::cout << __func__ << " connid = " << connid << std::endl;
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
  const auto& arg_str = to_string(arg);
  std::cout << __func__ << " arg = " << arg_str << std::endl;
  const auto &val = arg.get_optional<std::string>("value");

  if (!val) {
    std::cerr << " value is missing." << std::endl;
    return;
  }
  if (*val == "get_state") {
    fClient->publish(fChannelName, {R"({ "command": "get_state" })"});
    return;
  }

  const auto& v= *val;
  if (v == fairmq::command::Run.data()) {
    CopyLatestRunNumber(connid);
  }
  if (knownCommandList.count(v)>0) {
    LOG(debug) << " connid = " << connid;

    if (fCommandTargetService[connid].count("all")>0) {
      boost::property_tree::ptree cmd; 
      cmd.put("command", "change_state");
      cmd.put("value", v);
      cmd.put("service", "all");
      cmd.put("instance", "all");
      const auto& command = to_string(cmd);
      //std::cout << __func__ << " command = " << command << std::endl;
      LOG(debug) << " 1: command =  " << command;
      fClient->publish(fChannelName, command);
      return;
    }

    bool isServiceSelected{false};
    if (fCommandTargetService[connid].size() < fDeviceState.size()) {
      MakeTargetList(connid);
    }
    for (const auto& [serviceName, selected] : fCommandTargetService[connid]) {
      if (selected) {
        boost::property_tree::ptree cmd; 
        cmd.put("command", "change_state");
        cmd.put("value", v);
        cmd.put("service", serviceName);
        cmd.put("instance", "all");
        const auto& command = to_string(cmd);

        //std::cout << __func__ << " command = " << command << std::endl;
        LOG(debug) << " 2: command =  " << command;
        fClient->publish(fChannelName, command);
        if (!isServiceSelected) {
          isServiceSelected = true;
        }
      }
    }

    if (isServiceSelected) {
      LOG(debug) << " service selected";
      return;
    }
    LOG(debug) << " service not selected";
    
    for (const auto& [longName, selected] : fCommandTargetInstance[connid]) {
      if (selected) {
        const auto& pos = longName.find(fSeparator);
        const auto& serviceName = longName.substr(0, pos);
        const auto& shortName = longName.substr(pos+1);
        boost::property_tree::ptree cmd; 
        cmd.put("command", "change_state");
        cmd.put("value", v);
        cmd.put("service", serviceName);
        cmd.put("instance", shortName);
        const auto& command = to_string(cmd);
        //std::cout << __func__ << " command = " << command << std::endl;
        LOG(debug) << " 3: command = " << command;
        fClient->publish(fChannelName, command);
      }
    }
        
  }

}


//_____________________________________________________________________________
void WebGui::RedisSet(unsigned int connid, const boost::property_tree::ptree &arg)
{
  std::cout <<  __func__ << " " << connid << std::endl;
  const auto &name = arg.get_optional<std::string>("name");
  if (name) {
    if (*name=="run_number") {      
      WriteRunNumber(connid, arg); 
    }
  }
}

//_____________________________________________________________________________
void WebGui::RemoveWebSocketId(unsigned int connid)
{
  std::cout <<  __func__ << " " << connid << std::endl;
  fWebSocketIdList.remove_if([connid](const auto &p) { return (connid==p.first); });
  fCommandTargetService.erase(connid);
  fCommandTargetInstance.erase(connid);

}

//_____________________________________________________________________________
void WebGui::SaveRDB(const std::string &runNumber)
{

}

//_____________________________________________________________________________
void WebGui::SendInstanceState(std::string_view service, std::string_view instName, const InstanceState& inst)
{
  boost::property_tree::ptree obj;
  obj.put("type", "instance-state");
  obj.put("service", service.data());
  obj.put("instance", instName.data()); // short name
  obj.put("state", inst.state);
  obj.put("date", inst.date);
  const auto& str = to_string(obj);
  //std::cout << " obj (instance state) = " << str << std::endl;
  Send(0, str);
}

//_____________________________________________________________________________
void WebGui::SendServiceStateSummary(std::string_view service, const ServiceState& s)
{
  boost::property_tree::ptree obj;
  obj.put("type", "state-summary");
  obj.put("service", service.data()); 
  obj.put("date", s.date);
  obj.put("n_instances", s.instances.size());
  boost::property_tree::ptree countList;
  for (auto i=0; i<NStates; ++i) {
    boost::property_tree::ptree cnt;
    cnt.put("state-id", i);
    cnt.put("name", fair::mq::GetStateName(static_cast<fair::mq::State>(i)));
    cnt.put("value", s.counts[i]);
    countList.push_back(std::make_pair("", cnt));
  }
  obj.add_child("counts", countList);
  const auto& str = to_string(obj);
  //std::cout << " obj (state-summary) = " << str << std::endl;
  Send(0, str);
}

//_____________________________________________________________________________
void WebGui::SendWebSocketIdList()
{
  std::string msg{"WebSocket Connected ID: Date<br>"};

  for (const auto &[id, t] : fWebSocketIdList) {
    msg += " " + std::to_string(id) + " : " + t + "<br>";
  }
  //std::cout << __FILE__ << ":" << __LINE__ << " " << __func__ << " " << msg << std::endl;
  Send(0, msg.data());
}

//_____________________________________________________________________________
// keep a command channel in a local variable
void WebGui::SetCommandTargetInstance(unsigned int connid, const boost::property_tree::ptree& arg)
{
  //std::cout << __func__ << " connid = " << connid << std::endl;
  const auto& items = arg.get_child("items");
  //std::cout << __func__ << " items = " << to_string(items) << std::endl;

  auto& services = fCommandTargetService[connid];
  auto& instances = fCommandTargetInstance[connid];
  if (services.size() < fDeviceState.size()) {
    MakeTargetList(connid);
  }
  for (auto &[k, v] : instances) {
    v = false;
  }

  bool isAll{false}; 

  for (const auto& item : items) {
    const auto x = item.second.get_value<std::string>();
    //std::cout << __func__ << ":" << __LINE__ << " x = " << x << std::endl;
    if (x=="all") {
      isAll = true;
      break;
    } 
    instances[x] = true;
  }

  if (!isAll) {
    for (auto &[k, v] : services) {
      v = false;
    }
  }

  //std::cout << __func__ << " selected services\n";
  //for (const auto &[k, v] : services) {
  //  if (v) {
  //    std::cout << " " << k << "\n";
  //  }   
  //}
  //std::cout << " selected instances\n";
  //for (const auto &[k, v] : instances) {
  //  if (v) {
  //    std::cout << " " << k << "\n";
  //  }
  //}
  //std::cout.flush();
}

//_____________________________________________________________________________
// keep a command channel in a local variable and send selected instances
void WebGui::SetCommandTargetService(unsigned int connid, const boost::property_tree::ptree& arg)
{
  //std::cout << __func__ << " connid = " << connid << std::endl; 

  const auto& items = arg.get_child("items");
  //std::cout << __func__ << " items = " << to_string(items) << std::endl;

  auto &services = fCommandTargetService[connid];
  if (services.size() < fDeviceState.size()) {
    MakeTargetList(connid);
  }
  auto &instances = fCommandTargetInstance[connid];
  for (auto &[k, v] : services) {
    v = false;
  }
  bool isAll{false};
  for (auto& item : items) {
    std::string x = item.second.get_value<std::string>();
    //std::cout << __func__ << ":" << __LINE__ << " x = " << x << "\n";
    if (x=="all") {
      isAll = true;
      services["all"] = true;
      break;
    } 
    services[x] = true;
  }
  //std::cout.flush();

  std::list<std::string> candidates;
  for (auto &[svcName, svcSelected] : services) {
    if (isAll) {
      svcSelected = true;
    }
    if (svcSelected) {
      for (auto &[instName, instSelected] : instances) {
        // instName = service:id (long style)
        if (instName.find(svcName)==0) {
          instSelected = true;
          candidates.push_back(instName);
        }
      }
    }
  }

  candidates.sort();
  candidates.unique();

  //std::cout << __func__ << " instances of selected services\n";
  //for (const auto &x : candidates) {
  //  std::cout << " " << x << "\n";
  //}
  //std::cout.flush();
  //candidates.push_front("all");

  {
    boost::property_tree::ptree obj;
    obj.put("type", "selected instances");
    boost::property_tree::ptree arr;
    for (const auto &name : candidates) {
      boost::property_tree::ptree e;
      e.put("name", name); // long instance name
      e.put("selected", true);
      arr.push_back(std::make_pair("", e));
    }
    obj.add_child("instances", arr);
    const auto &reply = to_string(obj);
    //std::cout << __func__ << " " << reply << std::endl;
    Send(connid, reply);
  }

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
        std::cerr << MyClass << ":" << __LINE__ << " on_message: missing command value" << std::endl;
        return; 
      }
      if (*cmdValue=="daq-state") {
        std::thread t([this, msg = std::move(msg)]() { ParseDaqState(msg); }); 
        t.detach();
      }
    } else if (channel == fRedisKeyEventChannelName) {
      std::cout << MyClass << " on_message(): expired key = " << msg << std::endl;
      std::thread t([this, msg = std::move(msg)]() { ProcessExpiredKey(msg); });
      t.detach();
    }
  });
  sub.subscribe({std::string(daq::service::StateChannelName.data()), fRedisKeyEventChannelName}); 

  while (true) {
    try {
      sub.consume();
    } catch (const sw::redis::TimeoutError &e) {
      // try again.
    } catch (const sw::redis::Error &e) {
      std::cerr << MyClass << "::" << __func__ << ": error in consume(): " << e.what() << std::endl;
      break;
    } catch (const std::exception& e) {
      std::cerr << MyClass << "::" << __func__ << ": error in consume(): " << e.what() << std::endl;
      break;
    } catch (...) {
      std::cerr << MyClass << "::" << __func__ << ": unknown exception" << std::endl;
      break;
    }
  }
  std::cerr << MyClass << "::" << __func__ << " exit" << std::endl;
}

//_____________________________________________________________________________
// write operation on redis
void WebGui::WriteRunNumber(unsigned int connid, const boost::property_tree::ptree& arg)
{
  //std::cout << __func__ << " connid = " << connid << ", arg = " << arg << std::endl;
  auto val = arg.get_optional<std::string>("value");
  if (!val) {
    std::cerr << MyClass << " " << __func__ << " parse error " << std::endl;
    return;
  }
  std::string key{"run_info" + fSeparator + "run_number"};
  fClient->set(key, *val);
}
