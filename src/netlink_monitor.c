#include "netlink_monitor.h"
#include "state.h"
#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>


int nl_monitor_init() {
    // Initialize netlink socket and set up monitoring
    syslog(LOG_INFO, "Initializing netlink monitor...");

    // create a netlink socket
    int nl_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (nl_fd == -1) {
        syslog(LOG_ERR, "Failed to create netlink socket");
        return -1;
    }

    // 設定NOBLOCK標誌，避免阻塞
    int flags = fcntl(nl_fd, F_GETFL, 0);
    if (flags == -1) {
        syslog(LOG_ERR, "Failed to get socket flags");
        close(nl_fd);
        return -1;
    }
    if (fcntl(nl_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "Failed to set socket to non-blocking mode");
        close(nl_fd);
        return -1;
    }

    // bind to subscribe to link and ip address changes
    struct sockaddr_nl sa; // sa: socket address
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR; // subscribe to link and IP address changes

    if (bind(nl_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        syslog(LOG_ERR, "Failed to bind netlink socket");
        close(nl_fd);
        return -1;
    }

    return nl_fd;
}

void nl_monitor_parse(int nl_fd) {
    syslog(LOG_INFO, "Parsing netlink messages...");

    // read messages from the netlink socket
    char buf[8192];
    // iovec structure is used to describe a buffer for read/write operations
    struct iovec iov = { buf, sizeof(buf) };  
    struct sockaddr_nl sa;
    // msghdr structure is used to describe a message for sendmsg/recvmsg system calls
    struct msghdr msg = {&sa, sizeof(sa), &iov, 1, NULL, 0, 0};
    ssize_t len;

    while (1) {
        len = recvmsg(nl_fd, &msg, 0);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // All messages have been read, exit the loop
                break;
            }
            if (errno == EINTR) {
                // Interrupted by a signal, continue reading
                continue;
            }
            syslog(LOG_ERR, "Netlink recvmsg error: %m");
            break;
        }
        if (len == 0) {
            break;
        }

        // parse the Netlink interface information (L2)
        for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
            if (nh->nlmsg_type == NLMSG_DONE) {
                break;
            }
            if (nh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nh);
                syslog(LOG_ERR, "Netlink error: %d", err->error);
                continue;
            }

            if (nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_DELLINK) {
                struct ifinfomsg *ifinfo = (struct ifinfomsg *)NLMSG_DATA(nh);
                char ifname[IFNAMSIZ] = {0};
                if_indextoname(ifinfo->ifi_index, ifname);

                int admin_up = (ifinfo->ifi_flags & IFF_UP) != 0;
                int carrier_up = (ifinfo->ifi_flags & IFF_LOWER_UP) != 0;

                syslog(LOG_INFO, "Interface %s: admin %s, carrier %s", ifname, admin_up ? "up" : "down", carrier_up ? "up" : "down");

                // update state table (link):
                update_state_table_link(ifinfo->ifi_index, ifname, admin_up, carrier_up, nh->nlmsg_type == RTM_DELLINK);

                // handle broadcast link event
                struct ipc_if_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.event_type = nh->nlmsg_type;
                ev.if_index = ifinfo->ifi_index;
                strncpy(ev.if_name, ifname, sizeof(ev.if_name) - 1);
                ev.admin_up = admin_up;
                ev.carrier_up = carrier_up;

                // 嘗試從快取中撈取現有的 IP 補上，讓資料更完整
                interface_state_t st;
                if (get_state_by_index(ifinfo->ifi_index, &st) == 0) {
                    strncpy(ev.ip_addr, st.ip_addr, sizeof(ev.ip_addr) - 1);
                }
                ipc_server_broadcast_event(&ev);
                
            }
            // parse the Netlink IP address information (L3)
            else if (nh->nlmsg_type == RTM_NEWADDR || nh->nlmsg_type == RTM_DELADDR) {
                struct ifaddrmsg *ifaddr = (struct ifaddrmsg *)NLMSG_DATA(nh);
                char ifname[IFNAMSIZ] = {0};
                if_indextoname(ifaddr->ifa_index, ifname);

                struct rtattr *rta = (struct rtattr *)IFA_RTA(ifaddr);
                int rta_len = IFA_PAYLOAD(nh);
                char ip_str[INET6_ADDRSTRLEN] = {0};

                for (;RTA_OK(rta, rta_len); rta = RTA_NEXT(rta, rta_len)) {
                    if (rta->rta_type == IFA_LOCAL || rta->rta_type == IFA_ADDRESS) {
                        void *addr = RTA_DATA(rta);
                        if (ifaddr->ifa_family == AF_INET) {
                            inet_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
                        } else if (ifaddr->ifa_family == AF_INET6) {
                            inet_ntop(AF_INET6, addr, ip_str, sizeof(ip_str));
                        }
                    }
                }

                syslog(LOG_INFO, "IP Event: Interface %s:  IP %s : %s", ifname, (nh->nlmsg_type == RTM_NEWADDR) ? "added" : "deleted", ip_str);

                // update state table (ip address):
                update_state_table_ip(ifaddr->ifa_index, ifname, ip_str, nh->nlmsg_type == RTM_DELADDR);

                // handle broadcast ip event
                struct ipc_if_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.event_type = nh->nlmsg_type;
                ev.if_index = ifaddr->ifa_index;
                strncpy(ev.if_name, ifname, sizeof(ev.if_name) - 1);
                strncpy(ev.ip_addr, ip_str, sizeof(ev.ip_addr) - 1);

                // 嘗試從快取中撈取 Link 狀態補上
                interface_state_t st;
                if (get_state_by_index(ifaddr->ifa_index, &st) == 0) {
                    ev.admin_up = st.admin_up;
                    ev.carrier_up = st.carrier_up;
                }
                ipc_server_broadcast_event(&ev);
            }
        }
    }
}
