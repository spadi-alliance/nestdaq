#pragma once

/**
 * @file Functions.h
 * @brief Small Redis and string helper functions used by DAQ service plugins.
 */

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <sw/redis++/redis++.h>

namespace nestdaq::daq::service {

/**
 * @brief Join Redis key components with the configured separator.
 */
inline std::string join(const std::vector<std::string> &v, std::string_view separator)
{
    return boost::join(v, separator.data());
}

/**
 * @brief Scan Redis keys matching @p pattern and return all matches.
 */
inline std::unordered_set<std::string> scan(sw::redis::Redis &r,
        std::string_view pattern,
        sw::redis::Cursor cursor=0)
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

/**
 * @brief Build a scan pattern from key components and scan Redis for matches.
 */
inline std::unordered_set<std::string> scan(sw::redis::Redis &r,
        const std::vector<std::string>& v,
        std::string_view separator,
        sw::redis::Cursor cursor=0)
{
    return scan(r, boost::join(v, separator.data()), cursor);
}

} // namespace nestdaq::daq::service
