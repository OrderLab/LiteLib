#pragma once

#include <lite.hpp>
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
  bool ReadDelimitedFrom(google::protobuf::io::CodedInputStream *coded_input,
                         google::protobuf::MessageLite *message);
};