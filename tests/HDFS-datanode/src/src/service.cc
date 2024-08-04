#include "service.hpp"

#include "parse_util.hpp"
void Datanode::SendHeartbeat() {
  bool connected = false;
  evutil_socket_t namenode_socket = 0;
  while (emergency.load()) {
    // TODO: react to the response
    // auto callId = rpc_request_header.callid();
    // rpc_request_header.set_callid(++callId);
    // std::vector<google::protobuf::MessageLite *> messages;
    // messages.push_back(&rpc_request_header);
    // messages.push_back(&requestHeaderProto);
    // messages.push_back(&HeartbeatRequest);
    // std::shared_ptr<std::vector<uint8_t>> buffer =
    //     std::make_shared<std::vector<uint8_t>>();
    // WriteDelimitedTo(buffer, messages);
    // auto conn = *(server->GetFirstWorker()->conns_.begin());
    // std::shared_ptr<Packet> heartbeat = std::make_shared<Packet>(buffer,
    // Packet::rpc); if (!conn->SendCustomizedPackets(heartbeat,
    // heartbeat->buffer)){
    //   LOG(ERROR) << "failed to send heartbeat" << std::endl;
    // }
    std::cout << "send heartbeat in emergency" << std::endl;
    // sleep for 3 seconds
    boost::this_thread::sleep_for(boost::chrono::seconds(3));
  }
}

void Datanode::NormalToEmergencyHook() {
  emergency.store(true);
  // periodically sending the heartbeat
  HeartbeatThread =
      new boost::thread(std::bind(&Datanode::SendHeartbeat, this));
}

void Datanode::EmergencyToNormalHook() {
  emergency.store(false);
  delete HeartbeatThread;
}

std::pair<std::vector<std::shared_ptr<Packet>>, lite::RequestType>
Datanode::Match(
    std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
    lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, lite::RequestType>>
        &pending_requests) {
  if (pending_requests.empty()) {
    return {std::vector<std::shared_ptr<Packet>>{}, lite::forward};
  }
  std::vector<std::shared_ptr<Packet>> ret;
  lite::RequestType type = lite::forward;
  if (resp->type == Packet::rpc) {
    auto pair = pending_requests.pop_front();
    auto req = pair.first;
    type = pair.second;
    if (req->type == resp->type &&
        req->RpcRequestHeader.callid() == resp->RpcResponseHeader.callid() &&
        req->RpcRequestHeader.clientid() ==
            resp->RpcResponseHeader.clientid()) {
      std::cout << "match callId: " << req->RpcRequestHeader.callid()
                << std::endl;
      resp->RequestHeader = req->RequestHeader;
    }
    ret.push_back(req);
  } else if (resp->type == Packet::tcp) {
    // combine the unknown responses
    if (LastResponse->tcp_type == Packet::Other) {
      std::shared_ptr<std::vector<uint8_t>> total_buffer = LastResponse->buffer;
      if (total_buffer->size() != 0) {
        for (auto byte : *(resp->buffer)) {
          total_buffer->push_back(byte);
        }
        LastResponse = std::make_shared<Packet>(total_buffer, Packet::tcp);
        resp = LastResponse;
      } else {
        LastResponse = resp;
      }
    }
    if (resp->tcp_type == Packet::Other) {
      // still unknown
      return {std::vector<std::shared_ptr<Packet>>{}, lite::forward};
    }
    while (!pending_requests.empty()) {
      auto pair = pending_requests.pop_front();
      auto req = pair.first;
      type = pair.second;
      if (req->type != LastResponse->type) {
        LOG(ERROR) << "Mismatched type" << std::endl;
      }
      if (req->tcp_type == Packet::Other) {
        std::shared_ptr<std::vector<uint8_t>> total_buffer;
        if (ret.size() == 1) {
          // concatenate the packets
          total_buffer = ret.back()->buffer;
          ret.pop_back();
          for (auto byte : *(req->buffer)) {
            total_buffer->push_back(byte);
          }
          req = std::make_shared<Packet>(total_buffer, Packet::tcp);
        }
        ret.push_back(req);
        if (req->tcp_type == Packet::ReplyStatus) {
          // discard the reply
          ret.pop_back();
        }
        if (req->tcp_type == Packet::Other) {
          continue;
        }
      } else {
        ret.push_back(req);
      }
      if (resp->tcp_type == Packet::BlockOpResponse) {
        if (req->tcp_type == Packet::Op) {
          break;
        } else {
          LOG(ERROR) << "Wrong request tcp type for BlockOpResponse: "
                     << req->tcp_type << std::endl;
          break;
        }
      } else if (resp->tcp_type == Packet::PacketHeader) {
        return {std::vector<std::shared_ptr<Packet>>{}, type};
      } else {
        LOG(ERROR) << "Wrong response tcp type: " << req->tcp_type << std::endl;
        break;
      }
    }
  }
  LastResponse = resp;
  return std::make_pair(ret, type);
  // pending_requests.clear();
  // return {std::vector<std::shared_ptr<Packet>>{}, true};
}

