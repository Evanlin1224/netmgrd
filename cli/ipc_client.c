#include "ipc_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

static int recv_ipc_ack(int fd, struct ipc_ack *ack) {
    struct ipc_header ack_hdr;
    memset(&ack_hdr, 0, sizeof(ack_hdr));

    ssize_t n3 = recv(fd, &ack_hdr, sizeof(ack_hdr), MSG_WAITALL);
    if (n3 < (ssize_t)sizeof(ack_hdr)) {
        perror("CLI: Failed to recv ack header");
        return -1;
    }
    if (ack_hdr.type != MSG_RESP_ACK || ntohl(ack_hdr.length) != sizeof(struct ipc_ack)) {
        perror("CLI: Invalid ACK header received");
        return -1;
    }

    ssize_t n4 = recv(fd, ack, sizeof(*ack), MSG_WAITALL);
    if (n4 < (ssize_t)sizeof(*ack)) {
        perror("CLI: Failed to recv ack");
        return -1;
    }

    return ntohl(ack->status_code);
}

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

int ipc_client_set_link(int fd, const char *if_name, uint8_t admin_up) {
    struct ipc_header hdr;
    hdr.type = MSG_REQ_SET_LINK;
    hdr.length = htonl(sizeof(struct ipc_if_set_link));

    struct ipc_if_set_link payload;
    memset(&payload, 0, sizeof(payload));
    
    if (!if_name) return -1;
    snprintf(payload.if_name, sizeof(payload.if_name), "%s", if_name);
    payload.admin_up = admin_up;

    // 傳送 Header and payload
    ssize_t n1 = send(fd, &hdr, sizeof(hdr), 0);
    ssize_t n2 = send(fd, &payload, sizeof(payload), 0);

    if (n1 < 0 || n2 < 0) {
        perror("CLI: Failed to send link setting request");
        return -1;
    }

    // recv ack from netmgrd daemon
    struct ipc_ack ack;
    memset(&ack, 0, sizeof(ack));

    return recv_ipc_ack(fd, &ack);
}

int ipc_client_set_ip(int fd, const char *if_name, const char *ip_addr, uint8_t prefix_len) {
    struct ipc_header hdr;
    hdr.type = MSG_REQ_SET_IP;
    hdr.length = htonl(sizeof(struct ipc_if_set_ip));

    struct ipc_if_set_ip payload;
    memset(&payload, 0, sizeof(payload));

    if(!if_name || !ip_addr || prefix_len > 128) return -1;
    snprintf(payload.if_name, sizeof(payload.if_name), "%s", if_name);
    snprintf(payload.ip_addr, sizeof(payload.ip_addr), "%s", ip_addr);
    payload.prefix_len = prefix_len;

    // send hdr + ipc_if_set_ip
    ssize_t n1 = send(fd, &hdr, sizeof(hdr), 0);
    ssize_t n2 = send(fd, &payload, sizeof(payload), 0);

    if (n1 < 0 || n2 < 0) {
        perror("CLI: Failed to send IP setting request");
        return -1;
    }

    // recv ack from netmgrd daemon
    struct ipc_ack ack;
    memset(&ack, 0, sizeof(ack));
    
    return recv_ipc_ack(fd, &ack);
}

int ipc_client_del_ip(int fd, const char *if_name, const char *ip_addr, uint8_t prefix_len) {
    // 封裝header, payload
    struct ipc_header hdr;
    hdr.type = MSG_REQ_DEL_IP;
    hdr.length = htonl(sizeof(struct ipc_if_set_ip));

    struct ipc_if_set_ip payload;
    memset(&payload, 0, sizeof(payload));
    if (!if_name || !ip_addr || prefix_len > 128) return -1;
    snprintf(payload.if_name, sizeof(payload.if_name), "%s", if_name);
    snprintf(payload.ip_addr, sizeof(payload.ip_addr), "%s", ip_addr);
    payload.prefix_len = prefix_len;

    // send hdr + ipc_if_set_ip
    ssize_t n1 = send(fd, &hdr, sizeof(hdr), 0);
    ssize_t n2 = send(fd, &payload, sizeof(payload), 0);

    if (n1 < 0 || n2 < 0) {
        perror("CLI: Failed to send IP setting request (delete instruction)");
        return -1;
    }

    // recv ack header from netmgrd daemon
    struct ipc_ack ack;
    memset(&ack, 0, sizeof(ack));
    
    return recv_ipc_ack(fd, &ack);
}