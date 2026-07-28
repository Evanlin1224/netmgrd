#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/if.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <syslog.h>

/* define the cache table and counter */
static interface_state_t g_state_table[MAX_INTERFACES];
static int g_interface_count = 0;

void init_state_table(void) {
    memset(g_state_table, 0, sizeof(g_state_table));
    g_interface_count = 0;
    syslog(LOG_INFO, "State table initialized.");
}

// helper function: find the index
static int find_interface_index (int idx) {
    for (int i = 0; i < g_interface_count; i++) {
        if (g_state_table[i].index == idx)
            return i;
    }
    return -1;
}

// helper function: 移除指定位置的快取並向前緊縮陣列
static void remove_interface_at(int idx) {
    if (idx < 0 || idx >= g_interface_count) {
        return;
    }
    for (int i = idx; i < g_interface_count - 1; i++) {
        g_state_table[i] = g_state_table[i + 1];
    }
    g_interface_count--;
    memset(&g_state_table[g_interface_count], 0, sizeof(interface_state_t));
}

void update_state_table_link (int index, char *name, int admin_up, int carrier_up, int is_del) {
    int idx = find_interface_index(index);
    if (is_del) {
        if (idx != -1) {
            syslog(LOG_INFO, "StateTable: Removing interface %s (index %d)", g_state_table[idx].name, index);
            remove_interface_at(idx);
        }
        return;
    }

    if (idx != -1) {
        // update current entry
        g_state_table[idx].admin_up = admin_up;
        g_state_table[idx].carrier_up = carrier_up;
        syslog(LOG_INFO, "StateTable: Updating interface %s (index %d): admin %s, carrier %s", g_state_table[idx].name, index, admin_up ? "up" : "down", carrier_up ? "up" : "down");

    } else {
        // add new entry
        if (g_interface_count >= MAX_INTERFACES) {
            syslog(LOG_ERR, "StateTable: Maximum  number of interfaces reached.");
            return;
        }

        int new_idx = g_interface_count++;
        g_state_table[new_idx].index = index;
        g_state_table[new_idx].admin_up = admin_up;
        g_state_table[new_idx].carrier_up = carrier_up;
        if (name && strlen(name) > 0) {
            strncpy(g_state_table[new_idx].name, name, sizeof(g_state_table[new_idx].name) - 1);
        } else {
            snprintf(g_state_table[new_idx].name, sizeof(g_state_table[new_idx].name), "if%d", index);
        }
        syslog(LOG_INFO, "StateTable: Adding new interface %s (index %d): admin %s, carrier %s", g_state_table[new_idx].name, index, admin_up ? "up" : "down", carrier_up ? "up" : "down");
    }
}

void update_state_table_ip (int index, char *name, char *ip_addr, int is_del) {
    int idx = find_interface_index(index);
    if (idx == -1) {
        if (is_del) return;
        if (g_interface_count >= MAX_INTERFACES) {
            syslog(LOG_WARNING, "StateTable: Table full cannot add interface (index: %d) for IP", index);
            return;
        }

        // 如果 IP 事件先於 Link 事件到達，則建立一個初始條目
        idx = g_interface_count++;
        g_state_table[idx].index = index;
        if (name && strlen(name) > 0) {
            strncpy(g_state_table[idx].name, name, sizeof(g_state_table[idx].name) - 1);
        } else {
            snprintf(g_state_table[idx].name, sizeof(g_state_table[idx].name), "if%d", index);
        
        }
    }

    if (is_del) {
        if (strcmp(g_state_table[idx].ip_addr, ip_addr) == 0) {
            memset(g_state_table[idx].ip_addr, 0, sizeof(g_state_table[idx].ip_addr));
            syslog(LOG_INFO,"StateTable: Cleared IP on interface %s (index %d)", g_state_table[idx].name, index);
        }
    } else {
        strncpy(g_state_table[idx].ip_addr, ip_addr, sizeof(g_state_table[idx].ip_addr) - 1);
        syslog(LOG_INFO, "StateTable: Set IP %s on interface %s (index %d)", g_state_table[idx].ip_addr, g_state_table[idx].name, index);
    }
}

int get_state_table (interface_state_t *out_table, int max_entries) {
    int count = g_interface_count < max_entries ? g_interface_count : max_entries;
    if (count > 0 && out_table != NULL) {
        memcpy(out_table, g_state_table, sizeof(interface_state_t) * count);
    }
    return count;
}

int get_state_by_index (int index, interface_state_t *out_state) {
    int idx = find_interface_index(index);
    if (idx != -1 && out_state != NULL) {
        *out_state = g_state_table[idx];
        return 0;
    } else {
        return -1;
    }
}
