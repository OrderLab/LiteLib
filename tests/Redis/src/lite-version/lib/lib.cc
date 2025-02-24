#include <lite.hpp>

#include "packet.hpp"
#include "service.hpp"

extern "C" {
__attribute__((visibility("default"))) int LiteIsNormalMode() {
  return lite::IsNormalMode<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                            CacheEntry>();
}

__attribute__((visibility("default"))) int LiteInit(
    char *argv_0, RequestDestructorFn RequestDestructor,
    FlushWriteBufferFn FlushWriteBuffer,
    ReinstallClientEventHandlerFn ReinstallClientEventHandler,
    ReinstallListenerEventHandlerFn ReinstallListenerEventHandler) {
  auto ret =
      lite::Init<Redis, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>(
          argv_0, 1, std::numeric_limits<int>::max(), 16384 * 2, 1000ms,
          "/tmp/lite_Redis", RequestDestructor, FlushWriteBuffer,
          ReinstallClientEventHandler, ReinstallListenerEventHandler);
  shm = &static_cast<lite::EmbeddedServer<Redis, Packet, Packet, ConnectionInfo,
                                          CacheKey, CacheEntry> *>(
             lite::embedded_server_void_ptr)
             ->shared_memory_;
  return ret;
}

__attribute__((visibility("default"))) int LiteFullStartListening() {
  return lite::FullStartListening<Redis, Packet, Packet, ConnectionInfo,
                                  CacheKey, CacheEntry>();
}

__attribute__((visibility("default"))) int LiteSignalHandler(int sig) {
  return lite::SignalHandler<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                             CacheEntry>(sig);
}

__attribute__((visibility("default"))) void LiteRegisterListenerFD(
    int fd, void *listener) {
  lite::RegisterListenerFD<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                           CacheEntry>(fd, listener);
}

__attribute__((visibility("default"))) void LiteUnregisterListenerFD(int fd) {
  lite::UnregisterListenerFD<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                             CacheEntry>(fd);
}

__attribute__((visibility("default"))) int LiteGetDummyListenerFD() {
  return lite::GetDummyListenerFD<Redis, Packet, Packet, ConnectionInfo,
                                  CacheKey, CacheEntry>();
}

__attribute__((visibility("default"))) void *LiteRegisterClientFD(
    int fd, void *client) {
  return lite::RegisterClientFD<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                                CacheEntry>(fd, client);
}

__attribute__((visibility("default"))) void LiteUnregisterClientFD(int fd) {
  lite::UnregisterClientFD<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                           CacheEntry>(fd);
}

__attribute__((visibility("default"))) int LiteProcessRequest(void *conn_info,
                                                              void *request) {
  return lite::ProcessRequest<Redis, Packet, Packet, ConnectionInfo, CacheKey,
                              CacheEntry>(conn_info, request,
                                          &Redis::EmbeddedNormalUpdate);
}
}  // extern "C"