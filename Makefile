TARGET_NAME := chip8emu
CC = gcc
SRC_DIR := src
SRC := $(shell find $(SRC_DIR) -name '*.c')
OBJ := $(shell find $(SRC_DIR) -name '*.c' -type f -execdir echo '{}' ';' | sed "s/^/build\/obj\//g" | sed "s/.c/.o/g")

$(TARGET_NAME): $(SRC)
	$(CC) -o $@ $^

clean:
	rm $(TARGET_NAME)
