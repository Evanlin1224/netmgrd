#ifndef NL_CONTROL_H
#define NL_CONTROL_H

#include <stdint.h>

// define IP address operation types
typedef enum {
    NL_IP_ADDR_ADD = 0,
    NL_IP_ADDR_DEL = 1,
} nl_ip_addr_action_t;

int nl_set_link_status(const char *ifname, int admin_up);

int nl_modify_ip_addr(const char *ifname, const char *ip_addr, uint8_t prefix_len, nl_ip_addr_action_t action);

#endif // NL_CONTROL_H