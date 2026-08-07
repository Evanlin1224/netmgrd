#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include <stddef.h>
#include <linux/if.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// UDS default socket path
#define DEFAULT_SOCKET_PATH "/tmp/netmgrd.sock"

// TLV 訊息類型定義
#define MSG_REQ_SHOW        0x01 // ndc -> daemon: 請求狀態列表 (Payload 長度為 0)
#define MSG_RESP_SHOW        0x02 // daemon -> ndc: 回傳狀態列表
#define MSG_REQ_MONITOR     0x03 // ndc -> daemon: 請求狀態監控 (Payload 長度為 0)
#define MSG_EVENT_MONITOR   0x04 // daemon -> ndc: 廣播網卡異動事件
#define MSG_REQ_SET_LINK    0x05 // ndc -> daemon: 設定網卡狀態 (Payload: struct ipc_if_info)
#define MSG_REQ_SET_IP      0x06 // ndc -> daemon: 設定網卡 IP
#define MSG_REQ_DEL_IP      0x07 // ndc -> daemon: 刪除網卡 IP
#define MSG_RESP_ACK        0x08 // daemon -> ndc: 回傳 ACK

// TLV header
struct ipc_header {
    uint8_t type;
    uint32_t length;   // Big-Endian
} __attribute__((packed));

// MSG_RESP_SHOW payload format:
// uint32_t count (Big-Endian) + count * struct ipc_if_info
struct ipc_if_info {
    uint32_t if_index;   // 4 bytes
    char if_name[IFNAMSIZ]; // 16 bytes
    uint8_t admin_up;    // 1 byte
    uint8_t carrier_up;  // 1 byte
    char ip_addr[INET6_ADDRSTRLEN]; // 46 bytes
} __attribute__((packed));

// MSG_EVENT_MONITOR payload format: struct ipc_if_event
struct ipc_if_event {
    uint32_t event_type; // RTM_NEWLINK 等
    uint32_t if_index;
    char if_name[IFNAMSIZ];
    uint8_t admin_up;
    uint8_t carrier_up;
    char ip_addr[INET6_ADDRSTRLEN];
} __attribute__((packed));

// MSG_REQ_SET_LINK payload format:
struct ipc_if_set_link {
    char if_name[IFNAMSIZ];
    uint8_t admin_up;
} __attribute__((packed));

// MSG_REQ_SET_IP payload format:
struct ipc_if_set_ip {
    char if_name[IFNAMSIZ];
    char ip_addr[INET6_ADDRSTRLEN];
    uint8_t prefix_len;
} __attribute__((packed));

// MSG_RESP_ACK payload format:
struct ipc_ack {
    int32_t status_code;
    char message[128];
} __attribute__((packed));


// 初始化 UDS 伺服器，回傳 listen fd
int ipc_server_init(const char *custom_path);

// 接收新的 Client 連線，並註冊至 epoll 監聽
int ipc_server_accept(int listen_fd, int epoll_fd);

// 讀取並處理 Client 傳入的 TLV 請求
void ipc_server_handle_client(int client_fd, int epoll_fd);

// 廣播網卡事件給所有訂閱者
void ipc_server_broadcast_event(struct ipc_if_event *event);

// 清理 UDS Socket 檔案與資源
void ipc_server_cleanup(void);


#endif  // IPC_H