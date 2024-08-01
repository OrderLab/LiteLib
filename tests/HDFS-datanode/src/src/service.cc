#include "service.hpp"

#include "parse_util.hpp"
void Datanode::SendHeartbeat() {
  bool connected = false;
  evutil_socket_t namenode_socket = 0;
  while (emergency.load()) {
    // TODO: react to the response
    auto callId = rpc_request_header.callid();
    rpc_request_header.set_callid(++callId);
    std::vector<google::protobuf::MessageLite *> messages;
    messages.push_back(&rpc_request_header);
    messages.push_back(&requestHeaderProto);
    messages.push_back(&HeartbeatRequest);
    std::shared_ptr<std::vector<uint8_t>> buffer =
        std::make_shared<std::vector<uint8_t>>();
    WriteDelimitedTo(buffer, messages);
    auto conn = *(server->GetFirstWorker()->conns_.begin());
    std::cout << "send heartbeat in emergency" << std::endl;
    if (!conn->SendCustomizedPackets(std::make_shared<Packet>(buffer), *buffer)){
      LOG(ERROR) << "failed to send heartbeat" << std::endl;
    }
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

std::pair<std::vector<std::shared_ptr<Packet>>, lite::RequestType> Datanode::Match(
    const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
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
    while(!pending_requests.empty()){
      auto pair = pending_requests.pop_front();
      auto req = pair.first;
      type = pair.second;
      if (req->type != resp->type){
        LOG(ERROR) << "Mismatched type" << std::endl;
      }
      if (resp->tcp_type == Packet::BlockOpResponse){
        if (req->tcp_type == Packet::Op){
          ret.push_back(req);
          break;
        }else if (req->tcp_type == Packet::Other){
          std::shared_ptr<std::vector<uint8_t>> total_buffer;
          if (ret.size() == 1){
            // concatenate the packets
            total_buffer = ret.back()->buffer;
            ret.pop_back();
            for (auto byte: *(req->buffer)){
              total_buffer->push_back(byte);
            }
            
          }
        }else{
          LOG(ERROR) << "Wrong tcp type: " << req->tcp_type << std::endl;
          break;
        }
      }
    }
  }
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
  }
}