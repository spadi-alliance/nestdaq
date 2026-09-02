#pragma once

/**
 * @file Constants.h
 * @brief Shared Redis key names and FairMQ command strings used by plugins.
 */

#include <string_view>

namespace nestdaq::daq::service {

/** @brief Top-level Redis namespace for DAQ service data. */
static constexpr std::string_view kTopPrefix{"daq_service"};
/** @brief Redis namespace for live service presence keys. */
static constexpr std::string_view kPresencePrefix{"presence"};
/** @brief Redis namespace for service health hashes. */
static constexpr std::string_view kHealthPrefix{"health"};
/** @brief Redis namespace for FairMQ state values. */
static constexpr std::string_view kFairMQStatePrefix{"fair-mq-state"};
/** @brief Redis field/key prefix for last update timestamps. */
static constexpr std::string_view kUpdateTimePrefix{"updatedTime"};
/** @brief Redis namespace for published FairMQ program options. */
static constexpr std::string_view kProgOptionPrefix{"option"};
/** @brief Redis key prefix used for service instance index bookkeeping. */
static constexpr std::string_view kServiceInstanceIndexPrefix{"service-instance-index"};

static constexpr std::string_view kSeparator{"separator"};
static constexpr std::string_view kServiceName{"service-name"};
static constexpr std::string_view kServiceRegistryUri{"registry-uri"};

static constexpr std::string_view kRunInfoPrefix{"run_info"};
static constexpr std::string_view kRunNumber{"run_number"};
static constexpr std::string_view kStartTime{"start_time"};
static constexpr std::string_view kStartTimeNs{"start_time_ns"};
static constexpr std::string_view kStopTime{"stop_time"};
static constexpr std::string_view kStopTimeNs{"stop_time_ns"};

static constexpr std::string_view kUuid{"uuid"};
static constexpr std::string_view kMaxTtl{"max-ttl"};
static constexpr std::string_view kTtlUpdateInterval{"ttl-update-interval"};
static constexpr std::string_view kHostIpAddress{"host-ip"};
static constexpr std::string_view kHostname{"hostname"};
static constexpr std::string_view kCommandChannelName{"daqctl"}; // daq command pubsub channel (controller -> FairMQ Deivce)
static constexpr std::string_view kStateChannelName{"daqstate"}; // daq command pubsub channel (controller <- FairMQ Deivce)
}

namespace nestdaq::fairmq::command {
/** @brief FairMQ command string constants accepted by the service plugin. */
static constexpr std::string_view kBind{"BIND"};
static constexpr std::string_view kCompleteInit{"COMPLETE INIT"};
static constexpr std::string_view kConnect{"CONNECT"};
static constexpr std::string_view kEnd{"END"};
static constexpr std::string_view kInitDevice{"INIT DEVICE"};
static constexpr std::string_view kInitTask{"INIT TASK"};
static constexpr std::string_view kResetDevice{"RESET DEVICE"};
static constexpr std::string_view kResetTask{"RESET TASK"};
static constexpr std::string_view kRun{"RUN"};
static constexpr std::string_view kStop{"STOP"};

}

namespace nestdaq::daq::command {
/** @brief Redis pub/sub channel used for DAQ control commands. */
static constexpr std::string_view kChannel{"daqctl"};
static constexpr std::string_view kExit{"exit"};
static constexpr std::string_view kQuit{"quit"};
static constexpr std::string_view kReset{"reset"};
static constexpr std::string_view kStart{"start"};
}
