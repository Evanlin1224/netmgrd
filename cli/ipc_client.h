#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

#include "ipc.h"

int connect_ipc_server(const char *custom_path);

int send_ipc_request(int fd, uint8_t type);

#endif // IPC_CLIENT_H