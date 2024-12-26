/* Copyright 2024 Tencent Inc.  All rights reserved.

==============================================================================*/
#pragma once

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "ksana_llm/data_hub/schedule_output.h"
#include "ksana_llm/distributed/control_message.h"
#include "ksana_llm/distributed/node_info.h"
#include "ksana_llm/distributed/raw_packet.h"
#include "ksana_llm/distributed/raw_socket.h"
#include "ksana_llm/utils/environment.h"
#include "ksana_llm/utils/status.h"

namespace ksana_llm {

// Used to send & recv control message.
class ControlChannel {
 public:
  ControlChannel(const std::string& master_host, uint16_t master_port, size_t world_size, int node_rank,
                 PacketCreationFunc packet_creation_fn = GetPacketObject,
                 ScheduleOutputPool* schedule_output_pool = nullptr, std::shared_ptr<Environment> env = nullptr);
  ~ControlChannel();

  // For master node only.
  Status Listen();

  // Close open port.
  Status Close();

  // For slave node only.
  Status Connect();

  // disconnect from master.
  Status Disconnect();

  // Wait until all nodes arrive same location.
  Status Barrier();

  // Add node to cluster.
  Status AddNode();

  // Synchronize model layers for every node.
  Status SynchronizeNodeLayers();

  // Synchronize block num.
  Status SynchronizeCacheBlockNum();

  // Shutdown the pipeline cluster.
  Status ShutdownCluster();

 private:
  // worker to master.
  Status HandleServerPacket(NodeInfo* node_info, Packet* packet);

  // master to woker
  Status HandleClientPacket(NodeInfo* node_info, Packet* packet);

  // Add node.
  Status ProcessAddNodeRequest(NodeInfo* node_info, Packet* req_packet);
  Status ProcessAddNodeResponse(NodeInfo* node_info, Packet* rsp_packet);

  // heartbeat
  Status ProcessHeartbeatRequest(NodeInfo* node_info, Packet* req_packet);
  Status ProcessHeartbeatResponse(NodeInfo* node_info, Packet* rsp_packet);

  // Barrier
  Status ProcessBarrierRequest(NodeInfo* node_info, Packet* req_packet);
  Status ProcessBarrierResponse(NodeInfo* node_info, Packet* rsp_packet);

  // Layers
  Status ProcessLayerRequest(NodeInfo* node_info, Packet* req_packet);
  Status ProcessLayerResponse(NodeInfo* node_info, Packet* rsp_packet);

  // cache block num
  Status ProcessBlockNumRequest(NodeInfo* node_info, Packet* req_packet);
  Status ProcessBlockNumResponse(NodeInfo* node_info, Packet* rsp_packet);

  // Schedule output, from master to worker.
  Status ProcessScheduleRequest(NodeInfo* node_info, Packet* req_packet);
  Status ProcessScheduleResponse(NodeInfo* node_info, Packet* rsp_packet);

  // Process shutdown message.
  Status ProcessShutdownRequest(NodeInfo* node_info, Packet* req_packet);
  Status ProcessShutdownResponse(NodeInfo* node_info, Packet* rsp_packet);

  // heartbeat thread handle.
  Status ProcessHeartbeatLoop();

  // send schedule output to workers.
  Status ProcessSendScheduleOutputLoop();

 private:
  std::shared_ptr<RawSocket> raw_socket_ = nullptr;

  // The environments.
  std::shared_ptr<Environment> env_ = nullptr;

  // The buffer pool.
  ScheduleOutputPool* schedule_output_pool_ = nullptr;

  std::string master_host_;
  uint16_t master_port_;

  size_t world_size_;
  int node_rank_;

  // Used for barrier.
  int barrier_clock_idx_ = 0;

  // Whether the control channl is terminated..
  bool terminated_ = false;

  // The node ranks that has sent barrier message.
  std::unordered_map<int, std::unordered_set<int>> barrier_req_ranks_;

  // whether the layer have been allocated.
  bool layer_allocated_ = false;
  bool block_num_synchronized_ = false;

  // The barrier clocks that has checked by master.
  std::unordered_set<int> barrier_rsp_clocks_;

  // The shutdown nodes.
  std::unordered_set<int> shutdown_nodes_;

  // Notify for barrier utility.
  std::condition_variable barrier_cv_;
  std::condition_variable shutdown_cv_;
  std::condition_variable layer_allocation_cv_;
  std::condition_variable block_num_cv_;

  // rank to nodes and vice versa.
  std::unordered_map<int, NodeInfo> rank_nodes_;
  std::unordered_map<NodeInfo, int, NodeInfoHash, NodeInfoEqual> node_ranks_;

  // rank to data node.
  std::unordered_map<int, NodeInfo> rank_data_nodes_;

  // rank to {device_block_num, host_block_num}
  std::unordered_map<int, std::pair<size_t, size_t>> rank_cach_block_num_;

  // The heartbeat thread & timeout.
  std::shared_ptr<std::thread> heartbeat_thread_ = nullptr;
  size_t heartbeat_timeout_secs_ = 120;
  size_t heartbeat_interval_secs_ = 30;

  // rank to timestamp.
  std::unordered_map<int, time_t> node_heartbeat_timestamp_;

  // Send schedule_output async.
  std::shared_ptr<std::thread> send_packet_thread_ = nullptr;

  // Protect multi-thread receive handles.
  std::mutex mutex_;
};

}  // namespace ksana_llm
