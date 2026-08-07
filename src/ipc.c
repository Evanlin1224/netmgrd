#include "ipc.h"
#include "state.h"
#include "netlink_control.h"

#include <stdio.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>


#define MAX_CLIENTS 16

static int g_monitor_clients[MAX_CLIENTS];
static char g_socket_path[256] = {0};

// helper function: set socket to non-blocking mode
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int ipc_server_init(const char *custom_path) {
    if (custom_path && strlen(custom_path) > 0) {
        strncpy(g_socket_path, custom_path, sizeof(g_socket_path) - 1);
    } else {
        strncpy(g_socket_path, DEFAULT_SOCKET_PATH, sizeof(g_socket_path) - 1);
    }

    // 初始化訂閱列表
    for (int i = 0; i < MAX_CLIENTS; i++) {
            g_monitor_clients[i] = -1;
    }

    // 刪除先前殘留的socket檔案
    unlink(g_socket_path);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        syslog(LOG_ERR, "IPC: Failed to create UDS socket: %m");
        return -1;
    }

    if (set_nonblocking(listen_fd) < 0) {
        syslog(LOG_ERR, "IPC: Failed to set non-blocking mode on listen fd: %m");
        close(listen_fd);
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_socket_path, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ) {
        syslog(LOG_ERR, "IPC: Failed to bind socket to %s: %m", g_socket_path);
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 10) < 0) {
        syslog(LOG_ERR, "IPC: Failed to listen on socket: %m");
        close(listen_fd);
        return -1;
    }

    syslog(LOG_INFO, "IPC: Server initialized on %s", g_socket_path);
    return listen_fd;
}

int ipc_server_accept(int listen_fd, int epoll_fd) {
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        syslog(LOG_ERR, "IPC: Accept failed %m");
        return -1;
    }

    if (set_nonblocking(client_fd) < 0) {
        syslog(LOG_ERR, "IPC: Failed to set non-blocking mode on client fd: %m");
        close(client_fd);
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
        syslog(LOG_ERR, "IPC: Failed to add client fd to epoll: %m");
        close(client_fd);
        return -1;
    }

    syslog(LOG_INFO, "IPC: Accepted new connection, fd %d", client_fd);
    return client_fd;
}

