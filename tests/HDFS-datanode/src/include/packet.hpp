#pragma once

#include <lite.hpp>
#include <unordered_map>

#include "DatanodeProtocol.pb.h"
#include "IpcConnectionContext.pb.h"
#include "ProtobufRpcEngine.pb.h"
#include "RpcHeader.pb.h"
#include "datatransfer.pb.h"

class Packet {
  enum Type { rpc = 0, tcp = 1 };
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
  using InputIterator = uint8_t *;

  std::shared_ptr<std::vector<uint8_t>> buffer;

  Type type;
  Opcode opcode;
  bool request = 1;
 public:
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);

  std::shared_ptr<std::vector<uint8_t>> Serialize() const { return buffer; }

 private:
  static bool ReadDelimitedFrom(
      google::protobuf::io::CodedInputStream *coded_input,
      google::protobuf::MessageLite *message);
  static bool WriteDelimitedTo(
      std::shared_ptr<std::vector<uint8_t>> &buffer,
      const std::vector<google::protobuf::MessageLite *> &ymessages);
};