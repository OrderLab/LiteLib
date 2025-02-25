#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FlushWriteBufferFn)(void *conn);
typedef void (*RequestDestructorFn)(void *request);
// uninstall is 0 if the event handler is being uninstalled, 1 if it is being
// installed
typedef void (*ReinstallClientEventHandlerFn)(void *client, int install);
typedef void (*ReinstallListenerEventHandlerFn)(void *listener, int install);

int LiteIsNormalMode(void);

int LiteInit(char *argv_0, RequestDestructorFn RequestDestructor,
             FlushWriteBufferFn FlushWriteBuffer,
             ReinstallClientEventHandlerFn ReinstallClientEventHandler,
             ReinstallListenerEventHandlerFn ReinstallListenerEventHandler);

int LiteFullStartListening(void);

int LiteSignalHandler(int sig);

void LiteRegisterListenerFD(int fd, void *listener, int is_replay);
void LiteUnregisterListenerFD(int fd);
int LiteGetDummyListenerFD(void);

void *LiteRegisterClientFD(int fd, void *client);
void LiteUnregisterClientFD(int fd);

int LiteProcessRequest(void *conn_info, void *request);

#ifdef __cplusplus
}
#endif