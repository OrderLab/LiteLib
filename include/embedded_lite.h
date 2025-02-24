#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FlushWriteBufferFn)(void *conn);
typedef void (*RequestDestructorFn)(void *request);
typedef void (*ReinstallClientEventHandlerFn)(void *client);
typedef void (*ReinstallListenerEventHandlerFn)(void *listener);

int LiteIsNormalMode(void);

int LiteInit(char *argv_0, RequestDestructorFn RequestDestructor,
             FlushWriteBufferFn FlushWriteBuffer,
             ReinstallClientEventHandlerFn ReinstallClientEventHandler,
             ReinstallListenerEventHandlerFn ReinstallListenerEventHandler);

int LiteFullStartListening(void);

int LiteSignalHandler(int sig);

void LiteRegisterListenerFD(int fd, void *listener);
void LiteUnregisterListenerFD(int fd);
int LiteGetDummyListenerFD(void);

void *LiteRegisterClientFD(int fd, void *client);
void LiteUnregisterClientFD(int fd);

int LiteProcessRequest(void *conn_info, void *request);

#ifdef __cplusplus
}
#endif