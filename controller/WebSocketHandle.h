#ifndef WEBSOCKET_HANDLE_H_
#define WEBSOCKET_HANDLE_H_

#include <memory>
#include <string>
#include <vector>

class websocket_session;

void OnClose(unsigned int id);
void OnConnect(const std::shared_ptr<websocket_session> &session); 
void OnRead(unsigned int id, const std::string& message);
void OnRead(unsigned int id, const std::vector<char>& data);
void Write(unsigned int id, const std::string& message);


#endif