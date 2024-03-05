#include "request_handler.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "packet.hpp"

namespace memcached {
namespace server {

void RequestHandler::HandleRequest(const Packet &req,
                                   std::vector<Packet> &responses,
                                   bool &is_quit, bool &is_quite) {
  // std::cerr << "Request: \n" << req << std::endl;
  if (req.header.magic != 0x80) {
    std::cerr << "Unsupported Protocol Version:\n" << req << std::endl;
    // TODO: error handling
    exit(1);
  }
  Packet resp;
  resp.header.magic = 0x81;
  resp.header.opaque = req.header.opaque;
  resp.header.opcode = req.header.opcode;
  is_quit = false;
  is_quite = false;
  CacheEntry entry;
  switch (req.header.opcode) {
    case Header::kSet:
      // TODO: CAS, Expiration
      entry.value = req.value;
      for (size_t i = 0; i < 4; i++) entry.flags.push_back(req.extra[i]);
      entry.CAS = req.header.CAS != 0 ? req.header.CAS
                                      : 1;  // TODO: what to do if CAS = 0
      if (!cache_.Add(req.key, entry) && !cache_.Replace(req.key, entry)) {
        resp.header.status = 0x0005;  // TODO: error code
        break;
      }
      break;
    case Header::kAdd:
      // TODO: CAS, Expiration
      entry.value = req.value;
      for (size_t i = 0; i < 4; i++) entry.flags.push_back(req.extra[i]);
      entry.CAS = req.header.CAS != 0 ? req.header.CAS
                                      : 1;  // TODO: what to do if CAS = 0
      if (!cache_.Add(req.key, entry)) {
        resp.header.status = 0x0005;  // TODO: error code
        break;
      }
      break;
    case Header::kReplace:  // TODO
      // TODO: CAS, Expiration, Error
      entry.value = req.value;
      for (size_t i = 0; i < 4; i++) entry.flags.push_back(req.extra[i]);
      entry.CAS = req.header.CAS != 0 ? req.header.CAS
                                      : 1;  // TODO: what to do if CAS = 0
      if (!cache_.Replace(req.key, entry)) {
        resp.header.status = 0x0005;  // TODO: error code
      }
      break;
    case Header::kNoOp:
      break;
    case Header::kGetKQ:
      is_quite = true;
    case Header::kGetK:
      resp.key = req.key;
    case Header::kGet:
      if (!cache_.Get(req.key, entry)) {
        resp.header.status = 0x0001;
        resp.value = std::make_shared<std::vector<uint8_t>>(kNotFound_);
      } else {
        resp.value = entry.value;
        resp.header.CAS = entry.CAS;
        resp.extra = entry.flags;
        resp.header.extras_length = 4;
      }
      resp.header.key_length = resp.key.size();
      resp.header.total_body_length = resp.value->size() +
                                      resp.header.key_length +
                                      resp.header.extras_length;
      break;
    case Header::kQuit:
      is_quit = true;
      break;
    default:
      // TODO: more operations
      std::cerr << "Unsupported Opcode:\n" << req << std::endl;
      exit(1);
  }
  if (!is_quit) {
    // std::cerr << "Response: \n" << resp << std::endl;
    // responses_.push_back(std::move(resp)); TODO
    responses.push_back(resp);
  }
}

}  // namespace server
}  // namespace memcached
