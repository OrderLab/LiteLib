#include "service.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <filesystem>

#include <fstream>

#include "parse_util.hpp"
template <typename T>
class BlockGuard { // used to close the stream automatically
public:
  BlockGuard(std::string BlockPath, std::shared_ptr<T> &blockstream_) {
    blockstream = std::make_shared<T>(BlockPath, std::ios::binary);
    blockstream_ = blockstream;
  }
  ~BlockGuard() { blockstream->close(); }
  std::shared_ptr<T> blockstream;
};

void WriteErrorResponse(std::shared_ptr<std::vector<uint8_t>> &buffer) {
  hadoop::hdfs::BlockOpResponseProto BlockOpResponse;
  BlockOpResponse.set_status(hadoop::hdfs::Status::ERROR);
  WriteDelimitedTo(buffer, &BlockOpResponse);
}

void WritePacketHeader(std::shared_ptr<std::vector<uint8_t>> &buffer,
                       const hadoop::hdfs::PacketHeaderProto &PacketHeader) {
  size_t message_size = PacketHeader.ByteSizeLong();
  uint16_t nvalue = htons(message_size);
  buffer->push_back(static_cast<uint8_t>(nvalue & 0xFF));
  buffer->push_back(static_cast<uint8_t>((nvalue >> 8) & 0xFF));
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

void WritePacket(std::shared_ptr<std::ifstream> &blockstream,
                 std::shared_ptr<std::ifstream> &metastream, int seqno,
                 int offset, size_t datasize, Packet &resp) {
  size_t metasize = std::ceil(static_cast<double>(datasize) / 512.0) * 4;
  std::cout << "calculated metasize: " << metasize << std::endl;
  uint32_t pktlen = 4 + metasize + datasize;
  WriteFixedInt32ToVector(pktlen, resp.buffer);
  resp.packet_header.set_offsetinblock(static_cast<int64_t>(offset));
  resp.packet_header.set_seqno(seqno);
  if (datasize == 0) {
    resp.packet_header.set_lastpacketinblock(1);
  } else {
    resp.packet_header.set_lastpacketinblock(0);
  }
  resp.packet_header.set_datalen(static_cast<int64_t>(datasize));
  WritePacketHeader(resp.buffer, resp.packet_header);
  size_t old_size = resp.buffer->size();
  resp.buffer->resize(resp.buffer->size() + metasize + datasize);
  if (seqno == 0) {
    short version;
    uint8_t type;
    int bpc;
    metastream->read(reinterpret_cast<char *>(&version), 2);
    metastream->read(reinterpret_cast<char *>(&type), 1);
    metastream->read(reinterpret_cast<char *>(&bpc), 4);
  }
  metastream->read(reinterpret_cast<char *>(resp.buffer->data()) + old_size,
                   metasize);
  blockstream->read(reinterpret_cast<char *>(resp.buffer->data()) + old_size +
                        metasize,
                    datasize);
}
void Datanode::SendHeartbeatEmergency() {
  bool connected = false;
  evutil_socket_t namenode_socket = 0;
  try {
    while (emergency.load()) {
      boost::this_thread::sleep_for(boost::chrono::seconds(3));
      std::cout << "send heartbeat in emergency" << std::endl;
      auto callId = rpc_request_header.callid();
      rpc_request_header.set_callid(++callId);
      std::vector<google::protobuf::MessageLite *> messages;
      messages.push_back(&rpc_request_header);
      messages.push_back(&requestHeaderProto);
      messages.push_back(&HeartbeatRequest);
      std::shared_ptr<std::vector<uint8_t>> buffer =
          std::make_shared<std::vector<uint8_t>>();
      WriteRpc(buffer, messages);
      auto conn = *(server->GetFirstWorker()->conns_.begin());
      std::shared_ptr<Packet> heartbeat =
          std::make_shared<Packet>(buffer, Packet::rpc);
      if (!conn->SendCustomizedPackets(heartbeat, heartbeat->buffer)) {
        LOG(ERROR) << "failed to send heartbeat" << std::endl;
      }
      // Check for thread interruption
      boost::this_thread::interruption_point();
    }
  } catch (boost::thread_interrupted &) {
    std::cout << "Heartbeat thread interrupted and exiting..." << std::endl;
  }
}

void Datanode::NormalToEmergencyHook() {
  std::cout << "enter emergency mode" << std::endl;
  emergency.store(true);
  // periodically sending the heartbeat
  HeartbeatThread =
      new boost::thread(std::bind(&Datanode::SendHeartbeatEmergency, this));
}

void Datanode::EmergencyToNormalHook() {
  std::cout << "exit emergency mode" << std::endl;
  emergency.store(false);
  if (HeartbeatThread) {
    HeartbeatThread->interrupt(); // Interrupt the thread
    HeartbeatThread->join();      // Wait for thread to finish
    delete HeartbeatThread;
    HeartbeatThread = nullptr;
  }
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
    // This is because sometimes the packet will be split into two packets
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
      } else if (resp->tcp_type == Packet::ReplyStatus) {
        if (req->tcp_type != Packet::PacketHeader) {
          LOG(ERROR) << "not packet header" << std::endl;
          break;
        } else if (req->packet_header.lastpacketinblock()) {
          // last packet
          break;
        }
      } else {
        LOG(ERROR) << "Wrong response tcp type: " << req->tcp_type << std::endl;
        return {std::vector<std::shared_ptr<Packet>>{}, type};
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
      std::cout << "send heartbeat in normal mode" << std::endl;
      ReadDelimitedFrom(&coded_input, &HeartbeatRequest);
      rpc_request_header = req->RpcRequestHeader;
      requestHeaderProto = req->RequestHeader;
    } else if (req->RequestHeader.methodname() == "blockReport") {
      std::cout << "update based on the block report" << std::endl;
      initialized = true;
      hadoop::hdfs::datanode::BlockReportRequestProto BlockReportRequest;
      if (!ReadDelimitedFrom(&coded_input, &BlockReportRequest)) {
        LOG(ERROR) << "fail to parse the block report" << std::endl;
      }
      std::string poolId = BlockReportRequest.blockpoolid();
      std::cout << "poolId: " << poolId << std::endl;
      auto reports = BlockReportRequest.reports();
      for (auto report : reports) {
        // TODO: fix the bug that the size is always 0
        std::cout << "number of blocks: " << report.blocks().size()
                  << std::endl;
        for (auto i = 0; i < report.blocks().size(); i++) {
          std::cout << "get one block" << std::endl;
          long blockId = report.blocks()[0];
          auto blockinfo = report.blocksbuffers()[0];
          long *info_ptr = reinterpret_cast<long *>(blockinfo.data());
          long len = *(info_ptr + 1);
          long generationstamp = *(info_ptr + 2);
          hadoop::hdfs::ExtendedBlockProto block;
          block.set_poolid(poolId);
          block.set_blockid(blockId);
          block.set_generationstamp(generationstamp);
          block.set_numbytes(len);
          // BlockMap[std::make_pair(poolId, blockId)] = block;
          // std::cout << "map the block: " << blockId << std::endl;
        }
      }
    } else if (req->RequestHeader.methodname() == "registerDatanode") {
      std::cout << "update based on the register datanode" << std::endl;
      hadoop::hdfs::datanode::RegisterDatanodeRequestProto
          RegisterDatanodeRequest;
      if (!ReadDelimitedFrom(&coded_input, &RegisterDatanodeRequest)) {
        LOG(ERROR) << "fail to parse the register datanode" << std::endl;
      }
    } else if (req->RequestHeader.methodname() == "versionRequest") {
      std::cout << "proxy a version request" << std::endl;
      hadoop::hdfs::VersionRequestProto VersionRequest;
      if (!ReadDelimitedFrom(&coded_input, &VersionRequest)) {
        LOG(ERROR) << "fail to parse a version request" << std::endl;
      }
    } else if (req->RequestHeader.methodname() == "blockReceivedAndDeleted") {
      std::cout << "update based on the block received and deleted"
                << std::endl;
      hadoop::hdfs::datanode::BlockReceivedAndDeletedRequestProto
          BlockReceivedAndDeletedRequest;
      if (!ReadDelimitedFrom(&coded_input, &BlockReceivedAndDeletedRequest)) {
        LOG(ERROR) << "fail to parse the block received and deleted"
                   << std::endl;
      }
      std::string poolId = BlockReceivedAndDeletedRequest.blockpoolid();
      auto blocks = BlockReceivedAndDeletedRequest.blocks();      
      // TODO: update the block map
      // std::cout << BlockReceivedAndDeletedRequest.DebugString();
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
            << resp->block_op_response.readopchecksuminfo().checksum().type()
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
            req->buffer->data() + 3, static_cast<int>(req->buffer->size() - 3));
        google::protobuf::io::CodedInputStream coded_input(&array_input);
        hadoop::hdfs::OpReadBlockProto OpReadBlock;
        ReadDelimitedFrom(&coded_input, &OpReadBlock);
        hadoop::hdfs::ExtendedBlockProto block;
        block = OpReadBlock.header().baseheader().block();
        long blockId = static_cast<long>(block.blockid());
        long numbytes = static_cast<long>(block.numbytes());
        long generationstamp = static_cast<long>(block.generationstamp());
        std::string poolId = block.poolid();
        std::cout << "Read the block. Offset: " << OpReadBlock.offset()
                  << ", len: " << OpReadBlock.len() << ", poolId" << poolId
                  << ", block size: " << numbytes << ", blockId: " << blockId
                  << ", generationstamp: " << generationstamp << std::endl;
        // BlockMap[std::make_pair(poolId, blockId)] = block;
        // std::cout << "map the block: " << blockId << std::endl;
        break;
      }
      case Packet::WRITE_BLOCK: {
        google::protobuf::io::ArrayInputStream array_input(
            req->buffer->data() + 3, static_cast<int>(req->buffer->size() - 3));
        google::protobuf::io::CodedInputStream coded_input(&array_input);
        hadoop::hdfs::OpWriteBlockProto OpWriteBlock;
        ReadDelimitedFrom(&coded_input, &OpWriteBlock);
        hadoop::hdfs::ExtendedBlockProto block;
        block = OpWriteBlock.header().baseheader().block();
        long blockId = static_cast<long>(block.blockid());
        long numbytes = static_cast<long>(block.numbytes());
        long generationstamp = static_cast<long>(block.generationstamp());
        std::string poolId = block.poolid();
        std::cout << "Write the block. poolId: " << poolId
                  << ", block size: " << numbytes << ", blockId: " << blockId
                  << ", generationstamp: " << generationstamp << std::endl;
        // BlockMap[std::make_pair(poolId, blockId)] = block;
        // std::cout << "map the block: " << blockId << std::endl;
        break;
      }
      default:
        LOG(ERROR) << "unknown opcode" << std::endl;
        break;
      }
    }
  }
}

