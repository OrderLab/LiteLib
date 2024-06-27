#include "packet.hpp"

lite::DeserializeResult Packet::Deserialize(InputIterator &begin,
                                            InputIterator end) {
  buffer = std::make_shared<std::vector<uint8_t>>(begin, end);
  size_t left_size = end - begin;
  std::string four_bytes(begin, begin + 4);
  if (four_bytes == "hrpc") {
    // connection header
    std::cout << four_bytes << " version: "
              << static_cast<int>(*(reinterpret_cast<int8_t *>(begin + 4)));
    std::cout << " service class: "
              << static_cast<int>(*(reinterpret_cast<int8_t *>(begin + 5)));
    std::cout << " auth protocol: "
              << static_cast<int>(*(reinterpret_cast<int8_t *>(begin + 6)));
    std::cout << std::endl;
    begin = begin + 7;
    left_size = left_size - 7;
  }

  bool request = 1;
  hadoop::common::RpcRequestHeaderProto rpc_request_header;
  hadoop::common::RpcResponseHeaderProto rpc_response_header;
  uint32_t len = *reinterpret_cast<uint32_t *>(begin);
  begin = begin + 4;
  left_size = left_size - 4;
  google::protobuf::io::ArrayInputStream array_input(
      begin, static_cast<int>(left_size));
  google::protobuf::io::CodedInputStream coded_input(&array_input);
  std::cout << len << std::endl;
  if (!ReadDelimitedFrom(&coded_input, &rpc_request_header)) {
    // reconstruct the coded input stream
    google::protobuf::io::ArrayInputStream array_input(
        begin, static_cast<int>(left_size));
    google::protobuf::io::CodedInputStream coded_input(&array_input);
    request = 0;
    if (!ReadDelimitedFrom(&coded_input, &rpc_response_header)) {
      LOG(ERROR) << "not either a request or a response" << std::endl;
    }
  }
  // cases for callId < 0
  // public static final int AUTHORIZATION_FAILED_CALL_ID = -1;
  // public static final int INVALID_CALL_ID = -2;
  // public static final int CONNECTION_CONTEXT_CALL_ID = -3;
  // public static final int PING_CALL_ID = -4;
  if (request == 1) {  // request
    std::cout << "request" << std::endl;
    if (rpc_request_header.callid() == -3) {
      hadoop::common::IpcConnectionContextProto ipcConnectionContextProto;
      ReadDelimitedFrom(&coded_input, &ipcConnectionContextProto);
    }
    if (rpc_request_header.callid() > 0 && rpc_request_header.rpckind() == 2) {
      // proto_buf rpc calls
      hadoop::common::RequestHeaderProto requestHeaderProto;
      ReadDelimitedFrom(&coded_input, &requestHeaderProto);
      std::cout << requestHeaderProto.declaringclassprotocolname() << std::endl;
      std::cout << requestHeaderProto.methodname() << std::endl;
      std::cout << "callId: " <<rpc_request_header.callid() << std::endl;
      if (requestHeaderProto.methodname() == "versionRequest") {
      } else if (requestHeaderProto.methodname() == "sendHeartbeat") {
        hadoop::hdfs::datanode::HeartbeatRequestProto heartbeatRequestProto;
        ReadDelimitedFrom(&coded_input, &heartbeatRequestProto);
        std::cout << heartbeatRequestProto.registration().softwareversion()
                  << std::endl;
      } else {
        LOG(ERROR) << "Unknown method name" << requestHeaderProto.methodname()
                   << std::endl;
      }
    }
  } else {  // response
    std::cout << "response" << std::endl;
    std::cout << "callId: " << rpc_response_header.callid() << std::endl;
    std::cout << "status: " << rpc_response_header.status() << std::endl;
  }
  begin = end;
  return lite::DeserializeResult::kGood;
}

bool Packet::ReadDelimitedFrom(
    google::protobuf::io::CodedInputStream *coded_input,
    google::protobuf::MessageLite *message) {
  // Read the size.
  uint32_t size;
  if (!coded_input->ReadVarint32(&size)) {
    return false;  // Failed to read size.
  }

  // Tell the stream not to read beyond that size.
  google::protobuf::io::CodedInputStream::Limit limit =
      coded_input->PushLimit(size);

  // Parse the message.
  if (!message->MergeFromCodedStream(coded_input)) {
    return false;  // Failed to parse message.
  }
  if (!coded_input->ConsumedEntireMessage()) {
    return false;  // Input is ill-formed.
  }

  // Release the limit.
  coded_input->PopLimit(limit);

  return true;
}