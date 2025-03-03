#pragma once

#include <event.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "service.hpp"

using InputIterator = uint8_t *;

struct RESPPacket {
  int argc = 0;
  ShmVector<ShmString> argv;
  EmbeddedRequest *request = nullptr;
  bool is_generated_in_emergency_mode = true;

  ShmSharedPtr<ShmVector<uint8_t>> buffer;

  RESPPacket(ShmVoidAllocator allocator = shm->get_segment_manager())
      : argv(allocator) {}

  RESPPacket(EmbeddedRequest *request,
             ShmVoidAllocator allocator = shm->get_segment_manager())
      : argv(allocator),
        request(request),
        is_generated_in_emergency_mode(false) {
    // TODO: actually copy the request, otherwise when the full is killed and
    // it's in a transaction, it can't be processed in emergency mode
  }

  RESPPacket(RESPPacket &&other) noexcept
      : argc(other.argc),
        argv(std::move(other.argv)),
        request(other.request),
        is_generated_in_emergency_mode(other.is_generated_in_emergency_mode),
        buffer(std::move(other.buffer)) {
    // prevent double-free from RequestDestructor
    other.is_generated_in_emergency_mode = true;
    other.request = nullptr;
  }

  ~RESPPacket() {
    if (Likely(!is_generated_in_emergency_mode)) {
      Redis::RequestDestructor(request);
    } else {
      FreeEmbeddedRequestGeneratedInEmergencyMode();
    }
  }

  // RESPPacket(MapType &map,
  //            ShmVoidAllocator allocator = shm->get_segment_manager())
  //     : argv(allocator),
  //       buffer(ShmMakeShared(
  //           shm->get_segment_manager()->template
  //           construct<ShmVector<uint8_t>>(
  //               bip::anonymous_instance)(allocator),
  //           *allocator.get_segment_manager()));

