#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FlushWriteBufferFn)(void *conn);
typedef void (*RequestDestructorFn)(void *request);
typedef void (*ReinstallEventHandlerFn)(void *client);

int LiteInit(char *argv_0, RequestDestructorFn RequestDestructor,
             FlushWriteBufferFn FlushWriteBuffer,
             ReinstallEventHandlerFn ReinstallEventHandler);

int LiteSignalHandler(int sig);

void LiteRegisterListenerFD(int fd);
void LiteUnregisterListenerFD(int fd);

void *LiteRegisterClientFD(int fd, void *client);
void LiteUnregisterClientFD(int fd);

int LiteProcessRequest(void *conn_info, void *request);

#ifdef __cplusplus
}
#endif