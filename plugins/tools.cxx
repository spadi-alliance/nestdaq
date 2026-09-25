/** @file
 *  @brief Implements shared plugin utility functions.
 */

#include <chrono>
#include <ctime>
#include <iomanip> // for std::put_time 
#include <cstdlib>
#include <iostream>
#include <sstream>

#include <boost/asio.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <fairlogger/Logger.h>

#include "plugins/tools.h"

namespace bpo = boost::program_options;
namespace net = boost::asio;

std::string currentDate()
{
    auto time_point = std::chrono::system_clock::now();
    auto t         = std::chrono::system_clock::to_time_t(time_point);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    std::ostringstream ret;
    ret << std::put_time(&lt, "%Y-%m-%d %H:%M:%S");
    return ret.str();
}

std::string getIPv4FromHostname(const std::string& name)
{
    try {
        net::io_context ioc;
        net::ip::tcp::resolver resolver(ioc);
        net::ip::tcp::resolver::iterator end;
        auto it = std::find_if(resolver.resolve(name, "").begin(), end, [](const auto& entry) {
            return entry.endpoint().address().is_v4();
        });
        if (it != end) {
            std::stringstream ss;
            ss <<  it->endpoint().address();
            return ss.str();
        }
        LOG(error) << "could not find ipv4 address for hostname '" << name << "'";
    } catch (const std::exception &e) {
        LOG(error) << "could not resolve hostname '" << name << "', reason: " << e.what();
    }
    return "";
}

int parseCommandLine(int argc, char* argv[], // NOLINT(cppcoreguidelines-avoid-c-arrays)
                     const bpo::options_description& options,
                     bpo::variables_map& vm)
{
    int ret = EXIT_SUCCESS;
    try {
        bpo::store(bpo::parse_command_line(argc, argv, options), vm);
        bpo::notify(vm);

        if (vm.count("help") > 0) {
            throw std::runtime_error("help");
        }

    } catch (const bpo::error_with_option_name &e) {
        LOG(error) << "#Exception: boost program options error: " << e.what();
        ret = EXIT_FAILURE;
    } catch (const std::exception &e) {
        LOG(error) << "#Exception: unhandled exception: " << e.what();
        ret = EXIT_FAILURE;
    } catch (...) {
        LOG(error) << "#Exception: unknown exception ...";
        ret = EXIT_FAILURE;
    }

    if (ret != EXIT_SUCCESS) {// || argc == 1) {
        std::cout << options << '\n';
        ret = EXIT_FAILURE;
    }
    return ret;
}

boost::property_tree::ptree toJson(std::string_view s)
{
    std::istringstream iss(std::string{s});
    boost::property_tree::ptree ret;
    boost::property_tree::read_json(iss, ret); // NOLINT(clang-analyzer-optin.cplusplus.UninitializedObject)
    return ret;
}

std::string toJsonString(const boost::property_tree::ptree& pt, bool pretty)
{
    std::ostringstream oss;
    boost::property_tree::write_json(oss, pt, pretty);
    return oss.str();
}
