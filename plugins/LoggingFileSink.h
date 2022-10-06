#ifndef DaqService_Plugins_LoggingFileSink_h
#define DaqService_Plugins_LoggingFileSink_h

// A custom file sink for FairLogger

#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <map>
#include <iostream>
#include <string>
#include <string_view>

#include <boost/algorithm/string.hpp>
#include <fmt/core.h>

#if FMT_VERSION < 60000
#include <fmt/time.h>
#else
#include <fmt/chrono.h>
#endif

#include <fairmq/FairMQLogger.h>

namespace daq::service {

class LoggingFileSink {
public:
   struct OptionKey {
      static constexpr std::string_view Prefix{"daq-log"};
      static constexpr std::string_view Severity{"daq-log-severity"};
      static constexpr std::string_view Verbosity{"daq-log-verbosity"};
   };

   LoggingFileSink()
   {
      fClassName = __func__;
#if defined(_GNU_SOURCE)
      fProcessName = program_invocation_short_name;
#endif
   }
   ~LoggingFileSink()
   {
      if (fFile.is_open()) {
         fair::Logger::RemoveCustomSink(fClassName);
      }
   }

   //___________________________________________________________________________
   static void AddOptions(boost::program_options::options_description &options)
   {
      namespace bpo = boost::program_options;
      options.add_options()
         //
         (OptionKey::Prefix.data(), bpo::value<std::string>(), "Prefix path of a custom log file")
         //
         (OptionKey::Severity.data(), bpo::value<std::string>()->default_value("debug"),
          "Log severity level (custom log file): trace, debug, info, state, warn, error, fatal, nolog")
         //
         (OptionKey::Verbosity.data(), bpo::value<std::string>()->default_value("medium"),
          "Log verbosity level (custom log file): veryhigh, high, medium, low");
   }

   //___________________________________________________________________________
   static const std::string &ClassName() { return GetInstance().fClassName; }

   //___________________________________________________________________________
   static LoggingFileSink &GetInstance()
   {
      static LoggingFileSink fgInstance;
      return fgInstance;
   }

   //___________________________________________________________________________
   static void Open(const std::string &prefix, const std::string &severityStr, const std::string &verbosityStr, const std::string &id)
   {
      if (prefix.empty()) {
         return;
      }
      std::string filename{prefix};
      auto &file = GetInstance().fFile;
      {
         const std::string lt = fmt::format("{:%Y%m%d_%H%M%S}", fmt::localtime(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));
         const auto processName = GetInstance().fProcessName;
         if (!id.empty()) {
            filename += id.data();
         } else {
            if (processName.empty()) {
               filename += "pid";
            } else {
               filename += processName + "_pid";
            }
            filename += std::to_string(getpid());
         }
         filename += "_" + lt + ".log";
         file.open(filename, std::ios_base::out | std::ios_base::app);
      }
      if (!file.good()) {
         std::cerr << __func__ << " file open fail: " << filename << std::endl;
         return;
      }

      const auto &severity = fair::Logger::fSeverityMap.at(severityStr);
      const auto &verbosity = fair::Logger::fVerbosityMap.at(verbosityStr);
      //=================================
      fair::Logger::AddCustomSink(
         ClassName(), severity,
         [&file, severity, verbosity](const std::string &content, const fair::LogMetaData &metaData) {
            if ((severity == fair::Severity::nolog) || (severity > metaData.severity)) {
               return;
            }
            if (verbosity == fair::Verbosity::veryhigh) {
               file << fmt::format("[{}]", metaData.process_name);
            }
            if (verbosity >= fair::Verbosity::high) {
               file << fmt::format("[{:%Y-%m-%d %H:%M:%S}.{:06}]", fmt::localtime(metaData.timestamp),
                                   metaData.us.count());
            }
            if (verbosity == fair::Verbosity::medium) {
               file << fmt::format("[{:%Y-%m-%d %H:%M:%S}]", fmt::localtime(metaData.timestamp));
            }
            if (verbosity >= fair::Verbosity::medium) {
               file << fmt::format("[{}]", metaData.severity_name);
            }
            if (verbosity == fair::Verbosity::veryhigh) {
               file << fmt::format("{}:{}:{}", metaData.file, metaData.line, metaData.func);
            }
            file << " " << content << std::endl;
         });
   }

private:
   std::string fClassName;
   std::string fProcessName;
   std::ofstream fFile;
};

} // namesapce daq::service 

#endif