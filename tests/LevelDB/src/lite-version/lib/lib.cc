#include <lite.hpp>

#include "packet.hpp"
#include "service.hpp"

extern "C" {
__attribute__((visibility("default"))) int LiteIsNormalMode() {
  return lite::IsNormalMode<LevelDB, Packet, Packet, ConnectionInfo,
                            std::string, CacheEntry>();
}

__attribute__((visibility("default"))) int LiteInit(
    char *argv_0, FlushWriteBufferFn FlushWriteBuffer,
    ReinstallClientEventHandlerFn ReinstallClientEventHandler,
    ReinstallListenerEventHandlerFn ReinstallListenerEventHandler) {
  auto ret = lite::Init<LevelDB, Packet, Packet, ConnectionInfo, std::string,
                        CacheEntry>(
      argv_0, "/tmp/lite_leveldb_control_plane.sock", FlushWriteBuffer,
      ReinstallClientEventHandler, ReinstallListenerEventHandler);
  return ret;
}

__attribute__((visibility("default"))) int LiteFullStartListening() {
  return lite::FullStartListening<LevelDB, Packet, Packet, ConnectionInfo,
                                  std::string, CacheEntry>();
}

__attribute__((visibility("default"))) int LiteSignalHandler(int sig) {
  return lite::SignalHandler<LevelDB, Packet, Packet, ConnectionInfo,
                             std::string, CacheEntry>(sig);
}

__attribute__((visibility("default"))) void LiteRegisterListener(
    int fd, void *listener, int is_replay) {
  lite::RegisterListener<LevelDB, Packet, Packet, ConnectionInfo, std::string,
                         CacheEntry>(fd, listener, is_replay);
}

__attribute__((visibility("default"))) void LiteUnregisterListener(int fd) {
  lite::UnregisterListener<LevelDB, Packet, Packet, ConnectionInfo, std::string,
                           CacheEntry>(fd);
}

__attribute__((visibility("default"))) int LiteGetDummyListenerFD() {
  return lite::GetDummyListenerFD<LevelDB, Packet, Packet, ConnectionInfo,
                                  std::string, CacheEntry>();
}

__attribute__((visibility("default"))) void LiteRegisterClient(int fd,
                                                               void *client) {
  lite::RegisterClient<LevelDB, Packet, Packet, ConnectionInfo, std::string,
                       CacheEntry>(fd, client);
}

__attribute__((visibility("default"))) void LiteUnregisterClient(int fd) {
  lite::UnregisterClient<LevelDB, Packet, Packet, ConnectionInfo, std::string,
                         CacheEntry>(fd);
}

}  // extern "C"