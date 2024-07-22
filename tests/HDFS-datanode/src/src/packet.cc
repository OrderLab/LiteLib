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
    coded_input->PopLimit(limit);
    return false;  // Failed to parse message.
  }
  if (!coded_input->ConsumedEntireMessage()) {
    coded_input->PopLimit(limit);
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
                    const std::string &hostname, const uint32_t xferport,
                    const uint32_t ipcport) {
  datanodeID->set_hostname(hostname);
  datanodeID->set_xferport(xferport);
  datanodeID->set_ipcport(ipcport);
}

lite::DeserializeResult Packet::Deserialize(InputIterator &begin,
                                            InputIterator end) {
  // TODO: store the information in the cache
  buffer = std::make_shared<std::vector<uint8_t>>(begin, end);
  std::cout << buffer->size() << std::endl;
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
  }
  type = rpc;
  while (begin != end) {
    if (type == rpc) {
      hadoop::common::RpcRequestHeaderProto rpc_request_header;
      hadoop::common::RpcResponseHeaderProto rpc_response_header;
      uint32_t len = ntohl(*reinterpret_cast<uint32_t *>(begin));
      google::protobuf::io::ArrayInputStream array_input_1(
          begin + 4, static_cast<int>(len));
      google::protobuf::io::ArrayInputStream array_input_2(
          begin + 4, static_cast<int>(len));
      google::protobuf::io::CodedInputStream request_input(&array_input_1);
      google::protobuf::io::CodedInputStream response_input(&array_input_2);
      request = 1;
      if (!ReadDelimitedFrom(&request_input, &rpc_request_header)) {
        // reconstruct the coded input stream
        request = 0;
        if (!ReadDelimitedFrom(&response_input, &rpc_response_header)) {
          type = tcp;
          continue;
        }
      }
      begin = begin + 4 + len;
      // cases for callId < 0
      // public static final int AUTHORIZATION_FAILED_CALL_ID = -1;
      // public static final int INVALID_CALL_ID = -2;
      // public static final int CONNECTION_CONTEXT_CALL_ID = -3;
      // public static final int PING_CALL_ID = -4;
      if (request == 1) {  // request
        std::cout << "request" << std::endl;
        std::cout << "callId: " << rpc_request_header.callid() << std::endl;
        if (rpc_request_header.callid() == -3) {
          hadoop::common::IpcConnectionContextProto ipcConnectionContextProto;
          ReadDelimitedFrom(&request_input, &ipcConnectionContextProto);
          std::cout << "protocol: " << ipcConnectionContextProto.protocol()
                    << std::endl;
        }
        if (rpc_request_header.rpckind() != 2) {
          LOG(ERROR) << "not protbuf rpc" << std::endl;
        }
        if (rpc_request_header.callid() >= 0 &&
            rpc_request_header.rpckind() == 2) {
          // proto_buf rpc calls
          hadoop::common::RequestHeaderProto requestHeaderProto;
          ReadDelimitedFrom(&request_input, &requestHeaderProto);
          std::cout << requestHeaderProto.declaringclassprotocolname()
                    << std::endl;
          std::cout << requestHeaderProto.methodname() << std::endl;
          if (requestHeaderProto.methodname() == "versionRequest") {
          } else if (requestHeaderProto.methodname() == "sendHeartbeat") {
            hadoop::hdfs::datanode::HeartbeatRequestProto heartbeatRequestProto;
            ReadDelimitedFrom(&request_input, &heartbeatRequestProto);
            // test generate a rpc call and send it
            buffer = std::make_shared<std::vector<uint8_t>>();
            auto *registration = heartbeatRequestProto.release_registration();
            auto *datanodeID = registration->release_datanodeid();
            set_datanodeID(datanodeID, "lite", 22222, 33333);
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
            ReadDelimitedFrom(&request_input, &registerDatanodeRequestProto);
            // change the port information
            auto *registration =
                registerDatanodeRequestProto.release_registration();
            auto *datanodeID = registration->release_datanodeid();
            set_datanodeID(datanodeID, "lite", 22222, 33333);
            registration->set_allocated_datanodeid(datanodeID);
            registerDatanodeRequestProto.set_allocated_registration(
                registration);
            buffer = std::make_shared<std::vector<uint8_t>>();
            std::vector<google::protobuf::MessageLite *> messages;
            messages.push_back(&rpc_request_header);
            messages.push_back(&requestHeaderProto);
            messages.push_back(&registerDatanodeRequestProto);
            WriteDelimitedTo(buffer, messages);
          } else if (requestHeaderProto.methodname() == "blockReport") {
            hadoop::hdfs::datanode::BlockReportRequestProto
                blockReportRequestProto;
            ReadDelimitedFrom(&request_input, &blockReportRequestProto);
            // change the port information
            auto *registration = blockReportRequestProto.release_registration();
            auto *datanodeID = registration->release_datanodeid();
            set_datanodeID(datanodeID, "lite", 22222, 33333);
            registration->set_allocated_datanodeid(datanodeID);
            blockReportRequestProto.set_allocated_registration(registration);
            buffer = std::make_shared<std::vector<uint8_t>>();
            std::vector<google::protobuf::MessageLite *> messages;
            messages.push_back(&rpc_request_header);
            messages.push_back(&requestHeaderProto);
            messages.push_back(&blockReportRequestProto);
            WriteDelimitedTo(buffer, messages);
          } else {
            LOG(ERROR) << "Unknown method name: "
                       << requestHeaderProto.methodname() << std::endl;
          }
        }
      } else {  // response
        std::cout << "response" << std::endl;
        std::cout << "callId: " << rpc_response_header.callid() << std::endl;
        std::cout << "status: " << rpc_response_header.status() << std::endl;
      }
    } else if (type == tcp) {
      std::cout << "tcp" << std::endl;
      // the op packet
      uint16_t data_transfer_version =
          ntohs(*(reinterpret_cast<uint16_t *>(begin)));
      opcode = static_cast<Opcode>(*(reinterpret_cast<uint8_t *>(begin + 2)));
      if (data_transfer_version == 28) {
        begin += 3;
        std::cout << "data transfer version: " << data_transfer_version;
        std::cout << "opcode: " << opcode;
        std::cout << std::endl;
        google::protobuf::io::ArrayInputStream array_input(
            begin, static_cast<int>(end - begin));
        google::protobuf::io::CodedInputStream coded_input(&array_input);
        switch (opcode) {
          case WRITE_BLOCK: {
            hadoop::hdfs::OpWriteBlockProto OpWriteBlock;
            ReadDelimitedFrom(&coded_input, &OpWriteBlock);
            std::cout << "block size: "
                      << OpWriteBlock.header().baseheader().block().numbytes()
                      << ", blockID: "
                      << OpWriteBlock.header().baseheader().block().blockid()
                      << std::endl;
            break;
          }
          case READ_BLOCK: {
            hadoop::hdfs::OpReadBlockProto OpReadBlock;
            ReadDelimitedFrom(&coded_input, &OpReadBlock);
            std::cout << "Read the block. Offset: " << OpReadBlock.offset()
                      << ", len: " << OpReadBlock.len() << ", block size: "
                      << OpReadBlock.header().baseheader().block().numbytes()
                      << std::endl;
            break;
          }
          default:
            break;
        }
        begin += coded_input.CurrentPosition();
      } else {
        google::protobuf::io::ArrayInputStream array_input(
            begin, static_cast<int>(end - begin));
        google::protobuf::io::CodedInputStream coded_input(&array_input);
        hadoop::hdfs::ClientReadStatusProto ClientReadStatus;
        if (ReadDelimitedFrom(&coded_input, &ClientReadStatus) &&
            coded_input.ConsumedEntireMessage()) {
          std::cout << "client status: " << ClientReadStatus.status()
                    << std::endl;
          begin += coded_input.CurrentPosition();
        } else {
          google::protobuf::io::ArrayInputStream array_input(
              begin, static_cast<int>(end - begin));
          google::protobuf::io::CodedInputStream coded_input(&array_input);
          hadoop::hdfs::BlockOpResponseProto BlockOpResponse;
          if (ReadDelimitedFrom(&coded_input, &BlockOpResponse)) {
            std::cout << "block op response status: "
                      << BlockOpResponse.status() << std::endl;
            begin += coded_input.CurrentPosition();
          } else {
            google::protobuf::io::ArrayInputStream array_input(begin,
                                                               end - begin);
            google::protobuf::io::CodedInputStream coded_input(&array_input);
            hadoop::hdfs::PipelineAckProto PipelineAck;
            if (ReadDelimitedFrom(&coded_input, &PipelineAck)) {
              std::cout << "pipeline status: ";
              for (auto status : PipelineAck.reply()) std::cout << status;
              std::cout << std::endl;
              begin += coded_input.CurrentPosition();
            } else {
              uint32_t packet_length =
                  ntohl(*(reinterpret_cast<uint32_t *>(begin)));
              short head_len = ntohs(*(reinterpret_cast<short *>(begin + 4)));
              google::protobuf::io::ArrayInputStream array_input(begin + 6,
                                                                 head_len);
              google::protobuf::io::CodedInputStream coded_input(&array_input);
              hadoop::hdfs::PacketHeaderProto PacketHeader;
              if (PacketHeader.MergeFromCodedStream(&coded_input) &&
                  coded_input.ConsumedEntireMessage()) {
                if (PacketHeader.lastpacketinblock()) {
                  std::cout << "end of packets" << std::endl;
                } else {
                  std::cout << "Packet header, offset: "
                            << PacketHeader.offsetinblock()
                            << " data length: " << PacketHeader.datalen()
                            << std::endl;
                }
                begin += 6 + head_len;
              }
            }
          }
        }
      }
      if (begin != end) {
        std::string data(begin, end);
        std::cout << data << std::endl;
      }
      begin = end;
    }
  }
  return lite::DeserializeResult::kGood;
}
