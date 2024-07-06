#include "dissect.hpp"

#include <lite.hpp>

#include "mysql-server/protocol_classic.hpp"

// https://github.com/wireshark/wireshark/blob/c7745b4dde687f90450f3cbf3a1601df3eeecf7f/epan/dissectors/packet-mysql.c#L3019
bool ResponseDissector::Digest(
    std::shared_ptr<Packet> resp,
    std::vector<std::shared_ptr<Packet>> &responses) {
  if (state_ == END) {
    inter_eof_cnt = 0;
    responses.clear();
  }
  responses.push_back(resp);

  if (state_ == INIT) {
    state_ = END;
    return true;  // server greeting
  }

  uint8_t response_code = (*resp->buffer)[4];

  switch (response_code) {
    case 0xff:
      state_ = END;
      return true;
    case 0x00:
      if (resp->buffer->size() == 11) {
        state_ = END;
        return true;
      } else if (resp->buffer->size() ==
                 16) {  // OK of request prepared statement
        const uint16_t number_of_fields = uint2korr(&((*resp->buffer)[9]));
        const uint16_t number_of_params = uint2korr(&((*resp->buffer)[11]));
        if (number_of_fields > 0 || number_of_params > 0) {
          state_ = NORMAL;
          return false;
        } else {
          state_ = END;
          return true;
        }
      } else {  // has augmented packet
        state_ = NORMAL;
        return false;
      }
    case 0xfe:
      if (resp->buffer->size() > 9) {
        // NOT a EOF Packet
        state_ = END;
        return true;  // ignore AUTH_SWITCH_REQUEST
      }
      if (state_ == BEFORE_INTERMEDIATE_EOF && inter_eof_cnt < 1) {
        inter_eof_cnt++;
        state_ = NORMAL;
        return false;
      } else {
        state_ = END;
        return true;
      }
    case 0x03:
      state_ = BEFORE_INTERMEDIATE_EOF;
      return false;
    default:
      return false;
  }
}
