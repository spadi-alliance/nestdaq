#ifndef DaqService_Plugins_TimeUtil_h
#define DaqService_Plugins_TimeUtil_h

#include <chrono>
#include <string>
#include <utility>

namespace daq::service {

const std::string to_date(const std::chrono::system_clock::time_point &p);
auto update_date(const std::chrono::system_clock::time_point &s,
                 const std::chrono::steady_clock::time_point &t)
-> const std::pair<std::chrono::nanoseconds, std::chrono::system_clock::time_point>;

} // namespace daq::service

#endif