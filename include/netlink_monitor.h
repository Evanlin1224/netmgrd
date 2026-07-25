#ifndef NL_MONITOR_H
#define NL_MONITOR_H

/* retrieve netlink socket file descriptor */
int nl_monitor_init();

/* parser the netlink messages */
/**
 * 此函數包含了對 netlink 消息的解析邏輯。它接收一個 netlink 套接字文件描述符，並從該套接字中讀取消息，解析其內容，
 * 並根據消息類型執行相應的操作。這可能包括處理網絡接口的變化、路由表更新等事件。
 */
void nl_monitor_parse(int nl_fd);

#endif // NL_MONITOR_H