void Datanode::NormalUpdate(const std::shared_ptr<Packet> &resp,
                            std::vector<std::shared_ptr<Packet>> requests,
                            ConnectionInfo &conn, Cache *cache) {
  if (resp->type == Packet::rpc) {
    std::shared_ptr<Packet> req = requests.front();
    if (req->RequestHeader.methodname() == "sendHeartbeat") {
      uint32_t offset = 4;
      uint32_t size1 = req->RpcRequestHeader.ByteSizeLong();
      offset +=
          size1 + google::protobuf::io::CodedOutputStream::VarintSize32(size1);
      uint32_t size2 = req->RequestHeader.ByteSizeLong();
      offset +=
          size2 + google::protobuf::io::CodedOutputStream::VarintSize32(size2);
      google::protobuf::io::ArrayInputStream array_input(
          req->buffer->data() + offset,
          static_cast<int>(req->buffer->size() - offset));
      google::protobuf::io::CodedInputStream coded_input(&array_input);
      ReadDelimitedFrom(&coded_input, &HeartbeatRequest);
      rpc_request_header = req->RpcRequestHeader;
      requestHeaderProto = req->RequestHeader;
    }
  } else if (resp->type == Packet::tcp) {
    if (resp->tcp_type == Packet::BlockOpResponse) {
      std::cout << "block op response, status: "
                << resp->block_op_response.status() << std::endl;
      if (resp->block_op_response.has_readopchecksuminfo())
        std::cout
            << "checksum type: "
            << resp->block_op_response.readopchecksuminfo().checksum().type()
            << " bytes per checksum: "
            << resp->block_op_response.readopchecksuminfo()
                   .checksum()
                   .bytesperchecksum()
            << std::endl;
      auto req = requests.front();
      switch (req->opcode) {
        case Packet::READ_BLOCK: {
          google::protobuf::io::ArrayInputStream array_input(
              req->buffer->data() + 3,
              static_cast<int>(req->buffer->size() - 3));
          google::protobuf::io::CodedInputStream coded_input(&array_input);
          hadoop::hdfs::OpReadBlockProto OpReadBlock;
          ReadDelimitedFrom(&coded_input, &OpReadBlock);
          long blockId = static_cast<long>(
              OpReadBlock.header().baseheader().block().blockid());
          long numbytes = static_cast<long>(
              OpReadBlock.header().baseheader().block().numbytes());
          long generationstamp = static_cast<long>(
              OpReadBlock.header().baseheader().block().generationstamp());
          std::string poolId =
              OpReadBlock.header().baseheader().block().poolid();
          std::cout << "Read the block. Offset: " << OpReadBlock.offset()
                    << ", len: " << OpReadBlock.len() << ", poolId" << poolId
                    << ", block size: " << numbytes << ", blockId: " << blockId
                    << ", generationstamp: " << generationstamp << std::endl;
          Block block(blockId, numbytes, generationstamp, poolId);
          BlockMap.emplace(blockId, block);
          break;
        }
        default:
          LOG(ERROR) << "unknown opcode" << std::endl;
          break;
      }
    }
  }
}