#include "packet.hpp"

#include "parse_util.hpp"

void set_datanodeID(hadoop::hdfs::DatanodeIDProto *datanodeID,
                    const std::string &hostname, const uint32_t xferport,
                    const uint32_t ipcport) {
  datanodeID->set_hostname(hostname);
  datanodeID->set_xferport(xferport);
  datanodeID->set_ipcport(ipcport);
}

lite::DeserializeResult Packet::Deserialize(InputIterator &begin,
                                            InputIterator end) {
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
      if (!ReadDelimitedFrom(&request_input, &rpc_request_header)) { // read request
        // reconstruct the coded input stream
        request = 0;
        if (!ReadDelimitedFrom(&response_input, &rpc_response_header)) { // read response
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
        RpcRequestHeader = rpc_request_header;
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
          RequestHeader = requestHeaderProto;
          if (requestHeaderProto.methodname() == "versionRequest") {
          } else if (requestHeaderProto.methodname() == "sendHeartbeat") {
            hadoop::hdfs::datanode::HeartbeatRequestProto heartbeatRequestProto;
            ReadDelimitedFrom(&request_input, &heartbeatRequestProto);
            // change the port in datanode id
            buffer = std::make_shared<std::vector<uint8_t>>();
            auto *registration = heartbeatRequestProto.release_registration();
            auto *datanodeID = registration->release_datanodeid();
            set_datanodeID(datanodeID, "dn1", 9866, 9867);
            registration->set_allocated_datanodeid(datanodeID);
            heartbeatRequestProto.set_allocated_registration(registration);
            std::vector<google::protobuf::MessageLite *> messages;
            messages.push_back(&rpc_request_header);
            messages.push_back(&requestHeaderProto);
            messages.push_back(&heartbeatRequestProto);
            WriteRpc(buffer, messages);
          } else if (requestHeaderProto.methodname() == "registerDatanode") {
            hadoop::hdfs::datanode::RegisterDatanodeRequestProto
                registerDatanodeRequestProto;
            ReadDelimitedFrom(&request_input, &registerDatanodeRequestProto);
            // change the port information
            auto *registration =
                registerDatanodeRequestProto.release_registration();
            auto *datanodeID = registration->release_datanodeid();
            set_datanodeID(datanodeID, "dn1", 9866, 9867);
            registration->set_allocated_datanodeid(datanodeID);
            registerDatanodeRequestProto.set_allocated_registration(
                registration);
            buffer = std::make_shared<std::vector<uint8_t>>();
            std::vector<google::protobuf::MessageLite *> messages;
            messages.push_back(&rpc_request_header);
            messages.push_back(&requestHeaderProto);
            messages.push_back(&registerDatanodeRequestProto);
            WriteRpc(buffer, messages);
          } else if (requestHeaderProto.methodname() == "blockReport") {
            hadoop::hdfs::datanode::BlockReportRequestProto
                blockReportRequestProto;
            ReadDelimitedFrom(&request_input, &blockReportRequestProto);
            // std::cout << "block report: " << blockReportRequestProto.reports().front().blocksbuffers() << std::endl;
            // change the port information
            auto *registration = blockReportRequestProto.release_registration();
            auto *datanodeID = registration->release_datanodeid();
            set_datanodeID(datanodeID, "dn1", 9866, 9867);
            registration->set_allocated_datanodeid(datanodeID);
            blockReportRequestProto.set_allocated_registration(registration);
            buffer = std::make_shared<std::vector<uint8_t>>();
            std::vector<google::protobuf::MessageLite *> messages;
            messages.push_back(&rpc_request_header);
            messages.push_back(&requestHeaderProto);
            messages.push_back(&blockReportRequestProto);
            WriteRpc(buffer, messages);
          } else {
            LOG(ERROR) << "Unknown method name: "
                       << requestHeaderProto.methodname() << std::endl;
          }
        }
      } else {  // response
        std::cout << "response" << std::endl;
        std::cout << "callId: " << rpc_response_header.callid() << std::endl;
        std::cout << "status: " << rpc_response_header.status() << std::endl;
        RpcResponseHeader = rpc_response_header;
      }
    } else if (type == tcp) {
      std::cout << "tcp" << std::endl;
      while (1) {
        uint16_t data_transfer_version =
            ntohs(*(reinterpret_cast<uint16_t *>(begin)));
        // the op packet
        if (data_transfer_version == 28) {
          tcp_type = Op;
          opcode =
              static_cast<Opcode>(*(reinterpret_cast<uint8_t *>(begin + 2)));
          std::cout << "data transfer version: " << data_transfer_version;
          std::cout << "opcode: " << opcode;
          std::cout << std::endl;
          break;
        }
        // block op response or status reponse
        google::protobuf::io::ArrayInputStream array_input(
            begin, static_cast<int>(end - begin));
        google::protobuf::io::CodedInputStream coded_input(&array_input);
        if (ReadDelimitedFrom(&coded_input, &block_op_response)) {
          if (block_op_response.has_firstbadlink() ||
              block_op_response.has_checksumresponse() ||
              block_op_response.has_readopchecksuminfo() ||
              block_op_response.has_shortcircuitaccessversion()) {
            tcp_type = BlockOpResponse;
            std::cout << "block op response status: "
                      << block_op_response.status() << std::endl;
            std::cout << "has firstbadlink: " << block_op_response.has_firstbadlink() << std::endl;
            std::cout << "has checksumresponse: " << block_op_response.has_checksumresponse() << std::endl;
            std::cout << "has readopchecksuminfo: " << block_op_response.has_readopchecksuminfo() << std::endl;
            std::cout << "has shortcircuitaccessversion: " << block_op_response.has_shortcircuitaccessversion() << std::endl;
          } else {
            tcp_type = ReplyStatus;
            status = block_op_response.status();
            std::cout << "reply status: " << status << std::endl;
          }
          break;
        }
        // packet header
        if (end - begin >= 6) {
          uint32_t packet_length =
              ntohl(*(reinterpret_cast<uint32_t *>(begin)));
          short head_len = ntohs(*(reinterpret_cast<short *>(begin + 4)));
          google::protobuf::io::ArrayInputStream array_input(begin + 6,
                                                             head_len);
          google::protobuf::io::CodedInputStream coded_input(&array_input);
          if (packet_header.MergeFromCodedStream(&coded_input) &&
              coded_input.ConsumedEntireMessage()) {
            tcp_type = Packet::PacketHeader;
            std::cout << "packet header" << std::endl;
          }
          break;
        }
        tcp_type = Other;
        break;
      }
      begin = end;
    }
  }
  return lite::DeserializeResult::kGood;
}
