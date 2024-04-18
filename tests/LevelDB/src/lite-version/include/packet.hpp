#pragma once

#include <event.h>

#include <algorithm>
#include <concept.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using InputIterator = uint8_t *;

struct RESPType {
  virtual ~RESPType(){};

  virtual void AppendToBuffer(std::vector<uint8_t> &buffer){};
};

struct RESPString : public RESPType {
  std::shared_ptr<std::string> value = std::make_shared<std::string>();

  virtual ~RESPString(){};
};
struct RESPSimpleString : public RESPString {
  virtual ~RESPSimpleString(){};

  std::pair<lite::DeserializeResult, RESPSimpleString *> Deserialize(
      InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};
struct RESPError : public RESPString {
  virtual ~RESPError(){};

  std::pair<lite::DeserializeResult, RESPError *> Deserialize(
      InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};
struct RESPBulkString : public RESPString {
  virtual ~RESPBulkString(){};

  std::pair<lite::DeserializeResult, RESPBulkString *> Deserialize(
      InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPInteger : public RESPType {
  int64_t value = 0;

  virtual ~RESPInteger(){};

  std::pair<lite::DeserializeResult, RESPInteger *> Deserialize(
      InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPArray : public RESPType {
  std::shared_ptr<std::vector<std::shared_ptr<RESPType>>> value =
      std::make_shared<std::vector<std::shared_ptr<RESPType>>>();

  virtual ~RESPArray(){};

  std::pair<lite::DeserializeResult, RESPArray *> Deserialize(
      InputIterator &begin, InputIterator end);
  void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

class RESPTypeParser;

struct Packet {
  std::unique_ptr<RESPType> command;
  std::unique_ptr<RESPTypeParser> parser;

  Packet() : command(nullptr), parser(std::make_unique<RESPTypeParser>()) {}
  Packet(std::unique_ptr<RESPType> command) : command(std::move(command)) {}
  Packet(std::unique_ptr<RESPArray> command) : command(std::move(command)) {}

  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);

  std::shared_ptr<std::vector<uint8_t>> Serialize() const {
    std::vector<uint8_t> buffer;
    command->AppendToBuffer(buffer);
    return std::make_shared<std::vector<uint8_t>>(buffer);
  }

  std::string_view GetOpcode() const {
    auto opcode = dynamic_cast<RESPBulkString *>(
        (*dynamic_cast<RESPArray *>(command.get())->value)[0].get());
    if (opcode == nullptr) {
      throw std::runtime_error("Invalid opcode");
    }
    return std::string_view(*opcode->value);
  }

  size_t GetArgNum() const {
    return dynamic_cast<RESPArray *>(command.get())->value->size() - 1;
  }

  std::shared_ptr<RESPType> GetArg(size_t index) const {
    return (*dynamic_cast<RESPArray *>(command.get())->value)[index + 1];
  }
};
