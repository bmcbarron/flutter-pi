COMMON_CFLAGS = \
  -I./include $(shell pkg-config --cflags gbm libdrm glesv2 egl libsystemd libinput libudev xkbcommon) \
  -I../firebase-cpp-sdk/app/src/include \
  -I../firebase-cpp-sdk/database/src/include \
	-DBUILD_TEXT_INPUT_PLUGIN \
	-DBUILD_TEST_PLUGIN \
	-DBUILD_OMXPLAYER_VIDEO_PLAYER_PLUGIN \
	-O0 \
	-g \
	-w \
	-Wno-psabi \
	-Wif-not-aligned \
	-fsanitize=address \
	-fno-omit-frame-pointer \
	-fno-optimize-sibling-calls

#	-funwind-tables \

REAL_CFLAGS = $(CFLAGS) $(COMMON_CFLAGS)

REAL_CXXFLAGS = $(CXXFLAGS) $(COMMON_CFLAGS) -std=gnu++17

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
	-fsanitize=address \
	$(LDFLAGS)

SOURCES = src/flutter-pi.c \
	src/platformchannel.c \
	src/pluginregistry.c \
	src/texture_registry.c \
	src/compositor.c \
	src/modesetting.c \
	src/collection.c \
	src/cursor.c \
	src/keyboard.c \
	src/plugins/services.c \
	src/plugins/testplugin.c \
	src/plugins/text_input.c \
	src/plugins/raw_keyboard.c \
	src/plugins/omxplayer_video_player.c

CXX_SOURCES =	src/plugins/firebase.cpp

EXTRA_DEPS = include/jsmn.h Makefile

CC = /usr/bin/clang-9
CXX = /usr/bin/clang++-9

OBJECTS = $(SOURCES:src/%.c=out/obj/%.o) $(CXX_SOURCES:src/%.cpp=out/obj/%.o)
HEADERS = $(SOURCES:src/%.c=include/%.h) $(CXX_SOURCES:src/%.cpp=include/%.h)

all: out/flutter-pi

out/obj/%.o: src/%.c include/%.h $(EXTRA_DEPS)
	@mkdir -p $(@D)
	$(CC) -c $(REAL_CFLAGS) $< -o $@

out/obj/%.o: src/%.cpp include/%.h $(EXTRA_DEPS)
	@mkdir -p $(@D)
	$(CXX) -c $(REAL_CXXFLAGS) $< -o $@

out/flutter-pi: $(OBJECTS) $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(OBJECTS) $(REAL_LDFLAGS) -o out/flutter-pi

clean:
	@mkdir -p out
	rm -rf $(OBJECTS) out/flutter-pi out/obj/*
