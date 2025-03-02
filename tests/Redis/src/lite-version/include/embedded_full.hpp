#pragma once

// need to be synced with redis/src/server.h
#define LRU_BITS 24

struct robj {
  unsigned type : 4;
  unsigned encoding : 4;
  unsigned lru : LRU_BITS; /* LRU time (relative to global lru_clock) or
                            * LFU data (least significant 8 bits frequency
                            * and most significant 16 bits access time). */
  int refcount;
  void *ptr;
};

// need to be synced with redis/src/litesys.h
enum class EmbeddedRequestType {
  kSet,
  kHset,
  kGet,
  kHgetall,
  kPing,
  kMulti,
  kExec,
  kDiscard,
  kQuit,
  kUnknown
};

typedef struct {
  EmbeddedRequestType type;
  int argc;
  robj **argv;
  int *argv_len;
} EmbeddedRequest;
