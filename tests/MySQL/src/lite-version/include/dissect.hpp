#pragma once

#include "packet.hpp"

class ResponseDissector {
  int8_t inter_eof_cnt = 0;

 public:
  typedef enum mysql_state {
    INIT,
    NORMAL,
    BEFORE_INTERMEDIATE_EOF,
    END,
  } mysql_state_t;

  mysql_state_t state_ = INIT;
  bool Digest(std::shared_ptr<Packet> resp,
              std::vector<std::shared_ptr<Packet>>
                  &responses);  // true: end of the response
};
