# compiler and compile parameters
CFLAGS = -Wall -Wextra -Iinclude -g
CC = gcc

# target name
DAEMON_TARGET = netmgrd
CLI_TARGET = ndc

# source files and object files
DAEMON_SRCS = src/main.c src/daemon.c src/netlink_monitor.c src/netlink_control.c src/state.c src/ipc.c
CLI_SRCS = cli/ndc.c cli/ipc_client.c

DAEMON_OBJECTS = $(DAEMON_SRCS:.c=.o)
CLI_OBJECTS = $(CLI_SRCS:.c=.o)

# default target
all: ${DAEMON_TARGET} ${CLI_TARGET}

# link object files to create the executable
$(DAEMON_TARGET): $(DAEMON_OBJECTS)
	$(CC) $(CFLAGS) -o $(DAEMON_TARGET) $(DAEMON_OBJECTS)

$(CLI_TARGET): $(CLI_OBJECTS)
	$(CC) $(CFLAGS) -o $(CLI_TARGET) $(CLI_OBJECTS)

# compile each source file to an object file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# clean up object files and the executable
clean:
	rm -f $(DAEMON_OBJECTS) $(CLI_OBJECTS) $(DAEMON_TARGET) $(CLI_TARGET)

# delcare phony targets
.PHONY: all clean

