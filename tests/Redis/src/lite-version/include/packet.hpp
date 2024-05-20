#pragma once

#include <event.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <lite.hpp>
#include <map>
#include <memory>
#include <set>
#include <list>
#include <string>
#include <vector>

using InputIterator = uint8_t *;

struct RESPType
{
    virtual ~RESPType(){};

    virtual void AppendToBuffer(std::vector<uint8_t> &buffer) {};
};

struct RESPString : public RESPType
{
    std::shared_ptr<std::string> value = std::make_shared<std::string>();

    RESPString() = default;
    RESPString(const std::shared_ptr<std::string> &value) : value(value)
    {
    }
    virtual ~RESPString(){};
};
struct RESPSimpleString : public RESPString
{
    virtual ~RESPSimpleString(){};
    RESPSimpleString() = default;
    RESPSimpleString(const std::shared_ptr<std::string> &value) : RESPString(value)
    {
    }

    virtual void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};
struct RESPError : public RESPString
{
    RESPError() = default;
    RESPError(const std::shared_ptr<std::string> &value) : RESPString(value)
    {
    }
    virtual ~RESPError(){};

    virtual void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};
struct RESPBulkString : public RESPString
{
    RESPBulkString() = default;
    RESPBulkString(const std::shared_ptr<std::string> &value) : RESPString(value)
    {
    }
    virtual ~RESPBulkString(){};

    virtual void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPInteger : public RESPType
{
    int64_t value = 0;

    RESPInteger() = default;
    RESPInteger(int64_t value) : value(value)
    {
    }
    RESPInteger(const std::shared_ptr<std::string> &value)
    {
        this->value = std::stoll(*value);
    }

    virtual ~RESPInteger(){};

    virtual void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPArray : public RESPType
{
    std::vector<std::unique_ptr<RESPType>> value;

    virtual ~RESPArray(){};

    virtual void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPNull : public RESPType
{
    char value = 0;

    virtual ~RESPNull(){};

    virtual void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPMap : public RESPType
{
    std::shared_ptr<std::map<std::unique_ptr<RESPType>, std::unique_ptr<RESPType>>> value =
        std::make_shared<std::map<std::unique_ptr<RESPType>, std::unique_ptr<RESPType>>>();

    RESPMap() = default;
    RESPMap(const std::shared_ptr<std::map<std::unique_ptr<RESPType>, std::unique_ptr<RESPType>>> &value) : value(value)
    {
    }
    virtual ~RESPMap(){};

    virtual void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

struct RESPSet : public RESPType
{
    std::shared_ptr<std::set<std::unique_ptr<RESPType>>> value =
        std::make_shared<std::set<std::unique_ptr<RESPType>>>();

    RESPSet() = default;
    RESPSet(const std::shared_ptr<std::set<std::unique_ptr<RESPType>>> &value) : value(value)
    {
    }
    virtual ~RESPSet(){};

    virtual void AppendToBuffer(std::vector<uint8_t> &buffer) override;
};

class RESPTypeParser
{
    std::unique_ptr<RESPTypeParser> parser_ = nullptr;

  public:
    std::unique_ptr<RESPType> value_ = nullptr;

    virtual lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end, RESPType &value)
    {
        std::cerr << "RESPTypeParser::Deserialize" << std::endl;
        return lite::kBad;
    }
    lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);
};

struct Packet
{
    std::unique_ptr<RESPType> command;
    std::shared_ptr<RESPTypeParser> parser;

    Packet() : command(nullptr), parser(std::make_unique<RESPTypeParser>())
    {
    }
    Packet(std::unique_ptr<RESPType> command) : command(std::move(command))
    {
    }
    Packet(std::unique_ptr<RESPArray> command) : command(std::move(command))
    {
    }

    lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);

    std::shared_ptr<std::vector<uint8_t>> Serialize() const
    {
        std::vector<uint8_t> buffer;
        command->AppendToBuffer(buffer);
        return std::make_shared<std::vector<uint8_t>>(buffer);
    }

    std::string_view GetOpcode() const
    {
        try
        {
            auto opcode = dynamic_cast<RESPBulkString *>((dynamic_cast<RESPArray *>(command.get())->value)[0].get());
            return std::string_view(*opcode->value);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Unknown opcode: " << e.what() << std::endl;
            throw std::runtime_error("Invalid opcode");
        }
    }

    size_t GetArgNum() const
    {
        return dynamic_cast<RESPArray *>(command.get())->value.size() - 1;
    }

    RESPType *GetArg(size_t index) const
    {
        return (dynamic_cast<RESPArray *>(command.get())->value)[index + 1].get();
    }
};
