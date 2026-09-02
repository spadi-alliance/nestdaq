#pragma once

/**
 * @file TopologyData.h
 * @brief Data structures for the current Redis-backed DAQ topology schema.
 */

#include <map>
#include <string>
#include <vector>

namespace nestdaq::daq::service {

/** @brief Default FairMQ socket buffer size used when topology omits one. */
static constexpr int kDefaultSocketBufferSize{1000};
/** @brief Default FairMQ socket linger value used when topology omits one. */
static constexpr int kDefaultSocketLinger{500};
/** @brief Lower bound for automatically assigned TCP ports. */
static constexpr int kDefaultPortRangeMin{22000};
/** @brief Upper bound for automatically assigned TCP ports. */
static constexpr int kDefaultPortRangeMax{32000};

/**
 * @brief Current socket/channel properties exchanged through Redis topology keys.
 */
struct SocketProperty {
    std::string name; // channel name
    std::string type;
    std::string method; // bind or connect
    std::string address;
    std::string transport{"zeromq"};
    int snd_buf_size{kDefaultSocketBufferSize};
    int rcv_buf_size{kDefaultSocketBufferSize};
    int snd_kernel_size{0};
    int rcv_kernel_size{0};
    int linger{kDefaultSocketLinger};
    int rate_logging{1};
    int port_range_min{kDefaultPortRangeMin};
    int port_range_max{kDefaultPortRangeMax};
    bool auto_bind{true};
    int num_sockets{0};

    /** @brief True when TopologyConfig should derive subchannels automatically. */
    bool auto_sub_channel{false};
    /** @brief True after a bind address has been resolved and published. */
    bool bound{false};
    /** @brief True when connect setup must wait for peer state. */
    bool wait_for_peer_connection{true};
};

/**
 * @brief Current logical link between a local channel and a peer channel.
 */
struct LinkProperty {
    std::string my_service; // near
    std::string my_channel;
    std::string peer_service; // far
    std::string peer_channel;
    std::string options;
};

using MQChannel = std::map<std::string, SocketProperty>;

} // namespace nestdaq::daq::service
