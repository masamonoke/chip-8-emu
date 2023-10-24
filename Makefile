TARGET_NAME := chip8emu
CC = gcc
SRC_DIR := src
DEPS_DIR := dependency
SRC := $(shell find $(SRC_DIR) -name '*.c')
DEPS := $(shell find $(DEPS_DIR) -name '*.c')
OBJ_RAW := $(shell find $(DEPS_DIR) -name '*.c' -type f -execdir echo '{}' ';' | sed "s/\.c/\.o/g") \
	   $(shell find $(SRC_DIR) -name '*.c' -type f -execdir echo '{}' ';' | sed "s/\.c/\.o/g") \

OBJ := $(shell find $(DEPS_DIR) -name '*.c' -type f -execdir echo '{}' ';' | sed "s/^/build\//g" | sed "s/\.c/\.o/g") \
	   $(shell find $(SRC_DIR) -name '*.c' -type f -execdir echo '{}' ';' | sed "s/^/build\//g" | sed "s/\.c/\.o/g")

INCLUDES := -Iinclude -Idependency/log/src
BUILD_TYPE = dev
SDL_PATH ?= /opt/homebrew/Cellar/sdl2/2.28.3
SDL2CFLAGS := -I$(SDL_PATH)/include -D_THREAD_SAFE
LDFLAGS := -L$(SDL_PATH)/lib -lSDL2
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
	 -g -DDEBUG -pthread $(SDL2CFLAGS)

CFLAGS_RELEASE := -pedantic -Wall \
     -Wno-missing-braces -Wextra -Wno-missing-field-initializers \
     -Wformat=2 -Wswitch-default -Wswitch-enum -Wcast-align \
     -Wpointer-arith -Wbad-function-cast -Wstrict-overflow=5 \
     -Wstrict-prototypes -Winline -Wundef -Wnested-externs \
     -Wcast-qual -Wshadow -Wunreachable-code \
     -Wfloat-equal -Wstrict-aliasing=2 -Wredundant-decls \
     -Wold-style-definition -Werror \
     -fno-omit-frame-pointer\
     -fno-common -fstrict-aliasing \
	 -O3 -DNDEBUG -funroll-loops $(SDL2CFLAGS)

$(TARGET_NAME): deps
	$(CC) $(OBJ) -o $@ -fsanitize=address $(LDFLAGS)

deps: $(DEPS) $(SRC)
	if ! test -d build; then mkdir build; fi
ifeq ($(BUILD_TYPE), release)
	$(CC) $(CFLAGS_RELEASE) -c $(DEPS) $(SRC) $(INCLUDES) && mv $(OBJ_RAW) build/
else
	$(CC) $(CFLAGS_DEV) -c $(DEPS) $(SRC) $(INCLUDES) && mv $(OBJ_RAW) build/
endif

clean:
	if test -d build || $(TARGET_NAME); then rm $(TARGET_NAME) && rm -rf build; else echo "Noting to clean"; fi
