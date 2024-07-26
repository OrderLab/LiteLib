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
      // case '_':
      //   value_ =
      //       std::unique_ptr<RESPType>{dynamic_cast<RESPType *>(new RESPNull)};
      //   parser_ = std::make_unique<RESPNullParser>();
      //   break;
      // case '%':
      //   value_ =
      //       std::unique_ptr<RESPType>{dynamic_cast<RESPType *>(new RESPMap)};
      //   parser_ = std::make_unique<RESPMapParser>();
      //   break;
      // case '~':
      //   value_ =
      //       std::unique_ptr<RESPType>{dynamic_cast<RESPType *>(new RESPSet)};
      //   parser_ = std::make_unique<RESPSetParser>();
      //   break;
      default:
        std::cerr << "Unknown RESPType: " << *(begin - 1) << std::endl;
        return lite::kBad;
    }
  }
  return parser_->Deserialize(begin, end, *value_);
}