void ipc_server_handle_client(int client_fd, int epoll_fd) {
    struct ipc_header hdr;

    // read TLV Header (5 bytes)
    ssize_t n = recv(client_fd, &hdr, sizeof(hdr), 0);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return; // No data
        }

        // clients 斷線或是錯誤，需要進行清理
        syslog(LOG_INFO, "IPC: Client disconnected, fd %d", client_fd);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        close(client_fd);

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_monitor_clients[i] == client_fd) {
                g_monitor_clients[i] = -1;
                break;
            }
        }
    }

    // 處理請求類型
    if (hdr.type == MSG_REQ_SHOW) {
        syslog(LOG_INFO, "IPC received MSG_REQ_SHOW from client fd %d", client_fd);

        interface_state_t table[MAX_INTERFACES];
        int count = get_state_table(table, MAX_INTERFACES);

        // 計算 payload 大小：count(4 bytes) + count * ipc_if_info(68 bytes)
        uint32_t payload_len = sizeof(uint32_t) + count * sizeof(struct ipc_if_info);

        struct ipc_header resp_hdr;
        resp_hdr.type = MSG_RESP_SHOW;
        resp_hdr.length = htonl(payload_len);

        // 傳送 Header
        send(client_fd, &resp_hdr, sizeof(resp_hdr), 0);

        // 傳送 count (大端序)
        uint32_t net_count = htonl((uint32_t)count);
        send(client_fd, &net_count, sizeof(net_count), 0);

        // 打包結構並傳送
        for (int i = 0; i < count; i++) {
            struct ipc_if_info info;
            info.if_index = htonl((uint32_t)table[i].index);
            strncpy(info.if_name, table[i].name, sizeof(info.if_name));
            info.admin_up = (uint8_t)table[i].admin_up;
            info.carrier_up = (uint8_t)table[i].carrier_up;
            strncpy(info.ip_addr, table[i].ip_addr, sizeof(info.ip_addr));

            send(client_fd, &info, sizeof(info), 0);
        }

        // Show 請求為 One-shot，處理完即主動中斷連線
        syslog(LOG_INFO, "IPC: Responded to MSG_REQ_SHOW and closing fd %d", client_fd);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        close(client_fd);
    } else if (hdr.type == MSG_REQ_MONITOR) {
        syslog(LOG_INFO, "IPC: Received MSG_REQ_MONITOR from client fd %d", client_fd);

        int added = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_monitor_clients[i] == -1) {
                g_monitor_clients[i] = client_fd;
                added = 1;
                break;
            }
        }

        if (!added) {
            syslog(LOG_WARNING, "IPC: Monitor client list full, dropping client fd %d", client_fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
            close(client_fd);
        }
    } else if (hdr.type == MSG_REQ_SET_LINK) {
        syslog(LOG_INFO, "IPC: Received MSG_REQ_SET_LINK from client fd %d", client_fd);

        // parse payload (struct ipc_if_set_link)
        struct ipc_if_set_link payload;
        memset(&payload, 0, sizeof(payload));
        ssize_t payload_size = recv(client_fd, &payload, sizeof(payload), MSG_WAITALL);
        if (payload_size < (ssize_t)sizeof(payload)) {
            syslog(LOG_ERR, "IPC: Failed to recv SET_LINK payload from fd %d", client_fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
            close(client_fd);
            return;
        }

        // 執行 nl_set_link_status
        int ret = nl_set_link_status(payload.if_name, payload.admin_up);

        // 回傳 MSG_RESP_ACK
        struct ipc_ack ack;
        memset(&ack, 0, sizeof(ack));
        ack.status_code = htonl(ret);
        if (ret == 0) {
            snprintf(ack.message, sizeof(ack.message), "Interface %s set to %s", payload.if_name, payload.admin_up ? "UP" : "DOWN");
        } else {
            snprintf(ack.message, sizeof(ack.message), "Failed to set %s: %s (%d)", payload.if_name, strerror(-ret), ret);
        }
         struct ipc_header resp_hdr;
         resp_hdr.type = MSG_RESP_ACK;
         resp_hdr.length = htonl(sizeof(struct ipc_ack));
         send(client_fd, &resp_hdr, sizeof(resp_hdr), 0);
         send(client_fd, &ack, sizeof(ack), 0);

         // one-shot request, close connection after response
         epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
         close(client_fd);

    } else if (hdr.type == MSG_REQ_SET_IP || hdr.type == MSG_REQ_DEL_IP) {
        syslog(LOG_INFO, "IPC: Received %s from client fd %d", hdr.type == MSG_REQ_SET_IP ? "MSG_REQ_SET_IP" : "MSG_REQ_DEL_IP", client_fd);

        // parse payload (struct ipc_if_set_ip)
        struct ipc_if_set_ip payload;
        memset(&payload, 0, sizeof(payload));
        ssize_t payload_size = recv(client_fd, &payload, sizeof(payload), MSG_WAITALL);
        if (payload_size < (ssize_t)sizeof(payload)) {
            syslog(LOG_ERR, "IPC: Failed to recv SET_IP/DEL IP payload from fd %d", client_fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
            close(client_fd);
            return;
        }

        // 執行 nl_modify_ip_addr
        int ret = nl_modify_ip_addr(payload.if_name, payload.ip_addr, payload.prefix_len, hdr.type == MSG_REQ_SET_IP ? NL_IP_ADDR_ADD : NL_IP_ADDR_DEL);

        // 回傳 MSG_RESP_ACK
        struct ipc_ack ack;
        memset(&ack, 0, sizeof(ack));
        ack.status_code = htonl(ret);
        if (ret == 0) {
            snprintf(ack.message, sizeof(ack.message), "Interface %s IP %s/%d %s", payload.if_name, payload.ip_addr, payload.prefix_len, hdr.type == MSG_REQ_SET_IP ? "added" : "deleted");
        } else {
            snprintf(ack.message, sizeof(ack.message), "Failed to %s IP %s/%d on interface %s: %s (%d)", hdr.type == MSG_REQ_SET_IP ? "add" : "delete", payload.ip_addr, payload.prefix_len, payload.if_name, strerror(-ret), ret);
        }
        struct ipc_header resp_hdr;
        resp_hdr.type = MSG_RESP_ACK;
        resp_hdr.length = htonl(sizeof(struct ipc_ack));
        send(client_fd, &resp_hdr, sizeof(resp_hdr), 0);
        send(client_fd, &ack, sizeof(ack), 0);

        // one-shot request, close connection after response
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        close(client_fd);
    } else {
        syslog(LOG_WARNING, "IPC: Received unknown type 0x%02X from fd %d", hdr.type, client_fd);
    }
}

void ipc_server_broadcast_event(struct ipc_if_event *event) {
    struct ipc_header hdr;
    hdr.type = MSG_EVENT_MONITOR;
    hdr.length = htonl(sizeof(struct ipc_if_event));

    // 大端序轉換 (Big-Endian)
    struct ipc_if_event net_event;
    net_event.event_type = htonl(event->event_type);
    net_event.if_index = htonl(event->if_index);
    strncpy(net_event.if_name, event->if_name, sizeof(net_event.if_name));
    net_event.admin_up = event->admin_up;
    net_event.carrier_up = event->carrier_up;
    strncpy(net_event.ip_addr, event->ip_addr, sizeof(net_event.ip_addr));

    for (int i = 0; i < MAX_CLIENTS; i++) {
        int client_fd = g_monitor_clients[i];
        if (client_fd != -1) {
            // try to send header 
            ssize_t s1 = send(client_fd, &hdr, sizeof(hdr), MSG_NOSIGNAL);
            // try to send payload
            ssize_t s2 = send(client_fd, &net_event, sizeof(net_event), MSG_NOSIGNAL);

            if (s1 < 0 || s2 < 0) {
                syslog(LOG_WARNING, "IPC: Failed to send event to client fd %d (probably disconnected): %m", client_fd);
                close(client_fd);
                g_monitor_clients[i] = -1;
            }
        }
    }
}

void ipc_server_cleanup(void) {
    if (strlen(g_socket_path) > 0) {
        unlink(g_socket_path);
        syslog(LOG_INFO, "IPC: Removing socket file %s", g_socket_path);
    }
}