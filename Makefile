REAL_CFLAGS = -I./include $(shell pkg-config --cflags gbm libdrm glesv2 egl libsystemd libinput libudev xkbcommon) \
  -I../firebase-cpp-sdk/app/src/include \
  -I../firebase-cpp-sdk/database/src/include \
	-DBUILD_TEXT_INPUT_PLUGIN \
	-DBUILD_TEST_PLUGIN \
	-DBUILD_OMXPLAYER_VIDEO_PLAYER_PLUGIN \
	-O0 -ggdb \
	-funwind-tables \
	-fpermissive \
	-w \
  -std=gnu++17 \
	-Wno-psabi \
	-Wif-not-aligned \
	$(CFLAGS)

REAL_LDFLAGS = \
  -L../firebase-cpp-sdk/desktop_build/database -lfirebase_database \
  -L../firebase-cpp-sdk/desktop_build/app -lfirebase_app \
  -L../firebase-cpp-sdk/desktop_build/external/src/flatbuffers-build -lflatbuffers \
  -L../firebase-cpp-sdk/desktop_build/external/src/zlib-build -lz \
  -L../firebase-cpp-sdk/desktop_build -llibuWS \
	-L../firebase-cpp-sdk/desktop_build/external/src/firestore-build/external/src/leveldb-build -lleveldb \
	$(shell pkg-config --libs gbm libdrm glesv2 egl libsystemd libinput libudev xkbcommon) \
	-lrt \
	-lpthread \
	-ldl \
	-lm \
	-lssl \
	-lcrypto \
	-latomic \
	-rdynamic \
	$(LDFLAGS)

SOURCES = src/flutter-pi.cpp \
	src/platformchannel.cpp \
	src/pluginregistry.c \
	src/texture_registry.c \
	src/compositor.c \
	src/modesetting.c \
	src/collection.c \
	src/cursor.c \
	src/keyboard.c \
	src/std_codec.cpp \
	src/plugins/firebase.cpp \
	src/plugins/services.c \
	src/plugins/testplugin.c \
	src/plugins/text_input.c \
	src/plugins/raw_keyboard.c \
	src/plugins/omxplayer_video_player.c

EXTRA_HEADERS = include/jsmn.h include/intmem.h Makefile

CC = /usr/bin/g++
OBJECTS = $(patsubst src/%.c,out/obj/%.o,$(SOURCES))
HEADERS = $(patsubst src/%.c,include/%.h,$(SOURCES)) $(EXTRA_HEADERS)

all: out/flutter-pi

out/obj/%.o: src/%.c include/%.h $(EXTRA_HEADERS)
	@mkdir -p $(@D)
	$(CC) -c $(REAL_CFLAGS) $< -o $@

out/flutter-pi: $(OBJECTS) $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(REAL_CFLAGS) $(OBJECTS) $(REAL_LDFLAGS) -o out/flutter-pi

clean:
	@mkdir -p out
	rm -rf $(OBJECTS) out/flutter-pi out/obj/*
