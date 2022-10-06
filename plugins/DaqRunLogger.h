#ifndef DaqRunLogger_h
#define DaqRunLogger_h

#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <fmt/core.h>

#include <fairmq/FairMQLogger.h>


namespace daq::service {

class RunLogger {
public:
  static constexpr std::string_view MyClass{"daq::service::RunLogger"};
  struct OptionKey {
    static constexpr std::string_view RunLogPrefix{"run-log-prefix"};
    static constexpr std::string_view RunLogSeverity{"run-log-severity"};
    static constexpr std::string_view RunNumberFormat{"run-number-format"};
  };
  RunLogger(int64_t runNumber, 
            std::string_view severity,
            std::string_view prefix, 
            std::string_view form)
  {
    std::string f = fmt::format(form.data(), runNumber);
    std::string filename = prefix.data() + f + ".log";
    // open file in append mode
    fFile.open(filename, std::ofstream::out | std::ofstream::app);
    if (fFile.good()) {
      fair::Logger::AddCustomSink(MyClass.data(), severity.data(), [this](const std::string &content, const fair::LogMetaData &metaData) {
        // fFile << "process name: "     << metaData.process_name  << std::endl;
        // fFile << "microseconds us: "  << metaData.us.count()    << std::endl;
        // fFile << "timestamp: "        << metaData.timestamp     << std::endl;
        // fFile << "severity: "         << metaData.severity_name << std::endl;
        // fFile << "file: "             << metaData.file          << std::endl;
        // fFile << "line: "             << metaData.line          << std::endl;
        // fFile << "func: "             << metaData.func          << std::endl;
        // fFile << "content: "          << content                << std::endl;

        std::string yymmss;
        std::string hhmmss;
        {
           auto lt = std::localtime(&metaData.timestamp);
           std::stringstream ss;
           ss << std::put_time(std::localtime(&metaData.timestamp), "%Y/%m/%d %H:%M:%S");
           ss >> yymmss >> hhmmss;
        }

        fFile << fmt::format("[{}]",       metaData.process_name)                       //
              << fmt::format("[{}]",       yymmss)                                      //
              << fmt::format("[{}.{:06}]", hhmmss, metaData.us.count())                 //
              << fmt::format("[{}]",       metaData.severity_name)                      //
              << fmt::format("[{}:{}:{}]", metaData.file, metaData.line, metaData.func) //
              << " " << content << std::endl;

      });
    }
  }

  ~RunLogger()
  {
    if (fFile.is_open()) {
      fair::Logger::RemoveCustomSink(MyClass.data());
    }
  }

protected:
  std::ofstream fFile;
};

} // namespace daq::sesrvice

#endif