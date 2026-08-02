#define _XOPEN_SOURCE 700 // to fix the warning msg for `struct sigaction`
#include "daemon.h"
#include "netlink_monitor.h"
#include "state.h"
#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>

#define MAX_EVENTS 10

static volatile sig_atomic_t keep_running = 1;

// signal handler
void handle_signal(int signal) {
    if (signal == SIGTERM) {
        syslog(LOG_INFO, "Received SIGTERM. Terminating gracefully...");
        keep_running = 0;
    }
}

int main() {
    openlog("netmgrd", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Starting daemon...");
    
    daemonize();
    syslog(LOG_INFO, "Daemonized successfully.");

    init_state_table();

    // Register signal handler for SIGTERM
    struct sigaction act;
    act.sa_handler = handle_signal;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGTERM, &act, NULL) < 0) {
        syslog(LOG_ERR, "Failed to register SIGTERM handler.");
        closelog();
        exit(EXIT_FAILURE);
    }

    /* Initialize netlink socket */
    int nl_fd = nl_monitor_init();
    if (nl_fd < 0) {
        syslog(LOG_ERR, "Failed to initialize netlink monitor.");
        closelog();
        exit(EXIT_FAILURE);
    }

    // init ipc server
    int ipc_listen_fd = ipc_server_init(NULL);
    if (ipc_listen_fd < 0) {
        syslog(LOG_ERR, "Failed to initialize IPC server.");
        close(nl_fd);
        closelog();
        exit(EXIT_FAILURE);
    }

    /* create epoll instance */
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        syslog(LOG_ERR, "Failed to create epoll instance.");
        close(nl_fd);
        close(ipc_listen_fd);
        closelog();
        exit(EXIT_FAILURE);
    }

    /* add netlink socket to epoll*/
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = nl_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, nl_fd, &ev) < 0) {
        syslog(LOG_ERR, "Failed to add netlink socket fd to epoll: %m");
        close(epoll_fd);
        close(nl_fd);
        closelog();
        exit(EXIT_FAILURE);
    }

    /* add UDS socket to poll */
    ev.events = EPOLLIN;
    ev.data.fd = ipc_listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ipc_listen_fd, &ev) < 0) {
        syslog(LOG_ERR, "Failed to add UDS socket fd to epoll: %m");
        close(epoll_fd);
        close(nl_fd);
        close(ipc_listen_fd);
        closelog();
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS];
    syslog(LOG_INFO, "Entering main event loop ...");

    while(keep_running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                // Interrupted by a signal, continue waiting
                continue;
            }
            syslog(LOG_ERR, "epoll_wait error: %m");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            if (fd == nl_fd) {
                nl_monitor_parse(nl_fd);
            } else if (fd == ipc_listen_fd) {
                ipc_server_accept(ipc_listen_fd, epoll_fd);
            } else {
                ipc_server_handle_client(fd, epoll_fd);
            }
        }
    }

    // graceful shutdown and release resources
    syslog(LOG_INFO, "Cleaning up resources ...");
    close(epoll_fd);
    close(nl_fd);
    close(ipc_listen_fd);
    ipc_server_cleanup();

    syslog(LOG_INFO, "Daemon stopped.");
    closelog();

    return 0;
}