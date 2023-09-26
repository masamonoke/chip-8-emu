TARGET_NAME := chip8emu
CC = gcc
SRC_DIR := src
DEPS_DIR := dependency
SRC := $(shell find $(SRC_DIR) -name '*.c')
DEPS := $(shell find $(DEPS_DIR) -name '*.c')
OBJ := $(shell find $(DEPS_DIR) -name '*.c' -type f -execdir echo '{}' ';' | sed "s/^/build\//g" | sed "s/.c/.o/g") \
	   $(shell find $(SRC_DIR) -name '*.c' -type f -execdir echo '{}' ';' | sed "s/^/build\//g" | sed "s/.c/.o/g") \

INCLUDES := -I../include -I../dependency/log/src
COMPILE_TYPE = dev
CFLAGS_DEV := -pedantic -Wall \
     -Wno-missing-braces -Wextra -Wno-missing-field-initializers \
     -Wformat=2 -Wswitch-default -Wswitch-enum -Wcast-align \
     -Wpointer-arith -Wbad-function-cast -Wstrict-overflow=5 \
     -Wstrict-prototypes -Winline -Wundef -Wnested-externs \
     -Wcast-qual -Wshadow -Wunreachable-code \
     -Wfloat-equal -Wstrict-aliasing=2 -Wredundant-decls \
     -Wold-style-definition -Werror \
     -fno-omit-frame-pointer\
     -fno-common -fstrict-aliasing \

CFLAGS_RELEASE := -pedantic -O3 -DNDEBUG -funroll-loops

$(TARGET_NAME): deps
ifeq ($(COMPILE_TYPE), release)
	$(CC) $(OBJ) -o $@ $(CFLAGS_RELEASE)
else
	$(CC) $(OBJ) -o $@ $(CFLAGS_DEV)
endif

deps: $(DEPS) $(SRC)
	mkdir build
	cd build && $(CC) -c ../$(DEPS) ../$(SRC) $(INCLUDES)

clean:
	rm $(TARGET_NAME) && rm -rf build
