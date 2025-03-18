#include <lite.hpp>

#include "packet.hpp"
#include "service.hpp"

extern "C" {
__attribute__((visibility("default"))) int LiteIsNormalMode() {
  return lite::IsNormalMode<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                            CacheKey, CacheEntry>();
}

__attribute__((visibility("default"))) int LiteInit(
    char *argv_0, RequestDestructorFn RequestDestructor,
    FlushWriteBufferFn FlushWriteBuffer,
    ReinstallClientEventHandlerFn ReinstallClientEventHandler,
    ReinstallListenerEventHandlerFn ReinstallListenerEventHandler) {
  auto ret = lite::Init<Redis, RESPPacket, RESPPacket, ConnectionInfo, CacheKey,
                        CacheEntry>(
      argv_0, 1, 4ll * 1024 * 1024 * 1024, 16384 * 2 * 2, 1000ms,
      "/tmp/lite_Redis", RequestDestructor, FlushWriteBuffer,
      ReinstallClientEventHandler, ReinstallListenerEventHandler,
      &Redis::EmbeddedNormalUpdate);
  shm = &static_cast<lite::EmbeddedServer<
      Redis, RESPPacket, RESPPacket, ConnectionInfo, CacheKey, CacheEntry> *>(
             lite::embedded_server_void_ptr)
             ->shared_memory_;
  return ret;
}

__attribute__((visibility("default"))) int LiteFullStartListening() {
  return lite::FullStartListening<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                                  CacheKey, CacheEntry>();
}

__attribute__((visibility("default"))) int LiteSignalHandler(int sig) {
  return lite::SignalHandler<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                             CacheKey, CacheEntry>(sig);
}

__attribute__((visibility("default"))) void LiteRegisterListener(
    int fd, void *listener, int is_replay) {
  lite::RegisterListener<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                         CacheKey, CacheEntry>(fd, listener, is_replay);
}

__attribute__((visibility("default"))) void LiteUnregisterListener(int fd) {
  lite::UnregisterListener<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                           CacheKey, CacheEntry>(fd);
}

__attribute__((visibility("default"))) int LiteGetDummyListenerFD() {
  return lite::GetDummyListenerFD<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                                  CacheKey, CacheEntry>();
}

__attribute__((visibility("default"))) void *LiteRegisterClient(int fd,
                                                                void *client) {
  return lite::RegisterClient<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                              CacheKey, CacheEntry>(fd, client);
}

__attribute__((visibility("default"))) void LiteUnregisterClient(int fd) {
  lite::UnregisterClient<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                         CacheKey, CacheEntry>(fd);
}

__attribute__((visibility("default"))) int LiteProcessRequest(void *conn_info,
                                                              void *request,
                                                              int is_success) {
  return lite::ProcessRequest<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                              CacheKey, CacheEntry>(conn_info, request,
                                                    is_success);
}
}  // extern "C"