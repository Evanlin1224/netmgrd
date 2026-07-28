#include "ipc_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

int connect_ipc_server(const char *custom_path) {
    const char *path = (custom_path && strlen(custom_path) > 0) ? custom_path : DEFAULT_SOCKET_PATH;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("CLI: Failed to create socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("CLI: Failed to connect to server");
        close(fd);
        return -1;
    }

    return fd;
}

int send_ipc_request(int fd, uint8_t type) {
    struct ipc_header hdr;
    hdr.type = type;
    hdr.length = htonl(0); // 請求封包長度(payload)為0

    ssize_t n = send(fd, &hdr, sizeof(hdr), 0);
    if (n < 0) {
        perror("CLI: Failed to send request");
        return -1;
    }
    return 0;
}