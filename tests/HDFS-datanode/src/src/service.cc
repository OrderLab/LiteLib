#include "service.hpp"

#include <fstream>

#include "parse_util.hpp"
class BlockGuard {
 public:
  BlockGuard(std::string BlockPath) {
    blockstream = std::make_shared<std::ifstream>(BlockPath, std::ios::binary);
  }
  ~BlockGuard() { blockstream->close(); }
  std::shared_ptr<std::ifstream> blockstream;
};

void WriteErrorResponse(std::shared_ptr<std::vector<uint8_t>> &buffer) {
  hadoop::hdfs::BlockOpResponseProto BlockOpResponse;
  BlockOpResponse.set_status(hadoop::hdfs::Status::ERROR);
  WriteDelimitedTo(buffer, &BlockOpResponse);
}

void WritePacketHeader(std::shared_ptr<std::vector<uint8_t>> &buffer,
                       const hadoop::hdfs::PacketHeaderProto &PacketHeader) {
  uint32_t nvalue = htonl(PacketHeader.ByteSizeLong());
  buffer->push_back(static_cast<uint8_t>(nvalue & 0xFF));
  buffer->push_back(static_cast<uint8_t>((nvalue >> 8) & 0xFF));
  size_t message_size = PacketHeader.ByteSizeLong();
  // Resize the vector to hold the additional data (message size)
  size_t old_size = buffer->size();
  buffer->resize(old_size + message_size);
  // Use ArrayOutputStream with the new part of the buffer
  google::protobuf::io::ArrayOutputStream array_output_stream(
      buffer->data() + old_size, message_size);
  google::protobuf::io::CodedOutputStream coded_output_stream(
      &array_output_stream);
  // Serialize the message
  PacketHeader.SerializeWithCachedSizes(&coded_output_stream);
}

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
    // WriteRpc(buffer, messages);
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
      std::cout << "combine the unknown responses" << std::endl;
      std::shared_ptr<std::vector<uint8_t>> total_buffer = LastResponse->buffer;
      if (total_buffer) {
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
      std::cout << "unknown responses" << std::endl;
      return {std::vector<std::shared_ptr<Packet>>{}, lite::forward};
    }
    std::cout << "matching tcp resps" << std::endl;
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
          std::cout << "receive a block op response" << std::endl;
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
    LastResponse = resp;
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
    if (req->RequestHeader.methodname() == "sendHeartbeat") {
      ReadDelimitedFrom(&coded_input, &HeartbeatRequest);
      rpc_request_header = req->RpcRequestHeader;
      requestHeaderProto = req->RequestHeader;
    } else if (req->RequestHeader.methodname() == "blockReport") {
      std::cout << "update based on the block report" << std::endl;
      initialized = true;
      hadoop::hdfs::datanode::BlockReportRequestProto BlockReportRequest;
      if (!ReadDelimitedFrom(&coded_input, &BlockReportRequest)){
        LOG(ERROR) << "fail to parse the block report" << std::endl;
      }
      std::string poolId = BlockReportRequest.blockpoolid();
      std::cout << "poolId: " << poolId << std::endl;
      auto reports = BlockReportRequest.reports();
      for (auto report : reports) {
        std::cout << "number of blocks: " << report.blocks().size();
        for (auto i = 0; i < report.blocks().size(); i++) {
          std::cout << "get one block" << std::endl;
          long blockId = report.blocks()[i];
          auto blockinfo = report.blocksbuffers()[i];
          long *info_ptr = reinterpret_cast<long *>(blockinfo.data());
          long len = *(info_ptr + 1);
          long generationstamp = *(info_ptr + 2);
          hadoop::hdfs::ExtendedBlockProto block;
          block.set_poolid(poolId);
          block.set_blockid(blockId);
          block.set_generationstamp(generationstamp);
          block.set_numbytes(len);
          BlockMap.emplace(std::make_pair(poolId, blockId), block);
          std::cout << "map the block: " << blockId << std::endl;
        }
      }
    } else {
      LOG(ERROR) << "unknown method name: " << req->RequestHeader.methodname()
                 << std::endl;
    }
    } else if (resp->type == Packet::tcp) {
      if (resp->tcp_type == Packet::BlockOpResponse) {
        std::cout << "block op response, status: "
                  << resp->block_op_response.status() << std::endl;
        if (resp->block_op_response.has_readopchecksuminfo())
          std::cout
              << "checksum type: "
              <<
              resp->block_op_response.readopchecksuminfo().checksum().type()
              << "chunk offset: "
              << resp->block_op_response.readopchecksuminfo().chunkoffset()
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
            hadoop::hdfs::ExtendedBlockProto block;
            block = OpReadBlock.header().baseheader().block();
            long blockId = static_cast<long>(block.blockid());
            long numbytes = static_cast<long>(block.numbytes());
            long generationstamp =
            static_cast<long>(block.generationstamp()); std::string poolId =
            block.poolid(); std::cout << "Read the block. Offset: " <<
            OpReadBlock.offset()
                      << ", len: " << OpReadBlock.len() << ", poolId" <<
                      poolId
                      << ", block size: " << numbytes << ", blockId: " <<
                      blockId
                      << ", generationstamp: " << generationstamp <<
                      std::endl;
            BlockMap.emplace(std::make_pair(poolId, blockId), block);
            std::cout << "map the block: " << blockId << std::endl;
            break;
          }
          default:
            LOG(ERROR) << "unknown opcode" << std::endl;
            break;
        }
      }
    }
  }

  std::pair<Packet, bool> Datanode::EmergencyServe(
      std::shared_ptr<Packet> req, ConnectionInfo & conn, Cache * cache,
      Logger * logger, bool flow_control) {
    if (req->type == Packet::tcp) {
      if (req->tcp_type == Packet::Op) {
        switch (req->opcode) {
          case Packet::READ_BLOCK: {
            google::protobuf::io::ArrayInputStream array_input(
                req->buffer->data() + 3,
                static_cast<int>(req->buffer->size() - 3));
            google::protobuf::io::CodedInputStream coded_input(&array_input);
            hadoop::hdfs::OpReadBlockProto OpReadBlock;
            ReadDelimitedFrom(&coded_input, &OpReadBlock);
            hadoop::hdfs::ExtendedBlockProto TargetBlock =
                OpReadBlock.header().baseheader().block();
            std::streampos offset =
                static_cast<std::streampos>(OpReadBlock.offset());
            std::streamsize len =
                static_cast<std::streamsize>(OpReadBlock.len());
            Packet resp;
            resp.type == Packet::tcp;
            resp.tcp_type == Packet::BlockOpResponse;
            std::vector<google::protobuf::MessageLite *> messages;
            auto it = BlockMap.find(
                std::make_pair(TargetBlock.poolid(), TargetBlock.blockid()));
            if (it != BlockMap.end() ||
                it->second.generationstamp() != TargetBlock.generationstamp() ||
                it->second.numbytes() != TargetBlock.numbytes()) {
              // no such block in the map
              LOG(ERROR) << "no such block" << std::endl;
              WriteErrorResponse(resp.buffer);
              return std::make_pair(resp, false);
            }
            std::string BlockPath;
            std::string MetaPath;
            BlockGuard block_guard(BlockPath);
            BlockGuard meta_guard(MetaPath);
            std::shared_ptr<std::ifstream> blockstream =
                block_guard.blockstream;
            std::shared_ptr<std::ifstream> metastream = meta_guard.blockstream;
            if (!blockstream->is_open() || !metastream->is_open()) {
              LOG(ERROR) << "Failed to open the block: " << BlockPath
                         << std::endl;
              WriteErrorResponse(resp.buffer);
              return std::make_pair(resp, false);
            }
            blockstream->seekg(offset);
            metastream->seekg(0, std::ios::end);
            if (!blockstream->good() || !metastream->good()) {
              LOG(ERROR) << "Failed to seek to the offset: " << offset
                         << std::endl;
              WriteErrorResponse(resp.buffer);
              return std::make_pair(resp, false);
            }
            std::streamsize metasize = metastream->tellg();
            metastream->seekg(0, std::ios::beg);
            auto *ReadOpChecksumInfo =
                resp.block_op_response.release_readopchecksuminfo();
            auto *Checksum = ReadOpChecksumInfo->release_checksum();
            Checksum->set_type(
                hadoop::hdfs::ChecksumTypeProto::CHECKSUM_CRC32C);
            Checksum->set_bytesperchecksum(512);
            ReadOpChecksumInfo->set_allocated_checksum(Checksum);
            ReadOpChecksumInfo->set_chunkoffset(0);
            resp.block_op_response.set_status(hadoop::hdfs::Status::SUCCESS);
            resp.block_op_response.set_allocated_readopchecksuminfo(
                ReadOpChecksumInfo);
            WriteDelimitedTo(resp.buffer, &resp.block_op_response);
            uint32_t pktlen = 4 + metasize + len;
            WriteFixedInt32ToVector(pktlen, resp.buffer);
            resp.packet_header.set_offsetinblock(static_cast<int64_t>(offset));
            resp.packet_header.set_seqno(0);
            resp.packet_header.set_lastpacketinblock(0);
            resp.packet_header.set_datalen(static_cast<int64_t>(len));
            WritePacketHeader(resp.buffer, resp.packet_header);
            size_t old_size = resp.buffer->size();
            resp.buffer->resize(resp.buffer->size() + metasize + len);
            metastream->read(
                reinterpret_cast<char *>(resp.buffer->data()) + old_size,
                metasize);
            blockstream->read(reinterpret_cast<char *>(resp.buffer->data()) +
                                  old_size + metasize,
                              len);
            return std::make_pair(resp, false);
            break;
          }
          default:
            break;
        }
      }
    }
    return std::make_pair(Packet(), false);
  }