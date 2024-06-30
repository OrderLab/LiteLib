#pragma once

#include <lite.hpp>
#include <unordered_map>

#include "DatanodeProtocol.pb.h"
#include "IpcConnectionContext.pb.h"
#include "ProtobufRpcEngine.pb.h"
#include "RpcHeader.pb.h"

class Packet {
  using InputIterator = uint8_t *;

  std::shared_ptr<std::vector<uint8_t>> buffer;

  bool request = 1;

 public:
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);

  std::shared_ptr<std::vector<uint8_t>> Serialize() const { return buffer; }

 private:
  static bool ReadDelimitedFrom(
      google::protobuf::io::CodedInputStream *coded_input,
      google::protobuf::MessageLite *message);
  static bool WriteDelimitedTo(std::shared_ptr<std::vector<uint8_t>> &buffer,
                               const google::protobuf::MessageLite *message);

  static std::unordered_map<
      std::string, google::protobuf::MessageLite (*)(
                       google::protobuf::io::CodedInputStream *coded_input)>
      rpc_parse_function_map;

  google::protobuf::MessageLite sendHeartbeatRequest(
      google::protobuf::io::CodedInputStream *coded_input);
};