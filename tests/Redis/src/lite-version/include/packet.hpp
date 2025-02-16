#pragma once

#include <event.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <lite.hpp>

extern SharedMemory *shm;

using InputIterator = uint8_t *;

struct RESPType {
  RESPType(ShmVoidAllocator allocator = shm->get_segment_manager()) {}
  virtual ~RESPType() {};
  virtual void Destructor(SegmentManager *seg_mgr) {
    LOG(ERROR) << "RESPType::Destructor" << std::endl;
    seg_mgr->destroy_ptr(this);
  }
  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) const {};
};

class RESPTypeDeleter {
  SegmentManager *seg_mgr;

 public:
  RESPTypeDeleter(SegmentManager *seg_mgr) : seg_mgr(seg_mgr) {}

  void operator()(RESPType *ptr) {
    if (!ptr) return;
    ptr->Destructor(seg_mgr);
  }
};

struct RESPString : public RESPType {
  ShmSharedPtr<ShmString> value;

  RESPString(ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPType(allocator),
        value(ShmMakeShared(
            shm->get_segment_manager()->template construct<ShmString>(
                bip::anonymous_instance)(allocator),
            *shm)) {}
  RESPString(const ShmSharedPtr<ShmString> &value,
             ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPType(allocator), value(value) {}
  virtual ~RESPString() {};
  virtual void Destructor(SegmentManager *seg_mgr) override {
    seg_mgr->destroy_ptr(this);
  }
};
struct RESPSimpleString : public RESPString {
  virtual ~RESPSimpleString() {};
  RESPSimpleString(ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPString(allocator) {}
  RESPSimpleString(const ShmSharedPtr<ShmString> &value,
                   ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPString(value, allocator) {}

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) const override;
  virtual void Destructor(SegmentManager *seg_mgr) override {
    seg_mgr->destroy_ptr(this);
  }
};
struct RESPError : public RESPString {
  RESPError(ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPString(allocator) {}
  RESPError(const ShmSharedPtr<ShmString> &value,
            ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPString(value, allocator) {}
  virtual ~RESPError() {};
  virtual void Destructor(SegmentManager *seg_mgr) override {
    seg_mgr->destroy_ptr(this);
  }

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) const override;
};
struct RESPBulkString : public RESPString {
  RESPBulkString(ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPString(allocator) {}
  RESPBulkString(const ShmSharedPtr<ShmString> &value,
                 ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPString(value, allocator) {}
  virtual ~RESPBulkString() {};
  virtual void Destructor(SegmentManager *seg_mgr) override {
    seg_mgr->destroy_ptr(this);
  }

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) const override;
};

struct RESPInteger : public RESPType {
  int64_t value = 0;

  RESPInteger(ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPType(allocator) {}
  RESPInteger(int64_t value,
              ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPType(allocator), value(value) {}
  RESPInteger(const ShmSharedPtr<ShmString> &value,
              ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPType(allocator) {
    this->value = std::stoll(value->c_str());
  }

  virtual ~RESPInteger() {};
  virtual void Destructor(SegmentManager *seg_mgr) override {
    seg_mgr->destroy_ptr(this);
  }

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) const override;
};

struct RESPArray : public RESPType {
  ShmVector<ShmUniquePtrWithDeleter<RESPType, RESPTypeDeleter>> value;

  RESPArray(ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPType(allocator), value(allocator) {}
  virtual ~RESPArray() {};
  virtual void Destructor(SegmentManager *seg_mgr) override {
    seg_mgr->destroy_ptr(this);
  }

  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) const override;
};

// struct RESPNull : public RESPType {
//   char value = 0;

//   virtual ~RESPNull() {};
//   virtual void Destructor(SegmentManager* seg_mgr) override {
//     seg_mgr->destroy_ptr(this);
//   }
//   virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
// };

using MapType =
    ShmMap<ShmString, ShmUniquePtrWithDeleter<RESPType, RESPTypeDeleter>>;
struct RESPMap : public RESPType {
  ShmSharedPtr<MapType> value;

  RESPMap(ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPType(allocator) {
    MapType *map = allocator.get_segment_manager()->template construct<MapType>(
        bip::anonymous_instance)(allocator);
    value = ShmMakeShared(map, *allocator.get_segment_manager());
  }

  RESPMap(const ShmSharedPtr<MapType> &value,
          ShmVoidAllocator allocator = shm->get_segment_manager())
      : RESPType(allocator), value(value) {}
  virtual ~RESPMap() {};
  virtual void Destructor(SegmentManager *seg_mgr) override {
    seg_mgr->destroy_ptr(this);
  }
  virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) const override;
};

// struct RESPSet : public RESPType {
//   ShmSharedPtr<ShmSet<ShmUniquePtr<RESPType>>> value;

//   RESPSet() = default;
//   RESPSet(const ShmSharedPtr<ShmSet<ShmUniquePtr<RESPType>>> &value)
//       : value(value) {}
//   virtual ~RESPSet() {};
//   virtual void Destructor(SegmentManager* seg_mgr) override {
//     seg_mgr->destroy_ptr(this);
//   }
//   virtual void AppendToBuffer(ShmVector<uint8_t> &buffer) override;
// };

class RESPTypeParser {
 public:
  RESPTypeParser(ShmVoidAllocator allocator = shm->get_segment_manager()) {}
  virtual ~RESPTypeParser() {};
  virtual lite::DeserializeResult Deserialize(InputIterator &begin,
                                              InputIterator end,
                                              RESPType &value) {
    std::cerr << "RESPTypeParser::Deserialize" << std::endl;
    return lite::kBad;
  }
  virtual void Destructor(SegmentManager *seg_mgr) {
    LOG(ERROR) << "RESPTypeParser::Destructor" << std::endl;
    seg_mgr->destroy_ptr(this);
  }
};

class RESPTypeParserDeleter {
  SegmentManager *seg_mgr;

 public:
  RESPTypeParserDeleter(SegmentManager *seg_mgr) : seg_mgr(seg_mgr) {}
  void operator()(RESPTypeParser *ptr) { ptr->Destructor(seg_mgr); }
};

class RESPParser {
  ShmUniquePtrWithDeleter<RESPTypeParser, RESPTypeParserDeleter> parser_;

 public:
  ShmUniquePtrWithDeleter<RESPType, RESPTypeDeleter> value_;

  RESPParser(ShmVoidAllocator allocator = shm->get_segment_manager())
      : parser_(nullptr,
                RESPTypeParserDeleter{allocator.get_segment_manager()}),
        value_(nullptr, RESPTypeDeleter{allocator.get_segment_manager()}) {}
  ~RESPParser() {};

  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);
};

struct Packet {
  ShmUniquePtrWithDeleter<RESPType, RESPTypeDeleter> command;
  ShmSharedPtr<RESPParser> parser;

  Packet(ShmVoidAllocator allocator = shm->get_segment_manager())
      : command(nullptr, RESPTypeDeleter{allocator.get_segment_manager()}),
        parser(ShmMakeShared(
            allocator.get_segment_manager()->template construct<RESPParser>(
                bip::anonymous_instance)(allocator),
            *allocator.get_segment_manager())) {}

  Packet(ShmUniquePtrWithDeleter<RESPType, RESPTypeDeleter> command,
         ShmVoidAllocator allocator = shm->get_segment_manager())
      : command(boost::move(command)) {}

  Packet(ShmUniquePtrWithDeleter<RESPArray, RESPTypeDeleter> command,
         ShmVoidAllocator allocator = shm->get_segment_manager())
      : command({command.release(),
                 RESPTypeDeleter{allocator.get_segment_manager()}}) {}

  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end);

  ShmSharedPtr<ShmVector<uint8_t>> Serialize() const {
    ShmVector<uint8_t> *buffer =
        shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
            bip::anonymous_instance)(shm->get_segment_manager());
    command->AppendToBuffer(*buffer);
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
