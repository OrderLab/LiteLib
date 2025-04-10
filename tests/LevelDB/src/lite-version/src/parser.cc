#include "parser.hpp"

lite::DeserializeResult RESPTypeParser::Deserialize(InputIterator &begin,
                                                    InputIterator end) {
  if (begin == end) return lite::kIndeterminate;

  if (!value_) {
    switch (*(begin++)) {
      case '+':
        value_ = std::unique_ptr<RESPType>{
            dynamic_cast<RESPType *>(new RESPSimpleString)};
        parser_ = std::make_unique<RESPSimpleStringParser>();
        break;
      case '-':
        value_ =
            std::unique_ptr<RESPType>{dynamic_cast<RESPType *>(new RESPError)};
        parser_ = std::make_unique<RESPSimpleStringParser>();
        break;
      case ':':
        value_ = std::unique_ptr<RESPType>{
            dynamic_cast<RESPType *>(new RESPInteger)};
        parser_ = std::make_unique<RESPIntegerParser>();
        break;
      case '$':
        value_ = std::unique_ptr<RESPType>{
            dynamic_cast<RESPType *>(new RESPBulkString)};
        parser_ = std::make_unique<RESPBulkStringParser>();
        break;
      case '*':
        value_ =
            std::unique_ptr<RESPType>{dynamic_cast<RESPType *>(new RESPArray)};
        parser_ = std::make_unique<RESPArrayParser>();
        break;
      default:
        LOG(ERROR) << "Unknown RESPType: " << *(begin - 1) << "length: " << end - begin << std::endl;
        for(int i = 0; i < end - begin; i++){
          if (isprint(*(begin + i))) {
            LOG(ERROR) << *(begin + i);
          } else {
            LOG(ERROR) << "0x" << std::hex << (int)(unsigned char)*(begin + i);
          }
        }
        return lite::kBad;
    }
  }
  return parser_->Deserialize(begin, end, *value_);
}