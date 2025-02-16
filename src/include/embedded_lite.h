#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int LiteInit(char *argv_0);

int LiteSignalHandler(void);

typedef void (*ReinstallEventHandlerFn)(void *client);
void *LiteRegisterFD(int fd, ReinstallEventHandlerFn ReinstallEventHandler,
                     void *client);
void LiteUnregisterFD(int fd);

typedef void (*RequestDestructorFn)(void *request);
int LiteProcessRequest(void *conn_info, void *request,
                       RequestDestructorFn RequestDestructor);

#ifdef __cplusplus
}
#endif