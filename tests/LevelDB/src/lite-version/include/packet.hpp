#pragma once

#include <event.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using InputIterator = uint8_t *;

struct RESPType {
  virtual ~RESPType(){};

  static RESPType *Parse(InputIterator &begin, InputIterator end);
  virtual void AppendToBuffer(std::vector<uint8_t> &buffer) = 0;
};

struct RESPString : public RESPType {
  std::shared_ptr<std::string> value;

  RESPString(std::shared_ptr<std::string> value) : value(value) {}
  virtual ~RESPString(){};
};

struct RESPSimpleString : public RESPString {
  RESPSimpleString(std::shared_ptr<std::string> value) : RESPString(value) {}
  virtual ~RESPSimpleString(){};

  static RESPSimpleString *Parse(InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPError : public RESPType {
  std::shared_ptr<std::string> value;

  RESPError(std::shared_ptr<std::string> value) : value(value) {}
  virtual ~RESPError(){};

  static RESPError *Parse(InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPInteger : public RESPType {
  int64_t value;

  RESPInteger(int value) : value(value) {}
  virtual ~RESPInteger(){};

  static RESPInteger *Parse(InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPBulkString : public RESPString {
  RESPBulkString(std::shared_ptr<std::string> value) : RESPString(value) {}
  virtual ~RESPBulkString(){};

  static RESPBulkString *Parse(InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPArray : public RESPType {
  std::shared_ptr<std::vector<std::shared_ptr<RESPType>>> value;

  RESPArray() {}
  RESPArray(std::shared_ptr<std::vector<std::shared_ptr<RESPType>>> value)
      : value(value) {}
  virtual ~RESPArray(){};

  static RESPArray *Parse(InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

class Connection;

struct Packet {
  Connection *connection;

  std::unique_ptr<RESPArray> command;

  Packet() : command(nullptr) {}

  bool Parse(InputIterator &begin, InputIterator end) {
    std::unique_ptr<RESPArray> new_command(
        dynamic_cast<RESPArray *>(RESPType::Parse(begin, end)));
    if (new_command == nullptr) {
      return false;
    }
    command = std::move(new_command);
    auto opcode = dynamic_cast<RESPBulkString *>((*command->value)[0].get());
    if (opcode == nullptr) {
      return false;
    }
    auto &data = opcode->value;
    std::transform(data->begin(), data->end(), data->begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return true;
  }

  void AppendToBuffer(std::vector<uint8_t> &buffer) {
    command->AppendToBuffer(buffer);
  }

  std::string_view GetOpcode() const {
    auto opcode = dynamic_cast<RESPBulkString *>((*command->value)[0].get());
    if (opcode == nullptr) {
      throw std::runtime_error("Invalid opcode");
    }
    return std::string_view(*opcode->value);
  }

  size_t GetArgNum() const { return command->value->size() - 1; }

  std::shared_ptr<RESPType> GetArg(size_t index) const {
    return (*command->value)[index + 1];
  }
};
