#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

#include "ipc.h"

int connect_ipc_server(const char *custom_path);

int send_ipc_request(int fd, uint8_t type);

int ipc_client_set_link(int fd, const char *if_name, uint8_t admin_up);

int ipc_client_set_ip(int fd, const char *if_name, const char *ip_addr, uint8_t prefix_len);

int ipc_client_del_ip(int fd, const char *if_name, const char *ip_addr, uint8_t prefix_len);


#endif // IPC_CLIENT_H