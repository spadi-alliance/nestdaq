#ifndef DaqService_Plugins_Functions_h
#define DaqService_Plugins_Functions_h

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <sw/redis++/redis++.h>

namespace daq::service {

//_____________________________________________________________________________
inline std::string join(const std::vector<std::string> &v, std::string_view separator)
{
    return boost::join(v, separator.data());
}

//_____________________________________________________________________________
inline std::unordered_set<std::string> scan(sw::redis::Redis &r,
        std::string_view pattern,
        long long cursor=0LL)
{
    std::unordered_set<std::string> keys;
    while (true) {
        cursor = r.scan(cursor, pattern, std::inserter(keys, keys.begin()));
        if (cursor==0) {
            break;
        }
    }
    return keys;
}

//_____________________________________________________________________________
inline std::unordered_set<std::string> scan(sw::redis::Redis &r,
        const std::vector<std::string>& v,
        std::string_view separator,
        long long cursor=0LL)
{
    return scan(r, boost::join(v, separator.data()), cursor);
}

} // namespace daq::service

#endif