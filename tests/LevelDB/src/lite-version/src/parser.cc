#include "parser.hpp"

lite::DeserializeResult RESPTypeParser::Deserialize(InputIterator &begin,
                                                    InputIterator end) {
  if (begin == end) return lite::kIndeterminate;

  if (!value_) {
    switch (*(begin++)) {
      case '+':
        value_ = std::shared_ptr<RESPType>{
            dynamic_cast<RESPType *>(new RESPSimpleString)};
        parser_ = std::make_unique<RESPSimpleStringParser>();
      case '-':
        value_ =
            std::shared_ptr<RESPType>{dynamic_cast<RESPType *>(new RESPError)};
        parser_ = std::make_unique<RESPSimpleStringParser>();

      case ':':
        value_ = std::shared_ptr<RESPType>{
            dynamic_cast<RESPType *>(new RESPInteger)};
        parser_ = std::make_unique<RESPIntegerParser>();

      case '$':
        value_ = std::shared_ptr<RESPType>{
            dynamic_cast<RESPType *>(new RESPBulkString)};
        parser_ = std::make_unique<RESPBulkStringParser>();

      case '*':
        value_ =
            std::shared_ptr<RESPType>{dynamic_cast<RESPType *>(new RESPArray)};
        parser_ = std::make_unique<RESPArrayParser>();
      default:
        std::cerr << "Unknown RESPType: " << *(begin - 1) << std::endl;
        return lite::kBad;
    }
  }
  return parser_->Deserialize(begin, end, *value_);
}