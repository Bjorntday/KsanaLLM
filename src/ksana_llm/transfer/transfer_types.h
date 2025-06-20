/* Copyright 2024 Tencent Inc.  All rights reserved.

==============================================================================*/
#pragma once

#include <string>
#include <vector>
#include "ksana_llm/utils/tensor.h"

namespace ksana_llm {

/**
 * @brief 网络地址结构
 *
 * 该结构体封装了网络通信所需的地址信息。
 */
struct NetworkAddr {
  std::string ip = "127.0.0.1";  // 服务器IP地址，默认为本地回环地址
  int port = 50051;              // 服务器端口号，默认为50051

  std::string ToString() const { return ip + ":" + std::to_string(port); }

  static NetworkAddr FromString(const std::string& addr_str) {
    NetworkAddr addr;
    size_t pos = addr_str.find(':');
    if (pos != std::string::npos) {
      addr.ip = addr_str.substr(0, pos);
      addr.port = std::stoi(addr_str.substr(pos + 1));
    }
    return addr;
  }
};

/**
 * @brief 传输张量结构，用于在设备间传输模型张量数据
 *
 * 该结构体包含了张量传输所需的各种元数据信息，如块索引、层索引、
 * 设备索引、源数据指针、形状和数据类型等。
 */
struct TransferTensor {
  int block_idx = 0;           // 块索引，标识批处理中的块位置
  int layer_idx = 0;           // 层索引，标识模型中的层位置
  int device_idx = 0;          // 设备索引，标识目标设备
  void* src_ptr = nullptr;     // 源数据指针，指向需要传输的张量数据
  std::vector<int64_t> shape;  // 张量形状，描述张量的维度
  DataType dtype;              // 数据类型

  size_t GetElementNumber() const {
    if (shape.empty()) return 0;

    size_t num_elements = 1;
    for (const auto& dim : shape) {
      num_elements *= static_cast<size_t>(dim);
    }
    return num_elements;
  }
};

/**
 * @brief 传输任务信息，封装单个张量传输任务的完整信息
 *
 * 该结构体描述了一个完整的传输任务，包括请求ID、传输的张量、
 * token信息、目标指针、完成状态和目标地址等。
 */
struct TransferTask {
  int req_id = 0;             // 请求ID，用于唯一标识传输请求
  TransferTensor tensor;      // 传输的张量数据
  int token = 0;              // 和tensor二选一传输
  void* dst_ptr = nullptr;    // 目标指针，指向接收数据的内存或显存地址
  bool is_completed = false;  // 是否完成传输，标记任务完成状态
  std::string addr;           // 目标地址
};

}  // namespace ksana_llm