#include "netlink_monitor.h"
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

            }
        }
    }
}
