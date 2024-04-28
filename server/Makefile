CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror -DUSE_AESD_CHAR_DEVICE
LDFLAGS ?= -pthread -lrt
TARGET ?= aesdsocket

all: $(TARGET)

$(TARGET): aesdsocket.o
	@$(CC) aesdsocket.o -o $(TARGET) $(LDFLAGS)

%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@rm -f *.o $(TARGET)


