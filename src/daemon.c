#include "daemon.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


void daemonize() {
    printf("Daemon initialized.\n");

    // first fork
    pid_t pid = fork();
    if (pid < 0) {
        perror("First fork failed.\n");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // set new session
    if (setsid() < 0) {
        perror("Failed to create a new session.\n");
        exit(EXIT_FAILURE);
    }

    // Second fork
    pid = fork();
    if (pid < 0) {
        perror("Second fork failed.\n");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // rest umask setting
    umask(0);

    // change working directory to root
    if (chdir("/") < 0) {
        perror("Failed to change working directory to root.\n");
        exit(EXIT_FAILURE);
    }

    // redirect standard file descriptors to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) { // ensure that the file descriptor is valid
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    } else {
        perror("Failed to open /dev/null.\n");
        exit(EXIT_FAILURE);
    }
    
}