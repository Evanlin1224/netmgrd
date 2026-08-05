#include "netlink_control.h"

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

/** 
 * nl_add_rta() - Helper function to add a RTA to a message.
 * @nh: Pointer to the netlink message header.
 * @rta_type: Type of the RTA to add.
 * @data: Pointer to the data to be added.
 * @data_len: Length of the data to be added.
 */
static int nl_add_rta(struct nlmsghdr *nh, int rta_type, const void *data, size_t data_len) {
    // calculate the total length of the rta (include the rta header)
    int attr_len = RTA_LENGTH(data_len);

    // get the pointer to the end of the current message
    struct rtattr *rta = (struct rtattr *)((char *)nh + NLMSG_ALIGN(nh->nlmsg_len));

    // fill in the rta header
    rta->rta_len = attr_len;
    rta->rta_type = rta_type;

    // copy the data after the rta header
    memcpy(RTA_DATA(rta), data, data_len);

    // update nlmsg_len to include the new rta
    nh->nlmsg_len = NLMSG_ALIGN(nh->nlmsg_len) + RTA_ALIGN(attr_len);

    return 0;
}

/**
 * nl_send_req_and_wait_ack() - Helper function to send a netlink request and wait for an acknowledgment.
 * @nl_fd: File descriptor of the netlink socket.
 * @msg: Pointer to the netlink message to send.
 * @msg_len: Length of the netlink message to send.
 */
static int nl_send_req_and_wait_ack(int nl_fd, struct nlmsghdr *nh) {
    ssize_t res_len = send(nl_fd, nh, nh->nlmsg_len, 0);
    if (res_len == -1) {
        syslog(LOG_ERR, "Failed to send netlink message: %m");
        return -errno;
    }

    // wait for ack
    char ack_buf[4096];
    ssize_t ack_len = recv(nl_fd, ack_buf, sizeof(ack_buf), 0);
    if (ack_len <= 0) {
        syslog(LOG_ERR, "Failed to receive netlink ack: %m");
        return -errno;
    }

    struct nlmsghdr *ack_nh = (struct nlmsghdr *)ack_buf;
    // check if the ack header is valid
    if (!(NLMSG_OK(ack_nh, (size_t)ack_len))) {
        syslog(LOG_ERR, "Invalid netlink ack response header");
        return -EINVAL;
    }

    if (ack_nh->nlmsg_type == NLMSG_ERROR) {
        if (ack_len < (ssize_t)NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
            syslog(LOG_ERR, "Truncated netlink error response");
            return -EINVAL;
        }
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(ack_nh);
        if (err->error != 0) {
            syslog(LOG_ERR, "Netlink error: %s", strerror(-err->error));
            return err->error;  // 回傳負數 errno (ex: -EPERM, -ENODEV, -EEXIST)
        }
        return 0;
    }

    syslog(LOG_ERR, "Unexpected netlink response type: %d", ack_nh->nlmsg_type);
    return -EINVAL;
}

int nl_set_link_status(const char *ifname, int admin_up) {
    // 先驗證if_name是否存在
    int if_idx = (int)if_nametoindex(ifname); // if_nametoindex回傳型別為unsigned int
    if (if_idx == 0) {
        syslog(LOG_ERR, "Interface %s does not exist", ifname);
        return -1;
    }

    // 建立netlink socket (短暫且一次性的操作，不需要設定flags, 阻塞模式即可)
    int nl_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (nl_fd == -1) {
        syslog(LOG_ERR, "Failed to create netlink socket: %m");
        return -1;
    }

    // 準備netlink訊息
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifinfo;
    } msg;

    memset(&msg, 0, sizeof(msg));
    msg.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    msg.nh.nlmsg_type = RTM_NEWLINK;
    msg.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;

    msg.ifinfo.ifi_family = AF_UNSPEC; // AF_UNSPEC表示不指定地址族
    msg.ifinfo.ifi_index = if_idx;
    msg.ifinfo.ifi_change = IFF_UP; // 指定要修改的標誌位
    msg.ifinfo.ifi_flags = admin_up ? IFF_UP : 0;

    // send netlink request to linux kernel    
    int ret = nl_send_req_and_wait_ack(nl_fd, &msg);

    close(nl_fd);
    return ret;
}

int nl_modify_ip_addr(const char *ifname, const char *ip_addr, uint8_t prefix_len, nl_ip_addr_action_t action) {
    // 確認ifname是否存在
    int if_idx = (int)if_nametoindex(ifname);
    if (if_idx == 0) {
        syslog(LOG_ERR, "Interface %s does not exist", ifname);
        return -1;
    }

    // 建立netlink socket (短暫且一次性的操作，不需要設定flags, 阻塞模式即可)
    int nl_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (nl_fd == -1) {
        syslog(LOG_ERR, "Failed to create netlink socket: %m");
        return -1;
    }

    // 準備netlink訊息
    // note: NLMSG_SPACE() 已經包含nlmsghdr的大小, 不需要再加上sizeof(struct nlmsghdr)
    char buf[NLMSG_SPACE(sizeof(struct ifaddrmsg)) + 2 * RTA_SPACE(sizeof(struct in_addr))];
    memset(buf, 0, sizeof(buf));

    // buf轉換成nlmsghdr結構體指標
    struct nlmsghdr *msg = (struct nlmsghdr *)buf;
    struct ifaddrmsg *ifaddr = (struct ifaddrmsg *)NLMSG_DATA(msg);


    msg->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    msg->nlmsg_type = (action == NL_IP_ADDR_ADD) ? RTM_NEWADDR : RTM_DELADDR;
    msg->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | (action == NL_IP_ADDR_ADD ? (NLM_F_CREATE | NLM_F_REPLACE) : 0);

    ifaddr->ifa_family = AF_INET;
    ifaddr->ifa_index = if_idx;
    ifaddr->ifa_prefixlen = prefix_len;

    struct in_addr ip_bin;
    if (inet_pton(AF_INET, ip_addr, &ip_bin) <= 0) {
        syslog(LOG_ERR, "Invalid IP address format: %s", ip_addr);
        close(nl_fd);
        return -EINVAL;
    }
    nl_add_rta(msg, IFA_LOCAL, &ip_bin, sizeof(ip_bin));
    nl_add_rta(msg, IFA_ADDRESS, &ip_bin, sizeof(ip_bin));

    // send netlink request to linux kernel
    int ret = nl_send_req_and_wait_ack(nl_fd, msg);

    close(nl_fd);
    return ret;
}
