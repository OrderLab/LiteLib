#pragma once

#include <lite.hpp>
#include <unordered_map>
#include "RpcHeader.pb.h"
#include "IpcConnectionContext.pb.h"
#include "ProtobufRpcEngine.pb.h"
#include "DatanodeProtocol.pb.h"


class Packet {
  using InputIterator = uint8_t *;

  std::shared_ptr<std::vector<uint8_t>> buffer;

 public:
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);

  std::shared_ptr<std::vector<uint8_t>> Serialize() const { return buffer; }

 private:
  static bool ReadDelimitedFrom(google::protobuf::io::CodedInputStream *coded_input,
                         google::protobuf::MessageLite *message);
  
  static std::unordered_map<std::string, google::protobuf::MessageLite (*)(google::protobuf::io::CodedInputStream *coded_input)> rpc_parse_function_map;

  google::protobuf::MessageLite sendHeartbeatRequest(google::protobuf::io::CodedInputStream *coded_input);
};