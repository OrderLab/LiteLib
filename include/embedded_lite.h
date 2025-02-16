#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int LiteInit(char *argv_0, int number_of_workers, long long shared_memory_size,
             long long max_item_count, long long sliding_window_size_in_ms);

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