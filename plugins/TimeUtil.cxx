/** @file
 *  @brief Implements time conversion helpers used by plugins.
 */

#include <ctime>
#include <iomanip> // std::put_time
#include <iostream>
#include <sstream>

#include "plugins/TimeUtil.h"

namespace nestdaq::daq::service {

const std::string toDate(const std::chrono::system_clock::time_point &p)
{
    std::time_t t = std::chrono::system_clock::to_time_t(p);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    std::ostringstream ret;
    ret << std::put_time(&lt, "%Y-%m-%dT%H:%M:%S");

    return ret.str();
}

auto updateDate(const std::chrono::system_clock::time_point &s,
                const std::chrono::steady_clock::time_point &t)
-> const std::pair<std::chrono::nanoseconds, std::chrono::system_clock::time_point>
{
    auto uptime_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t);
    return {uptime_nsec, (s + std::chrono::duration_cast<std::chrono::nanoseconds>(uptime_nsec))};
}

} // namespace nestdaq::daq::service
