#pragma once

#include <lite.hpp>
#include <unordered_map>

#include "DatanodeProtocol.pb.h"
#include "IpcConnectionContext.pb.h"
#include "ProtobufRpcEngine.pb.h"
#include "RpcHeader.pb.h"
#include "datatransfer.pb.h"

class Packet {
  using InputIterator = uint8_t *;

public:
  enum Type { rpc = 0, tcp = 1 };

  enum TcpType {         // related field (comment)
    Op = 0,              // opcode
    BlockOpResponse = 1, // status, block_op_response
    ReplyStatus = 2,     // status
    PacketHeader = 3,    // packet_header
    Other = 4            // (Maybe unfinished packet or data)
  };
  enum Opcode : unsigned {
    WRITE_BLOCK = 80,
    READ_BLOCK = 81,
    READ_METADATA = 82,
    REPLACE_BLOCK = 83,
    COPY_BLOCK = 84,
    BLOCK_CHECKSUM = 85,
    TRANSFER_BLOCK = 86,
    REQUEST_SHORT_CIRCUIT_FDS = 87,
    RELEASE_SHORT_CIRCUIT_FDS = 88,
    REQUEST_SHORT_CIRCUIT_SHM = 89,
    BLOCK_GROUP_CHECKSUM = 90,
    CUSTOM = 127
  };

  std::shared_ptr<std::vector<uint8_t>> buffer;

  bool request = 1;

  Type type;
  // tcp type
  TcpType tcp_type = Other;

  Opcode opcode;

  // Status
  //   SUCCESS = 0,
  //   ERROR = 1,
  //   ERROR_CHECKSUM = 2,
  //   ERROR_INVALID = 3,
  //   ERROR_EXISTS = 4,
  //   ERROR_ACCESS_TOKEN = 5,
  //   CHECKSUM_OK = 6,
  //   ERROR_UNSUPPORTED = 7,
  //   OOB_RESTART = 8,     // Quick restart
  //   OOB_RESERVED1 = 9,   // Reserved
  //   OOB_RESERVED2 = 10,  // Reserved
  //   OOB_RESERVED3 = 11,  // Reserved
  //   IN_PROGRESS = 12,
  //   ERROR_BLOCK_PINNED = 13
  hadoop::hdfs::Status status;

  hadoop::hdfs::BlockOpResponseProto block_op_response;

  hadoop::hdfs::PacketHeaderProto packet_header;
  // rpc type
  hadoop::common::RpcRequestHeaderProto RpcRequestHeader;

  hadoop::common::RequestHeaderProto RequestHeader;

  hadoop::common::RpcResponseHeaderProto RpcResponseHeader;

  Packet() { buffer = std::make_shared<std::vector<uint8_t>>(); }

  Packet(std::shared_ptr<std::vector<uint8_t>> buffer_, Type type_) {
    type = type_;
    auto begin = buffer_->data();
    auto end = begin + buffer_->size();
    uint8_t *&reftobegin = begin;
    Deserialize(reftobegin, end);
    std::cout << "buffer size:" << buffer->size() << std::endl;
  }
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);

  std::shared_ptr<std::vector<uint8_t>> Serialize() const { return buffer; }
};