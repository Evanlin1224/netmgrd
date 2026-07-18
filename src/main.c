#include "daemon.h"
#define _XOPEN_SOURCE 700 // to fix the warning msg for `struct sigaction`
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>

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

    while(keep_running) {
        // Daemon main loop
        syslog(LOG_INFO, "Daemon is running...");
        sleep(5);
    }

    // graceful shutdown and release resources
    syslog(LOG_INFO, "Daemon stopped.");
    closelog();

    return 0;
}