#include "packet.hpp"

#include <arpa/inet.h>

#include <string>

std::vector<uint8_t> Header::kNotFound = {'N', 'o', 't', ' ', 'F',
                                          'o', 'u', 'n', 'd'};

// template <typename T>
// T ReverseBuffer(T buf, size_t len) {
//   for (size_t i = 0; i < len / 2; ++i) {
//     std::swap(reinterpret_cast<char *>(buf.data())[i],
//               reinterpret_cast<char *>(buf.data())[len - i - 1]);
//   }
//   return buf;
// }

template <typename T>
inline void ReverseAppendBuffer(std::vector<uint8_t> &buffer, const T *data,
                                size_t len) {
  const uint8_t *now = reinterpret_cast<const uint8_t *>(data) + len;
  while (len--) buffer.push_back(*--now);
}

inline void AppendBuffer(std::vector<uint8_t> &buffer,
                         std::vector<uint8_t> &data) {
  buffer.insert(buffer.end(), data.begin(), data.end());
}

inline void AppendBuffer(std::vector<uint8_t> &buffer,
                         std::vector<uint8_t> *data) {
  if (!data) return;
  buffer.insert(buffer.end(), data->begin(), data->end());
}

inline uint64_t ntohll(const uint64_t &x) {
  return (((uint64_t)htonl(x)) << 32) + (htonl((x) >> 32));
}

uint32_t Packet::GetOpaque() const {
  return ntohl(*reinterpret_cast<const uint32_t *>(&(*buffer)[12]));
}

uint16_t Packet::GetStatus() const {
  return ntohs(*reinterpret_cast<const uint16_t *>(&(*buffer)[6]));
}


ParsedPacket::ParsedPacket(const Packet &packet) {
  const auto &data = *(packet.buffer);
  header.magic = data[0];
  header.opcode = data[1];
  header.key_length = ntohs(*reinterpret_cast<const uint16_t *>(&data[2]));
  header.extras_length = data[4];
  header.data_type = data[5];
  header.status = ntohs(*reinterpret_cast<const uint16_t *>(&data[6]));
  header.total_body_length =
      ntohl(*reinterpret_cast<const uint32_t *>(&data[8]));
  header.opaque = ntohl(*reinterpret_cast<const uint32_t *>(&data[12]));
  header.CAS = ntohll(*reinterpret_cast<const uint64_t *>(&data[16]));
  extra = std::make_shared<std::vector<uint8_t>>(
      data.begin() + 24, data.begin() + 24 + header.extras_length);
  key = std::make_shared<std::vector<uint8_t>>(
      data.begin() + 24 + header.extras_length,
      data.begin() + 24 + header.extras_length + header.key_length);
  value = std::make_shared<std::vector<uint8_t>>(
      data.begin() + 24 + header.extras_length + header.key_length, data.end());
}

std::shared_ptr<std::vector<uint8_t>> ParsedPacket::Serialize() {
  if (!buffer->empty()) {
    return buffer;
  }
  auto buffers = std::make_shared<std::vector<uint8_t>>();
  ToBuffers(*buffers);
  return buffers;
}

void ParsedPacket::ToBuffers(std::vector<uint8_t> &buffers) {
  if (!buffer->empty()) {
    AppendBuffer(buffers, buffer.get());
    return;
  }
  buffers.reserve(buffers.size() + 24 + (extra ? extra->size() : 0) +
                  (key ? key->size() : 0) + (value ? value->size() : 0));
  ReverseAppendBuffer(buffers, &header.magic, sizeof(header.magic));
  ReverseAppendBuffer(buffers, &header.opcode, sizeof(header.opcode));
  ReverseAppendBuffer(buffers, &header.key_length, sizeof(header.key_length));
  ReverseAppendBuffer(buffers, &header.extras_length,
                      sizeof(header.extras_length));
  ReverseAppendBuffer(buffers, &header.data_type, sizeof(header.data_type));
  ReverseAppendBuffer(buffers, &header.status, sizeof(header.status));
  ReverseAppendBuffer(buffers, &header.total_body_length,
                      sizeof(header.total_body_length));
  ReverseAppendBuffer(buffers, &header.opaque, sizeof(header.opaque));
  ReverseAppendBuffer(buffers, &header.CAS, sizeof(header.CAS));
  AppendBuffer(buffers, extra.get());
  AppendBuffer(buffers, key.get());
  AppendBuffer(buffers, value.get());

  // buffers.insert(buffers.end(), &header.magic, sizeof(header.magic));
  // buffers.push_back(boost::asio::mutable_buffer(&header.opcode,
  // sizeof(header.opcode))); buffers.push_back(ReverseBuffer(
  //     boost::asio::mutable_buffer(&header.key_length,
  //     sizeof(header.key_length)), sizeof(header.key_length)));
  // buffers.push_back(
  //     boost::asio::mutable_buffer(&header.extras_length,
  //     sizeof(header.extras_length)));
  // buffers.push_back(
  //     boost::asio::mutable_buffer(&header.data_type,
  //     sizeof(header.data_type)));
  // buffers.push_back(
  //     ReverseBuffer(boost::asio::mutable_buffer(&header.status,
  //     sizeof(header.status)),
  //                   sizeof(header.status)));
  // buffers.push_back(
  //     ReverseBuffer(boost::asio::mutable_buffer(&header.total_body_length,
  //                                       sizeof(header.total_body_length)),
  //                   sizeof(header.total_body_length)));
  // buffers.push_back(
  //     ReverseBuffer(boost::asio::mutable_buffer(&header.opaque,
  //     sizeof(header.opaque)),
  //                   sizeof(header.opaque)));
  // buffers.push_back(
  //     ReverseBuffer(boost::asio::mutable_buffer(&header.CAS,
  //     sizeof(header.CAS)),
  //                   sizeof(header.CAS)));
  // buffers.push_back(boost::asio::mutable_buffer(extra.data(), extra.size()));
  // buffers.push_back(boost::asio::mutable_buffer(key.data(), key.size()));
  // buffers.push_back(boost::asio::mutable_buffer(value.data(), value.size()));

  // std::cerr << "ToBuffers: " << std::endl;
  // for (auto &buffer : buffers) {
  //   for (const u_char *c = (const u_char *)buffer.data();
  //        c != (const u_char *)buffer.data() + buffer.size(); ++c) {
  //     std::cerr << std::hex << "0x" << static_cast<uint32_t>(*c) << std::dec
  //               << " ";
  //   }
  //   std::cerr << std::endl;
  // }
}
