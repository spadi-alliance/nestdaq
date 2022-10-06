#ifndef WebGui_Tools_h
#define WebGui_Tools_h

#include <string>
#include <string_view>

#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>

std::string date();
std::string GetIPv4FromHostname(const std::string& name);
int ParseCommandLine(int argc, char* argv[], 
                     const boost::program_options::options_description& options,
                     boost::program_options::variables_map& vm);

boost::property_tree::ptree to_json(std::string_view s);
std::string to_string(const boost::property_tree::ptree& obj);

#endif