#ifndef STATE_H
#define STATE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/if.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_INTERFACES 24

typedef struct {
    int index;
    char name[IFNAMSIZ];          // Interface name
    char ip_addr[INET6_ADDRSTRLEN]; // IP address (IPv4 or IPv6)
    int admin_up;                   // Administrative state (1 for up, 0 for down)
    int carrier_up;                 // Carrier state (1 for up, 0 for down)
} interface_state_t;

void init_state_table (void);

void update_state_table_link (int index, char *name, int admin_up, int carrier_up, int is_del);

void update_state_table_ip (int index, char *name, char *ip_addr, int is_del);

int get_state_table (interface_state_t *out_table, int max_entries);

// 根據 index 查詢單一介面狀態，成功回傳 0，失敗回傳 -1 (例如廣播事件時需要查詢現有 Link 狀態)
int get_state_by_index (int index, interface_state_t *out_state); 

#endif // STATE_H