std::pair<Packet, bool> Datanode::EmergencyServe(std::shared_ptr<Packet> req, ConnectionInfo &conn, Cache *cache, Logger *logger, bool flow_control) {
  if (req->type == Packet::tcp) {
    Packet resp;
    resp.type = Packet::tcp;
    if (req->tcp_type == Packet::Op) {
      resp.tcp_type = Packet::BlockOpResponse;
      switch (req->opcode) {
      case Packet::READ_BLOCK: {
        resp.opcode = Packet::READ_BLOCK;
        std::cout << "try to read the disk" << std::endl;
        google::protobuf::io::ArrayInputStream array_input(
            req->buffer->data() + 3, static_cast<int>(req->buffer->size() - 3));
        google::protobuf::io::CodedInputStream coded_input(&array_input);
        hadoop::hdfs::OpReadBlockProto OpReadBlock;
        ReadDelimitedFrom(&coded_input, &OpReadBlock);
        hadoop::hdfs::ExtendedBlockProto TargetBlock =
            OpReadBlock.header().baseheader().block();
        std::streampos offset =
            static_cast<std::streampos>(OpReadBlock.offset());
        std::streamsize len = static_cast<std::streamsize>(OpReadBlock.len());

        hadoop::hdfs::ChecksumProto checksum;
        checksum.set_type(hadoop::hdfs::ChecksumTypeProto::CHECKSUM_CRC32C);
        checksum.set_bytesperchecksum(512);

        char checksumBuffer[12] = {0x0d, 0x08, 0x00, 0x22, 0x09, 0x0a, 0x05};
        checksum.SerializeToArray(checksumBuffer + 7, 5);
        
        int flags = fcntl(conn.client_fd, F_GETFL, 0);
        if (flags == -1) {
            perror("fcntl F_GETFL");
            return std::make_pair(resp, false);
        }

        flags &= ~O_NONBLOCK;  // Unset the O_NONBLOCK bit

        if (fcntl(conn.client_fd, F_SETFL, flags) == -1) {
            perror("fcntl F_SETFL");
            return std::make_pair(resp, false);
        }
        

        int sent = send(conn.client_fd, checksumBuffer, 12, 0);
        if (sent == -1) {
          perror("send checksum failed");
        } else {
          std::cout << "Sent " << sent << " bytes." << std::endl;
        }

        char OP_STATUS_SUCCESS[2] = {0x10, 0x00};
        sent = send(conn.client_fd, &OP_STATUS_SUCCESS, 2, 0);
        if (sent == -1) {
          perror("send OP_STATUS_SUCCESS failed");
        } else {
          std::cout << "Sent " << sent << " bytes." << std::endl;
        }
        
        int d1 = (TargetBlock.blockid() >> 16) & 0x1F;
        int d2 = (TargetBlock.blockid() >> 8) & 0x1F;
        std::string subdirs = "subdir" + std::to_string(d1) + "/subdir" + std::to_string(d2);

        std::string BlockPath = "/mnt/hdd/data/hdfs/datanode/current/" +
                                TargetBlock.poolid() +
                                "/current/finalized/" + subdirs + "/blk_" +
                                std::to_string(TargetBlock.blockid());
        std::string MetaPath = BlockPath + "_" +
                               std::to_string(TargetBlock.generationstamp()) +
                               ".meta";
        std::shared_ptr<std::ifstream> blockstream = std::make_shared<std::ifstream>(BlockPath, std::ios::binary | std::ios::in);
        std::shared_ptr<std::ifstream> metastream = std::make_shared<std::ifstream>(MetaPath, std::ios::binary | std::ios::in);

        if (!blockstream->is_open() || !metastream->is_open()) {
          LOG(ERROR) << "Failed to open the block: " << BlockPath << std::endl;
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        }
        blockstream->seekg(offset);
        if (!blockstream->good()) {
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        }

        unsigned char checksumHeader[7];
        metastream->read(reinterpret_cast<char*>(checksumHeader), 7);

        const size_t MAX_DATA_SIZE = 64 * 1024;
        char buffer[MAX_DATA_SIZE];

        auto remaining_bytes = len;
        uint64_t seqno = 0;

        while(remaining_bytes > 0) {
          hadoop::hdfs::PacketHeaderProto packet_header;

          size_t read_size = std::min(remaining_bytes, static_cast<long>(MAX_DATA_SIZE));

          unsigned char checksumBuf[size_t(ceil(read_size / 512.0) * 4)];
          
          metastream->read(reinterpret_cast<char*>(checksumBuf), sizeof(checksumBuf));
          if (!metastream->good()) {
            LOG(ERROR) << "Failed to read metadata from: " << MetaPath << std::endl;
            WriteErrorResponse(resp.buffer);
            return std::make_pair(resp, false);
          }

          blockstream->read(buffer, read_size);
          if (!blockstream->good()) {
            LOG(ERROR) << "Failed to read the block: " << BlockPath << std::endl;
            WriteErrorResponse(resp.buffer);
            return std::make_pair(resp, false);
          }

          uint32_t packet_size = 4 + read_size;
          packet_size += sizeof(checksumBuf);

          unsigned char packet_size_buffer[6];

          packet_size_buffer[0] = (packet_size >> 24) & 0xFF;
          packet_size_buffer[1] = (packet_size >> 16) & 0xFF;
          packet_size_buffer[2] = (packet_size >> 8) & 0xFF;
          packet_size_buffer[3] = packet_size & 0xFF;
          packet_size_buffer[4] = 0x0;
          packet_size_buffer[5] = 0x19;

          packet_header.set_datalen(read_size);
          packet_header.set_offsetinblock(offset + len - remaining_bytes);
          packet_header.set_seqno(seqno);
          packet_header.set_lastpacketinblock(false);

          std::vector<unsigned char> packet_header_buffer;
          packet_header_buffer.insert(packet_header_buffer.end(), packet_size_buffer, packet_size_buffer + 6);

          packet_header_buffer.resize(6 + packet_header.ByteSizeLong());

          packet_header.SerializeToArray(packet_header_buffer.data() + 6, packet_header.ByteSizeLong());
          
          packet_header_buffer.insert(packet_header_buffer.end(), checksumBuf, checksumBuf + sizeof(checksumBuf));

          int total_sent = 0;
          int to_send = packet_header_buffer.size();
          while (total_sent < to_send) {
            int sent = send(conn.client_fd, packet_header_buffer.data() + total_sent, to_send - total_sent, 0);
            if (sent == -1) {
              if (errno == EINTR) {
                continue; // Interrupted, try again
              } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // For non-blocking socket: wait for writable state
                // You might want to use select/poll/epoll here, or just break
                std::cerr << "Socket not ready for writing (EAGAIN/EWOULDBLOCK)." << std::endl;
                break;
              } else {
                perror("send packet header failed");
                break;
              }
            } else {
              total_sent += sent;
            }
          }
          std::cout << "Total bytes sent: " << total_sent << std::endl;

  
          total_sent = 0;
          to_send = read_size;
          while (total_sent < to_send) {
            int sent = send(conn.client_fd, buffer + total_sent, to_send - total_sent, 0);
            if (sent == -1) {
              if (errno == EINTR) {
                continue; // Interrupted, try again
              } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // For non-blocking socket: wait for writable state
                // You might want to use select/poll/epoll here, or just break
                std::cerr << "Socket not ready for writing (EAGAIN/EWOULDBLOCK)." << std::endl;
                break;
              } else {
                perror("send data failed");
                break;
              }
            } else {
              total_sent += sent;
            }
          }
          std::cout << "Total bytes sent: " << total_sent << std::endl;

          Packet packet = Packet();

          remaining_bytes -= read_size;
          seqno++;
        }

        hadoop::hdfs::PacketHeaderProto final_packet_header;
        final_packet_header.set_datalen(0);
        final_packet_header.set_offsetinblock(len);
        final_packet_header.set_seqno(seqno);
        final_packet_header.set_lastpacketinblock(true);

        std::vector<unsigned char> final_packet_header_buffer = {0x0, 0x0, 0x0, 0x04, 0x0, 0x19};
        final_packet_header_buffer.resize(6 + final_packet_header.ByteSizeLong());
        final_packet_header.SerializeToArray(final_packet_header_buffer.data() + 6, final_packet_header.ByteSizeLong());

        int total_sent = 0;
        int to_send = 6 + final_packet_header.ByteSizeLong();
        while (total_sent < to_send) {
          int sent = send(conn.client_fd, final_packet_header_buffer.data() + total_sent, to_send - total_sent, 0);
          if (sent == -1) {
            if (errno == EINTR) {
              continue; // Interrupted, try again
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // For non-blocking socket: wait for writable state
              // You might want to use select/poll/epoll here, or just break
              std::cerr << "Socket not ready for writing (EAGAIN/EWOULDBLOCK)." << std::endl;
              break;
            } else {
              perror("send final packet header failed");
              break;
            }
          } else {
            total_sent += sent;
          }
        }
        std::cout << "Total bytes sent: " << total_sent << std::endl;

        return std::make_pair(resp, false);
        break;
      }
      case Packet::WRITE_BLOCK: {
        uint32_t block_size = 0;
        resp.opcode = Packet::ZERO;
        std::cout << "try to write the disk" << std::endl;
        google::protobuf::io::ArrayInputStream array_input(
            req->buffer->data() + 3, static_cast<int>(req->buffer->size() - 3));
        google::protobuf::io::CodedInputStream coded_input(&array_input);
        hadoop::hdfs::OpWriteBlockProto OpWriteBlock;
        if (!ReadDelimitedFrom(&coded_input, &OpWriteBlock)) {
          LOG(ERROR) << "fail to read the write op" << std::endl;
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        }
        
        std::cout << "Original OpWriteBlock size: " << OpWriteBlock.ByteSizeLong() << ", OpWriteBlock:\n" << OpWriteBlock.DebugString() << std::endl;
        auto target = OpWriteBlock.header().baseheader().block();
        TargetMap[&conn] = OpWriteBlock;
        // BlockMap[std::make_pair(target.poolid(), target.blockid())] = target;

        auto DatanodeTargets = OpWriteBlock.targets();
        std::vector<int> datanodeSockfd(DatanodeTargets.size());

        for(int i = 0; i < datanodeSockfd.size(); i++) {
          auto NextDatanodeId = DatanodeTargets[i].id();
          const char *addr = NextDatanodeId.ipaddr().data();
          auto port = NextDatanodeId.xferport();
          datanodeSockfd[i] = socket(AF_INET, SOCK_STREAM, 0);
          if (datanodeSockfd[i] < 0) {
            LOG(ERROR) << "Socket creation failed for datanode " << i << std::endl;
            continue;
          }
          std::cout << "connect to datanode at " << addr << ":" << port << std::endl;
          sockaddr_in server_addr;
          memset(&server_addr, 0, sizeof(server_addr));
          server_addr.sin_family = AF_INET;
          server_addr.sin_port = htons(port);
          inet_pton(AF_INET, addr, &server_addr.sin_addr);
          if (connect(datanodeSockfd[i], (struct sockaddr *)&server_addr,
                      sizeof(server_addr)) < 0) {
            LOG(ERROR) << "Connection to datanode "<< i << " failed" << std::endl;
            close(datanodeSockfd[i]);
          }
        }

        OpWriteBlock.clear_targets();
        OpWriteBlock.clear_targetstoragetypes();
        OpWriteBlock.clear_targetpinnings();
        OpWriteBlock.clear_targetstorageids();

        std::cout << "replication number: " << DatanodeTargets.size()
                  << std::endl;

        hadoop::hdfs::DatanodeInfoProto* source = OpWriteBlock.mutable_source();
        
        // probably not needed till set_numblocks
        hadoop::hdfs::DatanodeIDProto* id = source->mutable_id();
        id->set_ipaddr("");
        id->set_hostname("");
        id->set_datanodeuuid("");
        id->set_xferport(0);
        id->set_infoport(0);
        id->set_ipcport(0);
        id->set_infosecureport(0);
        source->set_capacity(0);
        source->set_dfsused(0);
        source->set_remaining(0);
        source->set_blockpoolused(0);
        source->set_lastupdate(0);
        source->set_xceivercount(0);
        source->set_nondfsused(0);
        source->set_adminstate(hadoop::hdfs::DatanodeInfoProto::NORMAL);
        source->set_cachecapacity(0);
        source->set_cacheused(0);
        source->set_lastupdatemonotonic(0);
        source->set_lastblockreporttime(0);
        source->set_lastblockreportmonotonic(0);
        source->set_numblocks(0);
        OpWriteBlock.clear_targetstoragetypes();
        OpWriteBlock.clear_targetpinnings();
        OpWriteBlock.clear_targetstorageids();
        std::cout << "New OpWriteBlock size: " << OpWriteBlock.ByteSizeLong() << ", OpWriteBlock:\n" << OpWriteBlock.DebugString() << std::endl;

        std::shared_ptr<std::vector<uint8_t>> send_buffer = std::make_shared<std::vector<uint8_t>>();
        int OpWriteBlockSize = OpWriteBlock.ByteSizeLong();
        uint8_t appendBytes[] = {0x0, 0x1c, 0x50, static_cast<uint8_t>(OpWriteBlockSize & 0xFF), 0x1};
        size_t appendLength = sizeof(appendBytes);
        // Append the bytes to send_buffer
        send_buffer->insert(send_buffer->end(), appendBytes, appendBytes + appendLength);
        // WriteDelimitedTo(*send_buffer, &OpWriteBlock);
        send_buffer->resize(appendLength + OpWriteBlockSize);
        OpWriteBlock.SerializeToArray(send_buffer->data() + appendLength, OpWriteBlockSize);

        for(int i = 0; i < datanodeSockfd.size(); i++) {
          int sent = send(datanodeSockfd[i], send_buffer->data(), send_buffer->size(), 0);
          if (sent < 0) {
            LOG(ERROR) << "send failed" << std::endl;
            close(datanodeSockfd[i]);
          }

          char buffer[1024];
          memset(buffer, 0, sizeof(buffer));
          int bytes_received = recv(datanodeSockfd[i], buffer, sizeof(buffer) - 1, 0);
          if (bytes_received < 0) {
            LOG(ERROR) << "receive from datanode " << i << " failed" << std::endl;
            close(datanodeSockfd[i]);
          }
        }

        hadoop::hdfs::BlockOpResponseProto block_op_response;
        block_op_response.set_status(hadoop::hdfs::Status::SUCCESS);
        block_op_response.set_firstbadlink("");

        u_int8_t size = block_op_response.ByteSizeLong();

        std::vector<uint8_t> ack_buffer;
        ack_buffer.resize(1 + size);
        ack_buffer[0] = size;
        block_op_response.SerializeToArray(ack_buffer.data() + 1, size);
        
        printf("client_fd: %d", conn.client_fd);

        int sent = send(conn.client_fd, ack_buffer.data(), ack_buffer.size(), 0);
        if (sent == -1) {
          LOG(ERROR) <<  "send ack failed";
        } else {
          std::cout << "Sent " << sent << " bytes." << std::endl;
        }

        int d1 = (target.blockid() >> 16) & 0x1F;
        int d2 = (target.blockid() >> 8) & 0x1F;
        std::string BlockPath = "/mnt/hdd/data/hdfs/datanode/current/" + target.poolid() + "/current/finalized/subdir" + std::to_string(d1);
        if (!std::filesystem::exists(BlockPath)) {
          if (std::filesystem::create_directories(BlockPath)) {
              std::cout << "Directory created: " << BlockPath << std::endl;
          } else {
              std::cerr << "Failed to create directory." << std::endl;
          }
        }

        BlockPath += "/subdir" + std::to_string(d2);
        if (!std::filesystem::exists(BlockPath)) {
          if (std::filesystem::create_directories(BlockPath)) {
              std::cout << "Directory created: " << BlockPath << std::endl;
          } else {
              std::cerr << "Failed to create directory." << std::endl;
          }
        }

        BlockPath += "/blk_" + std::to_string(target.blockid());
        std::string MetaPath = BlockPath + "_" + std::to_string(target.generationstamp()) + ".meta";

        std::shared_ptr<std::ofstream> blockstream = std::make_shared<std::ofstream>(BlockPath, std::ios::binary | std::ios::out);
        std::shared_ptr<std::ofstream> metastream = std::make_shared<std::ofstream>(MetaPath, std::ios::binary | std::ios::out);

        while(true) {
          uint32_t net_packet_size;
          ssize_t bytes_recv = 0;
          size_t total_received = 0;
          char* buffer = reinterpret_cast<char*>(&net_packet_size);

          while (total_received < sizeof(net_packet_size)) {
              bytes_recv = recv(conn.client_fd, buffer + total_received,
                                sizeof(net_packet_size) - total_received, 0);

              if (bytes_recv > 0) {
                  total_received += bytes_recv;
              } else if (bytes_recv == 0) {
                  std::cerr << "Connection closed by peer." << std::endl;
                  return std::make_pair(resp, false);
              } else {
                  if (errno == EAGAIN || errno == EWOULDBLOCK) {
                      // std::cout << "EAGAIN: Retrying..." << std::endl;
                      usleep(100);
                      continue;
                  } else {
                      perror("recv packet size failed");
                      return std::make_pair(resp, false);
                  }
              }
          }

          uint32_t packet_size = ntohl(net_packet_size);
          std::cout << "packet_size: " << packet_size << std::endl;

          unsigned char packet_header_buffer[27];
          total_received = 0;
          while (total_received < 27) {
              bytes_recv = recv(conn.client_fd, packet_header_buffer + total_received,
                                27 - total_received, 0);

              if (bytes_recv > 0) {
                  total_received += bytes_recv;
              } else if (bytes_recv == 0) {
                  std::cerr << "Connection closed by peer." << std::endl;
                  return std::make_pair(resp, false);
              } else {
                  if (errno == EAGAIN || errno == EWOULDBLOCK) {
                      // std::cout << "EAGAIN: Retrying..." << std::endl;
                      usleep(100);
                      continue;
                  } else {
                      perror("recv packet size failed");
                      return std::make_pair(resp, false);
                  }
              }
          }

          hadoop::hdfs::PacketHeaderProto packet_header;
          packet_header.ParseFromArray(packet_header_buffer + 2, 25);

          std::cout << "packet_header: " << packet_header.DebugString() << std::endl;

          uint32_t checksum_size = packet_size - packet_header.datalen() - 4;
          block_size += packet_header.datalen();

          unsigned char data_buffer[checksum_size + packet_header.datalen()];
          total_received = 0;
          while (total_received < checksum_size + packet_header.datalen()) {
              bytes_recv = recv(conn.client_fd, data_buffer + total_received,
                                checksum_size + packet_header.datalen() - total_received, 0);

              if (bytes_recv > 0) {
                  total_received += bytes_recv;
              } else if (bytes_recv == 0) {
                  std::cerr << "Connection closed by peer." << std::endl;
                  return std::make_pair(resp, false);
              } else {
                  if (errno == EAGAIN || errno == EWOULDBLOCK) {
                      // std::cout << "EAGAIN: Retrying..." << std::endl;
                      usleep(100);
                      continue;
                  } else {
                      perror("recv packet size failed");
                      return std::make_pair(resp, false);
                  }
              }
          }

          char send_buffer_2[sizeof(net_packet_size) + 27 + total_received];
          std::memcpy(send_buffer_2, &net_packet_size, sizeof(net_packet_size));
          std::memcpy(send_buffer_2 + sizeof(net_packet_size), packet_header_buffer, sizeof(packet_header_buffer));
          std::memcpy(send_buffer_2 + sizeof(net_packet_size) + 27, data_buffer, total_received);
          
          for(int i = 0; i < datanodeSockfd.size(); i++) {
            int sent = send(datanodeSockfd[i], send_buffer_2, sizeof(net_packet_size) + 27 + total_received, 0);
            if (sent < 0) {
              LOG(ERROR) << "send failed" << std::endl;
              close(datanodeSockfd[i]);
            }
  
            char buffer[1024];
            memset(buffer, 0, sizeof(buffer));
            int bytes_received = recv(datanodeSockfd[i], buffer, sizeof(buffer) - 1, 0);
            if (bytes_received < 0) {
              LOG(ERROR) << "receive from datanode " << i << " failed" << std::endl;
              close(datanodeSockfd[i]);
            }
          }

          if (!blockstream->is_open() || !metastream->is_open()) {
              LOG(ERROR) << "Failed to open the block for writing: " << BlockPath << std::endl;
              WriteErrorResponse(resp.buffer);
              return std::make_pair(resp, false);
          }

          blockstream->write(reinterpret_cast<const char*>(data_buffer + checksum_size), packet_header.datalen());
          if (!blockstream->good()) {
            LOG(ERROR) << "Failed to write to the block: " << BlockPath << std::endl;
            WriteErrorResponse(resp.buffer);
            return std::make_pair(resp, false);
          }
          

          if (packet_header.seqno() == 0) {
            unsigned char checksumHeader[] = {0x00, 0x01, 0x02, 0x00, 0x00, 0x02, 0x00};
            metastream->write(reinterpret_cast<const char*>(checksumHeader), 7);
          }

          metastream->write(reinterpret_cast<const char*>(data_buffer), checksum_size);
          if (!metastream->good()) {
            LOG(ERROR) << "Failed to write to the meta: " << MetaPath << std::endl;
            WriteErrorResponse(resp.buffer);
            return std::make_pair(resp, false);
          }

          hadoop::hdfs::PipelineAckProto pipelineAck;
          pipelineAck.set_seqno(packet_header.seqno());
          pipelineAck.add_reply(hadoop::hdfs::Status::SUCCESS);

          unsigned char pipelineAckBuffer[1 + pipelineAck.ByteSizeLong() + 5];

          pipelineAck.SerializeToArray(pipelineAckBuffer + 1, pipelineAck.ByteSizeLong());
          if (static_cast<uint8_t>(pipelineAck.ByteSizeLong()) + 5 > 255) {
            std::cerr << "Warning: ByteSizeLong() too large to store in one byte!" << std::endl;
          } else {
            pipelineAckBuffer[0] = static_cast<uint8_t>(pipelineAck.ByteSizeLong() + 5);
          }
          unsigned char additionalData[5] = {0x18, 0x00, 0x22, 0x01, 0x00};

          std::memcpy(&pipelineAckBuffer[pipelineAck.ByteSizeLong() + 1], additionalData, sizeof(additionalData));
          
          std::cout << "pipelineAck: " << pipelineAck.DebugString();

          sent = send(conn.client_fd, pipelineAckBuffer, sizeof(pipelineAckBuffer), 0);

          int err_count = 0;
          while (sent < 0 and err_count < 5) {
            LOG(ERROR) << "Failed to send pipelineAck" << "send() failed with error code: " << errno << std::endl;
            // WriteErrorResponse(resp.buffer);
            // return std::make_pair(resp, false);
            sent = send(conn.client_fd, pipelineAckBuffer, sizeof(pipelineAckBuffer), 0);
            err_count += 1;
          }
          
          std::cout << "sent " << sent << " bytes" << std::endl;

          if(packet_header.lastpacketinblock()) {
            break;
          }
        }

        blockstream->flush();
        metastream->flush();

        hadoop::hdfs::datanode::BlockReceivedAndDeletedRequestProto blockReceivedAndDeletedRequest;

        auto* registration = blockReceivedAndDeletedRequest.mutable_registration();
        blockReceivedAndDeletedRequest.set_blockpoolid(target.poolid());
        auto* datanodeID = registration->mutable_datanodeid();
        datanodeID->set_ipaddr("10.10.1.2");
        datanodeID->set_hostname("node1");
        datanodeID->set_datanodeuuid("d660df90-c60d-458d-b3cb-38261b459007");
        datanodeID->set_xferport(9866);
        datanodeID->set_infoport(9864);
        datanodeID->set_ipcport(9867);
        datanodeID->set_infosecureport(0);

        auto* storageInfo = registration->mutable_storageinfo();
        storageInfo->set_layoutversion(-57);
        storageInfo->set_namespceid(1084605719);
        storageInfo->set_clusterid("CID-4bb27076-ddb4-4740-9027-cc95653aedaa");
        storageInfo->set_ctime(1744664128720);

        auto* keys = registration->mutable_keys();
        keys->set_isblocktokenenabled(false);
        keys->set_keyupdateinterval(0);
        keys->set_tokenlifetime(0);
        auto* currentKey = keys->mutable_currentkey();
        currentKey->set_keyid(0);
        currentKey->set_expirydate(0);
        currentKey->set_keybytes("");

        registration->set_softwareversion("3.3.6");

        auto* blockReport = blockReceivedAndDeletedRequest.add_blocks();
        blockReport->set_storageuuid(OpWriteBlock.storageid());

        auto* blockReportEntry = blockReport->add_blocks();
        auto* block = blockReportEntry->mutable_block();
        block->set_blockid(OpWriteBlock.header().baseheader().block().blockid());
        block->set_genstamp(OpWriteBlock.header().baseheader().block().generationstamp());
        block->set_numbytes(block_size);
        blockReportEntry->set_status(hadoop::hdfs::datanode::ReceivedDeletedBlockInfoProto::RECEIVED);

        auto* storage = blockReport->mutable_storage();
        storage->set_storageuuid(OpWriteBlock.storageid());
        storage->set_state(hadoop::hdfs::DatanodeStorageProto::NORMAL);
        storage->set_storagetype(hadoop::hdfs::StorageTypeProto::DISK);
        
        uint8_t blockReceivedAndDeletedRequestHeader[] = {0x0, 0x0, 0x1, 0xa4, 0x1a, 0x8, 0x2, 0x10, 0x0, 0x18, 0xe, 0x22, 0x10, 0x4a, 0xf5, 0x7b, 0xa9, 0xa5, 0x6, 0x45, 0xc2, 0xa9, 0x85, 0xfd, 0x24, 0x68, 0x70, 0x8d, 0xf8, 0x28, 0x0, 0x54, 0xa, 0x17, 0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x52, 0x65, 0x63, 0x65, 0x69, 0x76, 0x65, 0x64, 0x41, 0x6e, 0x64, 0x44, 0x65, 0x6c, 0x65, 0x74, 0x65, 0x64, 0x12, 0x37, 0x6f, 0x72, 0x67, 0x2e, 0x61, 0x70, 0x61, 0x63, 0x68, 0x65, 0x2e, 0x68, 0x61, 0x64, 0x6f, 0x6f, 0x70, 0x2e, 0x68, 0x64, 0x66, 0x73, 0x2e, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x63, 0x6f, 0x6c, 0x2e, 0x44, 0x61, 0x74, 0x61, 0x6e, 0x6f, 0x64, 0x65, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x63, 0x6f, 0x6c, 0x18, 0x1, 0xb2, 0x2};

        std::shared_ptr<std::vector<uint8_t>> blockReceivedAndDeletedRequestBuffer = 
            std::make_shared<std::vector<uint8_t>>();

        blockReceivedAndDeletedRequestBuffer->resize(sizeof(blockReceivedAndDeletedRequestHeader) + blockReceivedAndDeletedRequest.ByteSizeLong());

        memcpy(blockReceivedAndDeletedRequestBuffer->data(), blockReceivedAndDeletedRequestHeader, sizeof(blockReceivedAndDeletedRequestHeader));

        blockReceivedAndDeletedRequest.SerializeToArray(
            blockReceivedAndDeletedRequestBuffer->data() + sizeof(blockReceivedAndDeletedRequestHeader),
            blockReceivedAndDeletedRequest.ByteSizeLong());

        auto conn = *(server->GetFirstWorker()->conns_.begin());
        std::shared_ptr<Packet> finalResponse = std::make_shared<Packet>(blockReceivedAndDeletedRequestBuffer, Packet::tcp);

        if (!conn->SendCustomizedPackets(finalResponse, finalResponse->buffer)) {
            LOG(ERROR) << "Failed to send finalResponse" << std::endl;
        }

        // close(conn.client_fd);
        return std::make_pair(resp, false);
        break;
      }
      default:
        break;
      }
    } else if (req->tcp_type == Packet::PacketHeader) {
      resp.tcp_type = Packet::PacketHeader;
      uint32_t packet_length =
          ntohl(*(reinterpret_cast<uint32_t *>(req->buffer->data())));
      short head_len =
          ntohs(*(reinterpret_cast<short *>(req->buffer->data() + 4)));
      google::protobuf::io::ArrayInputStream array_input(
          req->buffer->data() + 6, head_len);
      google::protobuf::io::CodedInputStream coded_input(&array_input);
      hadoop::hdfs::PacketHeaderProto packet_header;
      packet_header.MergeFromCodedStream(&coded_input);
      std::streampos offset =
          static_cast<std::streampos>(packet_header.offsetinblock());
      auto datalen = packet_header.datalen();
      auto it = TargetMap.find(&conn);
      if (it == TargetMap.end()) {
        LOG(ERROR) << "no such block" << std::endl;
        WriteErrorResponse(resp.buffer);
        return std::make_pair(resp, false);
      }
      auto OpWriteBlock = it->second;
      auto target = OpWriteBlock.header().baseheader().block();

      if (!packet_header.lastpacketinblock()) {
        int d1 = (target.blockid() >> 16) & 0x1F;
        int d2 = (target.blockid() >> 8) & 0x1F;
        std::string BlockPath = "/mnt/hdd/data/hdfs/datanode/current/" + target.poolid() + "/current/finalized/subdir" + std::to_string(d1);
        if (!std::filesystem::exists(BlockPath)) {
          if (std::filesystem::create_directories(BlockPath)) {
              std::cout << "Directory created: " << BlockPath << std::endl;
          } else {
              std::cerr << "Failed to create directory." << std::endl;
          }
        }

        BlockPath += "/subdir" + std::to_string(d2);
        if (!std::filesystem::exists(BlockPath)) {
          if (std::filesystem::create_directories(BlockPath)) {
              std::cout << "Directory created: " << BlockPath << std::endl;
          } else {
              std::cerr << "Failed to create directory." << std::endl;
          }
        }
        std::string MetaPath = BlockPath + "_" +
                               std::to_string(target.generationstamp()) +
                               ".meta";
        std::shared_ptr<std::ofstream> blockstream;
        std::shared_ptr<std::ofstream> metastream;
        BlockGuard block_guard(BlockPath, blockstream);
        BlockGuard meta_guard(MetaPath, metastream);
        if (!blockstream->is_open() || !metastream->is_open()) {
          LOG(ERROR) << "Failed to open the block: " << BlockPath << std::endl;
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        }
        blockstream->seekp(offset);
        metastream->seekp(0, std::ios::end);
        int metasize = std::ceil(datalen / 512) * 4;
        if (packet_header.seqno() == 0) {
          short version = 1;
          uint8_t type = 2;
          int bpc = 512;
          metastream->write(reinterpret_cast<char *>(&version), 2);
          metastream->write(reinterpret_cast<char *>(&type), 1);
          metastream->write(reinterpret_cast<char *>(&bpc), 4);
        }
        metastream->write(reinterpret_cast<char *>(req->buffer->data()) + 6 +
                              head_len,
                          metasize);
        blockstream->write(reinterpret_cast<char *>(req->buffer->data()) + 6 +
                               head_len + metasize,
                           datalen);
        std::cout << "write the block" << std::endl;
        if (!blockstream || !metastream) {
          LOG(ERROR) << "Failed to write the block: " << BlockPath << std::endl;
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        }
        metastream->close();
        blockstream->close();
      }
      // replication
      hadoop::hdfs::PipelineAckProto PipelineAck;
      auto DatanodeTargets = OpWriteBlock.targets();
      std::cout << "targets size: " << DatanodeTargets.size() << std::endl;
      if (DatanodeTargets.empty()) {
        resp.block_op_response.set_status(hadoop::hdfs::SUCCESS);
        return std::make_pair(resp, false);
      } else {
        auto NextDatanodeId = DatanodeTargets[0].id();
        const char *addr = NextDatanodeId.ipaddr().data();
        auto port = NextDatanodeId.xferport();
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
          LOG(ERROR) << "Socket creation failed" << std::endl;
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        }
        sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, addr, &server_addr.sin_addr);
        if (connect(sockfd, (struct sockaddr *)&server_addr,
                    sizeof(server_addr)) < 0) {
          LOG(ERROR) << "Connection to next datanode failed" << std::endl;
          close(sockfd);
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        }
        if (send(sockfd, req->buffer->data(), req->buffer->size(), 0) < 0) {
          LOG(ERROR) << "send failed" << std::endl;
          close(sockfd);
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        }
        std::cout << "send success " << std::endl;
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0) {
          LOG(ERROR) << "receive failed" << std::endl;
          close(sockfd);
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        }
        // close(sockfd);
        google::protobuf::io::ArrayInputStream array_input(buffer,
                                                           bytes_received);
        google::protobuf::io::CodedInputStream coded_input(&array_input);
        if (!ReadDelimitedFrom(&coded_input, &PipelineAck)) {
          WriteErrorResponse(resp.buffer);
          return std::make_pair(resp, false);
        } else {
          PipelineAck.add_reply(hadoop::hdfs::SUCCESS);
          WriteDelimitedTo(resp.buffer, &PipelineAck);
        }
      }
      return std::make_pair(resp, false);
    }
  }
  else if (req->type == Packet::rpc) {
    // deserialize the packet and output the codes
    std::cout << "rpc request" << std::endl;
  }
  return std::make_pair(Packet(), false);
}
