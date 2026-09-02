#pragma once

/**
 * @file WebSocketHandle.h
 * @brief Global connection callbacks used by Beast WebSocket sessions.
 */

#include <memory>
#include <string>
#include <vector>

class WebSocketSession;

void handleWebSocketClose(unsigned int id);
void handleWebSocketConnect(const std::shared_ptr<WebSocketSession> &session);
void handleWebSocketRead(unsigned int id, const std::string& message);
void handleWebSocketRead(unsigned int id, const std::vector<char>& message);
void writeWebSocketMessage(unsigned int id, const std::string& message);
