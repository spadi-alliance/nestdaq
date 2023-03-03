#include <chrono>
#include <ctime>
#include <iomanip> // for std::put_time 
#include <cstdlib>
#include <iostream>
#include <sstream>

#include <boost/asio.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "plugins/tools.h"

namespace bpo = boost::program_options;
namespace net = boost::asio;

//_____________________________________________________________________________
std::string date()
{
    auto timePoint = std::chrono::system_clock::now();
    auto t         = std::chrono::system_clock::to_time_t(timePoint);
    const auto lt  = std::localtime(&t);
    std::ostringstream ret;
    ret << std::put_time(lt, "%Y-%m-%d %H:%M:%S");
    return ret.str();
}

//_____________________________________________________________________________
std::string GetIPv4FromHostname(const std::string& name)
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
        std::cerr << "could not find ipv4 address for hostname '" << name << "'";
    } catch (const std::exception &e) {
        std::cerr << "could not resolve hostname '" << name << "', reason: " << e.what() << std::endl;
    }
    return "";
}

//_____________________________________________________________________________
int ParseCommandLine(int argc, char* argv[],
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
        std::cerr << "#Exception: boost program options error: " << e.what() << std::endl;
        ret = EXIT_FAILURE;
    } catch (const std::exception &e) {
        std::cerr << "#Exception: unhandled exception: " << e.what() << std::endl;
        ret = EXIT_FAILURE;
    } catch (...) {
        std::cerr << "#Exception: unknown exception ..." << std::endl;
        ret = EXIT_FAILURE;
    }

    if (ret != EXIT_SUCCESS) {// || argc == 1) {
        std::cout << options << std::endl;
        ret = EXIT_FAILURE;
    }
    return ret;
}

//_____________________________________________________________________________
boost::property_tree::ptree to_json(std::string_view s)
{
    std::istringstream iss(s.data());
    boost::property_tree::ptree ret;
    boost::property_tree::read_json(iss, ret);
    return ret;
}

//_____________________________________________________________________________
std::string to_string(const boost::property_tree::ptree& pt)
{
    std::ostringstream oss;
    boost::property_tree::write_json(oss, pt);
    return oss.str();
}