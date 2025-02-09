#pragma once

#include <event.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <lite.hpp>

extern bip::managed_shared_memory *shm;

using InputIterator = uint8_t *;

struct RESPType {
  virtual ~RESPType() {};
  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) {};
};

struct RESPString : public RESPType {
  ShmSharedPtr<ShmString> value;

  RESPString() = default;
  RESPString(const ShmSharedPtr<ShmString> &value) : value(value) {}
  virtual ~RESPString() {};
};
struct RESPSimpleString : public RESPString {
  virtual ~RESPSimpleString() {};
  RESPSimpleString() = default;
  RESPSimpleString(const ShmSharedPtr<ShmString> &value) : RESPString(value) {}

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
};
struct RESPError : public RESPString {
  RESPError() = default;
  RESPError(const ShmSharedPtr<ShmString> &value) : RESPString(value) {}
  virtual ~RESPError() {};

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
};
struct RESPBulkString : public RESPString {
  RESPBulkString() = default;
  RESPBulkString(const ShmSharedPtr<ShmString> &value) : RESPString(value) {}
  virtual ~RESPBulkString() {};

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
};

struct RESPInteger : public RESPType {
  int64_t value = 0;

  RESPInteger() = default;
  RESPInteger(int64_t value) : value(value) {}
  RESPInteger(const ShmSharedPtr<ShmString> &value) {
    this->value = std::stoll(*(value.get()));
  }

  virtual ~RESPInteger() {};

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
};

struct RESPArray : public RESPType {
  ShmVector<ShmUniquePtr<RESPType>> value;

  virtual ~RESPArray() {};

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
};

struct RESPNull : public RESPType {
  char value = 0;

  virtual ~RESPNull() {};

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
};

struct RESPMap : public RESPType {
  ShmSharedPtr<ShmMap<ShmUniquePtr<RESPType>, ShmUniquePtr<RESPType>>> value;

  RESPMap() = default;
  RESPMap(
      const ShmSharedPtr<ShmMap<ShmUniquePtr<RESPType>, ShmUniquePtr<RESPType>>>
          &value)
      : value(value) {}
  virtual ~RESPMap() {};

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
};

struct RESPSet : public RESPType {
  ShmSharedPtr<ShmSet<ShmUniquePtr<RESPType>>> value;

  RESPSet() = default;
  RESPSet(const ShmSharedPtr<ShmSet<ShmUniquePtr<RESPType>>> &value)
      : value(value) {}
  virtual ~RESPSet() {};

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
};

class RESPTypeParser {
  ShmUniquePtr<RESPTypeParser> parser_;

 public:
  ShmUniquePtr<RESPType> value_;

  virtual ~RESPTypeParser() = default;
  virtual lite::DeserializeResult Deserialize(InputIterator &begin,
                                              InputIterator end,
                                              RESPType &value) {
    std::cerr << "RESPTypeParser::Deserialize" << std::endl;
    return lite::kBad;
  }
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);
};

struct Packet {
  ShmUniquePtr<RESPType> command;
  ShmSharedPtr<RESPTypeParser> parser;

  Packet()
      : command(nullptr),
        parser(
            ShmMakeShared(shm->get_segment_manager()->construct<RESPTypeParser>(
                              bip::anonymous_instance)(),
                          *shm)) {}
  Packet(ShmUniquePtr<RESPType> command) : command(boost::move(command)) {}
  Packet(ShmUniquePtr<RESPArray> command) : command(boost::move(command)) {}

  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);

  ShmSharedPtr<ShmVector<uint8_t>> Serialize() const {
    ShmVector<uint8_t> *buffer =
        shm->get_segment_manager()->construct<ShmVector<uint8_t>>(
            bip::anonymous_instance)();
    command->AppendToBuffer(buffer);
    return ShmMakeShared(buffer, *shm);
  }

  std::string_view GetOpcode() const {
    try {
      auto opcode = dynamic_cast<RESPBulkString *>(
          (dynamic_cast<RESPArray *>(command.get())->value)[0].get());
      return std::string_view(*opcode->value);
    } catch (const std::exception &e) {
      std::cerr << "Unknown opcode: " << e.what() << std::endl;
      throw std::runtime_error("Invalid opcode");
    }
  }

  size_t GetArgNum() const {
    return dynamic_cast<RESPArray *>(command.get())->value.size() - 1;
  }

  RESPType *GetArg(size_t index) const {
    return (dynamic_cast<RESPArray *>(command.get())->value)[index + 1].get();
  }
};
