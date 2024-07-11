#include "packet.hpp"

void WriteFixedInt32ToVector(uint32_t value,
                             std::shared_ptr<std::vector<uint8_t>> &buffer) {
  uint32_t nvalue = htonl(value);
  buffer->push_back(static_cast<uint8_t>(nvalue & 0xFF));
  buffer->push_back(static_cast<uint8_t>((nvalue >> 8) & 0xFF));
  buffer->push_back(static_cast<uint8_t>((nvalue >> 16) & 0xFF));
  buffer->push_back(static_cast<uint8_t>((nvalue >> 24) & 0xFF));
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

bool Packet::WriteDelimitedTo(
    std::shared_ptr<std::vector<uint8_t>> &buffer,
    const std::vector<google::protobuf::MessageLite *> &messages) {
  // write in the total size
  uint32_t total_size = 0;
  for (google::protobuf::MessageLite *message : messages) {
    uint32_t message_size = message->ByteSizeLong();
    total_size +=
        message_size +
        google::protobuf::io::CodedOutputStream::VarintSize32(message_size);
  }
  WriteFixedInt32ToVector(total_size, buffer);
  // write in each message
  for (google::protobuf::MessageLite *message : messages) {
    // Calculate the size of the message
    size_t message_size = message->ByteSizeLong();

    // Resize the vector to hold the additional data (message size + varint
    // size)
    size_t old_size = buffer->size();
    size_t varint_size = google::protobuf::io::CodedOutputStream::VarintSize32(
        static_cast<uint32_t>(message_size));
    buffer->resize(old_size + message_size + varint_size);

    // Use ArrayOutputStream with the new part of the buffer
    google::protobuf::io::ArrayOutputStream array_output_stream(
        buffer->data() + old_size, message_size + varint_size);
    google::protobuf::io::CodedOutputStream coded_output_stream(
        &array_output_stream);

    // Write the size of the message as a varint
    coded_output_stream.WriteVarint32(static_cast<uint32_t>(message_size));

    // Serialize the message
    message->SerializeWithCachedSizes(&coded_output_stream);
  }

  return true;  // The message was written successfully.
}

void set_datanodeID(hadoop::hdfs::DatanodeIDProto *datanodeID,
                    const std::string &hostname, const uint32_t xferport) {
  datanodeID->set_hostname(hostname);
  datanodeID->set_xferport(xferport);
}

lite::DeserializeResult Packet::Deserialize(InputIterator &begin,
                                            InputIterator end) {
  // TODO: store the information in the cache
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

  hadoop::common::RpcRequestHeaderProto rpc_request_header;
  hadoop::common::RpcResponseHeaderProto rpc_response_header;
  uint32_t len = ntohl(*reinterpret_cast<uint32_t *>(begin));
  begin = begin + 4;
  left_size = left_size - 4;
  google::protobuf::io::ArrayInputStream array_input(
      begin, static_cast<int>(left_size));
  google::protobuf::io::CodedInputStream coded_input(&array_input);
  std::cout << "received buffer size: " << left_size << std::endl;
  std::cout << "len: " << len << std::endl;
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
      std::cout << "callId: " << rpc_request_header.callid() << std::endl;
      if (requestHeaderProto.methodname() == "versionRequest") {
      } else if (requestHeaderProto.methodname() == "sendHeartbeat") {
        hadoop::hdfs::datanode::HeartbeatRequestProto heartbeatRequestProto;
        ReadDelimitedFrom(&coded_input, &heartbeatRequestProto);
        std::cout << heartbeatRequestProto.registration().softwareversion()
                  << std::endl;
        // test generate a rpc call and send it
        buffer = std::make_shared<std::vector<uint8_t>>();
        auto *registration = heartbeatRequestProto.release_registration();
        auto *datanodeID = registration->release_datanodeid();
        set_datanodeID(datanodeID, "lite", 11212);
        registration->set_allocated_datanodeid(datanodeID);
        heartbeatRequestProto.set_allocated_registration(registration);
        std::vector<google::protobuf::MessageLite *> messages;
        messages.push_back(&rpc_request_header);
        messages.push_back(&requestHeaderProto);
        messages.push_back(&heartbeatRequestProto);
        WriteDelimitedTo(buffer, messages);
      } else if (requestHeaderProto.methodname() == "registerDatanode") {
        hadoop::hdfs::datanode::RegisterDatanodeRequestProto
            registerDatanodeRequestProto;
        ReadDelimitedFrom(&coded_input, &registerDatanodeRequestProto);
        // change the port information
        auto *registration =
            registerDatanodeRequestProto.release_registration();
        auto *datanodeID = registration->release_datanodeid();
        set_datanodeID(datanodeID, "lite", 11212);
        registration->set_allocated_datanodeid(datanodeID);
        registerDatanodeRequestProto.set_allocated_registration(registration);
        buffer = std::make_shared<std::vector<uint8_t>>();
        std::vector<google::protobuf::MessageLite *> messages;
        messages.push_back(&rpc_request_header);
        messages.push_back(&requestHeaderProto);
        messages.push_back(&registerDatanodeRequestProto);
        WriteDelimitedTo(buffer, messages);
      } else if (requestHeaderProto.methodname() == "blockReport") {
        hadoop::hdfs::datanode::BlockReportRequestProto blockReportRequestProto;
        ReadDelimitedFrom(&coded_input, &blockReportRequestProto);
        // change the port information
        auto *registration = blockReportRequestProto.release_registration();
        auto *datanodeID = registration->release_datanodeid();
        set_datanodeID(datanodeID, "lite", 11212);
        registration->set_allocated_datanodeid(datanodeID);
        blockReportRequestProto.set_allocated_registration(registration);
        buffer = std::make_shared<std::vector<uint8_t>>();
        std::vector<google::protobuf::MessageLite *> messages;
        messages.push_back(&rpc_request_header);
        messages.push_back(&requestHeaderProto);
        messages.push_back(&blockReportRequestProto);
        WriteDelimitedTo(buffer, messages);
      } else {
        LOG(FATAL) << "Unknown method name: " << requestHeaderProto.methodname()
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
