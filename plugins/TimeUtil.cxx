#include <ctime>
#include <iomanip> // std::put_time
#include <iostream>
#include <sstream>

#include "plugins/TimeUtil.h"

//_____________________________________________________________________________
const std::string daq::service::to_date(const std::chrono::system_clock::time_point &p)
{
  std::time_t t = std::chrono::system_clock::to_time_t(p);
  const tm *lt = std::localtime(&t);
  std::ostringstream ret;
  ret << std::put_time(lt, "%Y-%m-%dT%H:%M:%S");

  return ret.str();
}

//_____________________________________________________________________________
auto daq::service::update_date(const std::chrono::system_clock::time_point &s, 
                               const std::chrono::steady_clock::time_point &t)
                 -> const std::pair<std::chrono::nanoseconds, std::chrono::system_clock::time_point> 
{
  auto uptimeNsec = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t);
  return {uptimeNsec, (s + std::chrono::duration_cast<std::chrono::nanoseconds>(uptimeNsec))};
}