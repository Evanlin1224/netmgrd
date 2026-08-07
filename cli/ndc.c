#include "ipc_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/rtnetlink.h> // 取得 RTM_NEWLINK 等常數

static void print_usage(void) {
    printf("Usage:\n");
    printf("  ndc show\n");
    printf("  ndc monitor\n");
    printf("  ndc set link <ifname> <up | down>\n");
    printf("  ndc set ip <ifname> <ip/prefix>\n");
    printf("  ndc del ip <ifname> <ip/prefix>\n");
}

static void handle_show(int fd) {
    if (send_ipc_request(fd, MSG_REQ_SHOW) < 0) return;

    struct ipc_header hdr;
    if (recv(fd, &hdr, sizeof(hdr), MSG_WAITALL) <= 0) {
        fprintf(stderr, "Error receiving response header\n");
        return;
    }

    if (hdr.type != MSG_RESP_SHOW) {
        fprintf(stderr, "Unexpected response type: 0x%02X\n", hdr.type);
        return;
    }

    uint32_t payload_len = ntohl(hdr.length);
    if (payload_len < sizeof(uint32_t)) {
        fprintf(stderr, "Invalid payload length: %u\n", payload_len);
        return;
    }

    uint32_t count = 0;
    if (recv(fd, &count, sizeof(count), MSG_WAITALL) <= 0) {
        fprintf(stderr, "Error receiving count\n");
        return;
    }
    count = ntohl(count);

    // 印出表格表頭
    printf("+-------+------------------+----------+------------+---------------------------------------------+\n");
    printf("| %-5s | %-16s | %-8s | %-10s | %-43s |\n", "Index", "Interface", "Admin", "Carrier", "IP Address");
    printf("+-------+------------------+----------+------------+---------------------------------------------+\n");

    for (uint32_t i = 0; i < count; i++) {
        struct ipc_if_info info;
        if (recv(fd, &info, sizeof(info), MSG_WAITALL) <= 0) {
            fprintf(stderr, "Error receving interface info\n");
            break;
        }

        uint32_t if_index = ntohl(info.if_index);
        char ip[INET6_ADDRSTRLEN];
        strncpy(ip, info.ip_addr, sizeof(ip));
        if (strlen(ip) == 0 ) {
            strcpy(ip, "N/A");
        }

        printf("| %-5u | %-16s | %-8s | %-10s | %-43s |\n",
                if_index,
                info.if_name,
                info.admin_up ? "UP" : "DOWN",
                info.carrier_up ? "UP" : "DOWN",
                ip);
    }
    printf("+-------+------------------+----------+------------+---------------------------------------------+\n");
}

static void handle_monitor(int fd) {
    if (send_ipc_request(fd, MSG_REQ_MONITOR) < 0) return;
    printf("Successfully subscribed to daemon events. Monitoring...\n");

    while (1) {
        struct ipc_header hdr;
        if (recv(fd, &hdr, sizeof(hdr), MSG_WAITALL) <= 0) {
            printf("\nDaemon disconnected.\n");
            break;
        }
        if (hdr.type != MSG_EVENT_MONITOR) return;

        struct ipc_if_event event;
        if (recv(fd, &event, sizeof(event), MSG_WAITALL) <= 0) {
            break;        
        }

        uint32_t if_index = ntohl(event.if_index);
        uint32_t event_type = ntohl(event.event_type);

        const char *event_str = "UNKNOWN";
        if (event_type == RTM_NEWLINK) event_str = "LINK_UP";
        else if (event_type == RTM_DELLINK) event_str = "LINK_DOWN";
        else if (event_type == RTM_NEWADDR) event_str = "IP_ADDED";
        else if (event_type == RTM_DELADDR) event_str = "IP DELETED";

        printf("[EVENT] Type: %-15s | Name: %-6s | Index: %-2u | Admin: %-4s | Carrier: %-4s | IP: %s\n",
                event_str,
                event.if_name,
                if_index,
                event.admin_up ? "UP" : "DOWN",
                event.carrier_up ? "UP" : "DOWN",
                strlen(event.ip_addr) > 0 ? event.ip_addr : "N/A");
    }
}

static void handle_set_link(int fd, const char *if_name, uint8_t admin_up) {
    if (ipc_client_set_link(fd, if_name, admin_up) != 0) return;
    printf("Success to set link status\n");
}

static void handle_ip(int fd, const char *if_name, const char *ip_addr, uint8_t prefix_len, int is_del) {
    if (is_del) {
        if (ipc_client_del_ip(fd, if_name, ip_addr, prefix_len) != 0) return;
    } else {
        if (ipc_client_set_ip(fd, if_name, ip_addr, prefix_len) != 0) return;
    }

    printf("Success to %s IP\n", is_del ? "delete" : "set");
}

static int parse_ip_and_prefix_len(const char *source, char *ip, uint8_t *prefix_len) {
    char tmp[64];
    snprintf(tmp, INET6_ADDRSTRLEN, "%s", source);

    char *slash = strchr(tmp, '/');
    if (slash) {
        *slash = '\0';
        *prefix_len = (uint8_t)atoi(slash + 1);
    } else {
        *prefix_len = 32; // default value
    }

    snprintf(ip, INET6_ADDRSTRLEN, "%s", tmp);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    int fd = connect_ipc_server(NULL);
    if (fd < 0) {
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "show") == 0) {
        handle_show(fd);
    } else if (strcmp(argv[1], "monitor") == 0) {
        handle_monitor(fd);
    } else if (strcmp(argv[1], "set") == 0) {
        // 檢查參數數量是否正確
        if (argc != 5) {
            printf("Please enter correct instruction:\n");
            print_usage();
            close(fd);
            return EXIT_FAILURE;
        }

        if (strcmp(argv[2], "link") == 0) {
            handle_set_link(fd, argv[3], strcmp(argv[4], "up") == 0 ? 1 : 0);
            close(fd);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[2], "ip") == 0) {
            char ip[INET6_ADDRSTRLEN];
            uint8_t prefix_len;
            if (parse_ip_and_prefix_len(argv[4], ip, &prefix_len) < 0) {
                printf("Invalid IP address: %s\n", argv[4]);
                close(fd);
                return EXIT_FAILURE;
            }
            handle_ip(fd, argv[3], ip, prefix_len, 0);
            close(fd);
            return EXIT_SUCCESS;
        } else {
            printf("Invalid instruction: %s\n", argv[2]);
            close(fd);
            return EXIT_FAILURE;
        }
    } else if(strcmp(argv[1], "del") == 0) {
        if (argc != 5) {
            printf("Please enter correct instruction:\n");
            print_usage();
            close(fd);
            return EXIT_FAILURE;
        }

        if (strcmp(argv[2], "ip") == 0) {
            char ip[INET6_ADDRSTRLEN];
            uint8_t prefix_len;
            if (parse_ip_and_prefix_len(argv[4], ip, &prefix_len) < 0) {
                printf("Invalid IP address: %s\n", argv[4]);
                close(fd);
                return EXIT_FAILURE;
            }
            handle_ip(fd, argv[3], ip, prefix_len, 1);
            close(fd);
            return EXIT_SUCCESS;
        } else {
            printf("Invalid instruction: %s\n", argv[2]);
            close(fd);
            return EXIT_FAILURE;
        }
    } else {
        print_usage();
        close(fd);
        return EXIT_FAILURE;
    }
    close(fd);
    return EXIT_SUCCESS;
}