#include "parser.hpp"

lite::DeserializeResult RESPParser::Deserialize(InputIterator &begin,
                                                InputIterator end) {
  if (begin == end) return lite::kIndeterminate;

  if (!value_) {
    switch (*(begin++)) {
      case '+':
        value_ = {
            shm->get_segment_manager()->template construct<RESPSimpleString>(
                bip::anonymous_instance)(),
            RESPTypeDeleter{shm->get_segment_manager()}};
        parser_ = {shm->get_segment_manager()
                       ->template construct<RESPSimpleStringParser>(
                           bip::anonymous_instance)(),
                   RESPTypeParserDeleter{shm->get_segment_manager()}};
        break;
      case '-':
        value_ = {shm->get_segment_manager()->template construct<RESPError>(
                      bip::anonymous_instance)(),
                  RESPTypeDeleter{shm->get_segment_manager()}};
        parser_ = {shm->get_segment_manager()
                       ->template construct<RESPSimpleStringParser>(
                           bip::anonymous_instance)(),
                   RESPTypeParserDeleter{shm->get_segment_manager()}};
        break;
      case ':':
        value_ = {shm->get_segment_manager()->template construct<RESPInteger>(
                      bip::anonymous_instance)(),
                  RESPTypeDeleter{shm->get_segment_manager()}};
        parser_ = {
            shm->get_segment_manager()->template construct<RESPIntegerParser>(
                bip::anonymous_instance)(),
            RESPTypeParserDeleter{shm->get_segment_manager()}};
        break;
      case '$':
        value_ = {
            shm->get_segment_manager()->template construct<RESPBulkString>(
                bip::anonymous_instance)(),
            RESPTypeDeleter{shm->get_segment_manager()}};
        parser_ = {shm->get_segment_manager()
                       ->template construct<RESPBulkStringParser>(
                           bip::anonymous_instance)(),
                   RESPTypeParserDeleter{shm->get_segment_manager()}};
        break;
      case '*':
        value_ = {shm->get_segment_manager()->template construct<RESPArray>(
                      bip::anonymous_instance)(),
                  RESPTypeDeleter{shm->get_segment_manager()}};
        parser_ = {
            shm->get_segment_manager()->template construct<RESPArrayParser>(
                bip::anonymous_instance)(),
            RESPTypeParserDeleter{shm->get_segment_manager()}};
        break;
      // case '_':
      //   value_ = ShmMakeUnique(shm->get_segment_manager()->template
      //   construct<RESPNull>(bip::anonymous_instance)(), *shm); parser_ =
      //   ShmMakeUnique(shm->get_segment_manager()->template
      //   construct<RESPNullParser>(bip::anonymous_instance)(), *shm); break;
      // case '%':
      //   value_ = ShmMakeUnique(shm->get_segment_manager()->template
      //   construct<RESPMap>(bip::anonymous_instance)(), *shm); parser_ =
      //   ShmMakeUnique(shm->get_segment_manager()->template
      //   construct<RESPMapParser>(bip::anonymous_instance)(), *shm); break;
      // case '~':
      //   value_ = ShmMakeUnique(shm->get_segment_manager()->template
      //   construct<RESPSet>(bip::anonymous_instance)(), *shm); parser_ =
      //   ShmMakeUnique(shm->get_segment_manager()->template
      //   construct<RESPSetParser>(bip::anonymous_instance)(), *shm); break;
      default:
        std::cerr << "Unknown RESPType: " << *(begin - 1) << std::endl;
        return lite::kBad;
    }
  }
  return parser_->Deserialize(begin, end, *value_);
}