#include <lite.hpp>

#include "packet.hpp"
#include "service.hpp"

extern "C" {
__attribute__((visibility("default"))) int LiteInit(
    char *argv_0, int number_of_workers, long long shared_memory_size,
    long long max_item_count, long long sliding_window_size_in_ms) {
  auto ret =
      lite::Init<Redis, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>(
          argv_0, number_of_workers, shared_memory_size, max_item_count,
          sliding_window_size_in_ms);
  shm = &static_cast<lite::EmbeddedServer<Redis, Packet, Packet, ConnectionInfo,
                                          CacheKey, CacheEntry> *>(
             lite::embedded_server_void_ptr)
             ->shared_memory_;
  return ret;
}

__attribute__((visibility("default"))) int LiteSignalHandler() {
  return lite::SignalHandler<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                             CacheEntry>();
}

__attribute__((visibility("default"))) void *LiteRegisterFD(
    int fd, ReinstallEventHandlerFn ReinstallEventHandler, void *client) {
  return lite::RegisterFD<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                          CacheEntry>(fd, ReinstallEventHandler, client);
}

__attribute__((visibility("default"))) void LiteUnregisterFD(int fd) {
  lite::UnregisterFD<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                     CacheEntry>(fd);
}

__attribute__((visibility("default"))) int LiteProcessRequest(
    void *conn_info, void *request, RequestDestructorFn RequestDestructor) {
  return lite::ProcessRequest<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                              CacheEntry>(conn_info, request, RequestDestructor,
                                          &Redis::EmbeddedNormalUpdate);
}
}  // extern "C"