# compiler and compile parameters
CFLAGS = -Wall -Wextra -Iinclude -g
CC = gcc

# target name
TARGET = netmgrd

# source files and object files
SRCS = src/main.c src/daemon.c src/netlink_monitor.c
OBJECTS = $(SRCS:.c=.o)

# default target
all: ${TARGET}

# link object files to create the executable
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

# compile each source file to an object file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# clean up object files and the executable
clean:
	rm -f $(OBJECTS) $(TARGET)

# delcare phony targets
.PHONY: all clean