  // int processMultibulkBuffer(client *c) in redis/src/networking.c
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end) {
    if (argc == 0) {
      if (*begin != '*') return lite::kBad;
      auto newline = std::find(begin, end, '\r');
      if (newline + 1 >= end) return lite::kIndeterminate;
      if (*(newline + 1) != '\n') return lite::kBad;
      argc = std::stoi(std::string(begin + 1, newline));
      begin = newline + 2;
    }
    while (argv.size() < argc) {
      if (begin == end) return lite::kIndeterminate;
      if (*begin != '$') return lite::kBad;
      auto newline = std::find(begin, end, '\r');
      if (newline + 1 >= end) return lite::kIndeterminate;
      if (*(newline + 1) != '\n') return lite::kBad;
      auto len = std::stoi(std::string(begin + 1, newline));
      newline += 2;
      if (newline + len + 1 >= end) return lite::kIndeterminate;
      if (*(newline + len) != '\r' || *(newline + len + 1) != '\n')
        return lite::kBad;
      argv.emplace_back(newline, newline + len, shm->get_segment_manager());
      begin = newline + len + 2;
    }
    return lite::kGood;
  }

  ShmSharedPtr<ShmVector<uint8_t>> Serialize() const { return buffer; }

  EmbeddedRequest *ToEmbeddedRequest() {
    if (request) return request;
    request = new EmbeddedRequest;
    std::string cmd(argv[0]);
    std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (cmd == "hmset") {
      request->type = EmbeddedRequestType::kHset;
    } else if (cmd == "hgetall") {
      request->type = EmbeddedRequestType::kHgetall;
    } else if (cmd == "hset") {
      request->type = EmbeddedRequestType::kHset;
    } else if (cmd == "quit") {
      request->type = EmbeddedRequestType::kQuit;
    } else if (cmd == "multi") {
      request->type = EmbeddedRequestType::kMulti;
    } else if (cmd == "exec") {
      request->type = EmbeddedRequestType::kExec;
    } else if (cmd == "discard") {
      request->type = EmbeddedRequestType::kDiscard;
    } else if (cmd == "set") {
      request->type = EmbeddedRequestType::kSet;
    } else if (cmd == "get") {
      request->type = EmbeddedRequestType::kGet;
    } else if (cmd == "ping") {
      request->type = EmbeddedRequestType::kPing;
    } else {
      LOG(ERROR) << "Unknown opcode: " << argv[0] << std::endl;
    }
    request->argc = argc;
    request->argv = new robj *[argc];
    request->argv_len = new int[argc];
    for (int i = 0; i < argc; i++) {
      request->argv[i] = new robj;
      request->argv[i]->ptr = argv[i].data();
      request->argv_len[i] = argv[i].size();
    }
    return request;
  }

  void FreeEmbeddedRequestGeneratedInEmergencyMode() {
    if (!request) return;
    for (int i = 0; i < argc; i++) {
      delete request->argv[i];
    }
    delete[] request->argv;
    delete[] request->argv_len;
    delete request;
  }

  static RESPPacket ResponseNull(
      ShmVoidAllocator allocator = shm->get_segment_manager()) {
    RESPPacket packet(allocator);
    packet.buffer = ShmMakeShared(
        shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
            bip::anonymous_instance)(allocator),
        *allocator.get_segment_manager());
    static const char null_str[] = "$-1\r\n";
    packet.buffer->insert(packet.buffer->end(), null_str, null_str + 5);
    return packet;
  }

  static RESPPacket ResponseSimpleString(
      const std::string &simple_string,
      ShmVoidAllocator allocator = shm->get_segment_manager()) {
    RESPPacket packet(allocator);
    packet.buffer = ShmMakeShared(
        shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
            bip::anonymous_instance)(allocator),
        *allocator.get_segment_manager());
    packet.buffer->reserve(simple_string.size() + 3);
    packet.buffer->push_back('+');
    packet.buffer->insert(packet.buffer->end(), simple_string.begin(),
                          simple_string.end());
    packet.AddResponseNewline();
    return packet;
  }

  static RESPPacket ResponseError(
      const std::string &error_msg,
      ShmVoidAllocator allocator = shm->get_segment_manager()) {
    RESPPacket packet(allocator);
    packet.buffer = ShmMakeShared(
        shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
            bip::anonymous_instance)(allocator),
        *allocator.get_segment_manager());
    packet.buffer->reserve(error_msg.size() + 3);
    packet.buffer->push_back('-');
    packet.buffer->insert(packet.buffer->end(), error_msg.begin(),
                          error_msg.end());
    packet.AddResponseNewline();
    return packet;
  }

  void InitResponseArray(
      const int len, ShmVoidAllocator allocator = shm->get_segment_manager()) {
    buffer = ShmMakeShared(
        shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
            bip::anonymous_instance)(allocator),
        *allocator.get_segment_manager());
    std::string len_str = std::to_string(len);
    buffer->reserve(len + len_str.size() + 3);
    buffer->push_back('*');
    buffer->insert(buffer->end(), len_str.begin(), len_str.end());
    AddResponseNewline();
  }

  static RESPPacket ResponseArray(
      const int len, ShmVoidAllocator allocator = shm->get_segment_manager()) {
    RESPPacket packet(allocator);
    packet.InitResponseArray(len, allocator);
    return packet;
  }

  void AddResponseArrayElement(const char *s, size_t len) {
    std::string len_str = std::to_string(len);
    buffer->reserve(buffer->size() + len + len_str.size() + 5);
    buffer->push_back('$');
    buffer->insert(buffer->end(), len_str.begin(), len_str.end());
    AddResponseNewline();
    buffer->insert(buffer->end(), s, s + len);
    AddResponseNewline();
  }

  void AddResponseArrayElement(const uint8_t *s, size_t len) {
    AddResponseArrayElement(reinterpret_cast<const char *>(s), len);
  }

  void AddResponseArrayElement(const char *s) {
    AddResponseArrayElement(s, strlen(s));
  }

  static RESPPacket ResponseBulkString(
      const char *bulk_string, size_t len,
      ShmVoidAllocator allocator = shm->get_segment_manager()) {
    if (len == 0) {
      return ResponseNull(allocator);
    }
    RESPPacket packet(allocator);
    packet.buffer = ShmMakeShared(
        shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
            bip::anonymous_instance)(allocator),
        *allocator.get_segment_manager());
    packet.AddResponseArrayElement(bulk_string, len);
    return packet;
  }

 private:
  void AddResponseNewline() {
    buffer->push_back('\r');
    buffer->push_back('\n');
  }
};
