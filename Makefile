DEP_LIBS = egl gbm glesv2 glib-2.0 libdrm libinput libsystemd libudev xkbcommon libsecret-1

COMMON_CFLAGS = \
  -I./include $(shell pkg-config --cflags $(DEP_LIBS)) \
  -I../firebase-cpp-sdk/app/src/include \
  -I../firebase-cpp-sdk/auth/src/include \
  -I../firebase-cpp-sdk/database/src/include \
	-DBUILD_TEXT_INPUT_PLUGIN \
	-DBUILD_TEST_PLUGIN \
	-DBUILD_OMXPLAYER_VIDEO_PLAYER_PLUGIN \
	-w \
	-Wno-psabi \
	-Wif-not-aligned \
	-O0 \
	-gfull \
	-fsanitize=address \
	-fno-omit-frame-pointer \
	-fno-optimize-sibling-calls

#	-funwind-tables \

REAL_CFLAGS = $(CFLAGS) $(COMMON_CFLAGS)

REAL_CXXFLAGS = $(CXXFLAGS) $(COMMON_CFLAGS) -std=gnu++17

REAL_LDFLAGS = \
  -L../firebase-cpp-sdk/desktop_build/database -lfirebase_database \
  -L../firebase-cpp-sdk/desktop_build/auth -lfirebase_auth \
  -L../firebase-cpp-sdk/desktop_build/app -lfirebase_app \
  -L../firebase-cpp-sdk/desktop_build/app/rest -lfirebase_rest_lib \
  -L../firebase-cpp-sdk/desktop_build/external/src/flatbuffers-build -lflatbuffers \
  -L../firebase-cpp-sdk/desktop_build/external/src/zlib-build -lz \
  -L../firebase-cpp-sdk/desktop_build -llibuWS \
	-L../firebase-cpp-sdk/desktop_build/external/src/firestore-build/external/src/leveldb-build -lleveldb \
	$(shell pkg-config --libs $(DEP_LIBS)) \
	-lcurl \
	-lrt \
	-lpthread \
	-ldl \
	-lm \
	-lssl \
	-lsecret-1 \
	-lcrypto \
	-latomic \
	-rdynamic \
	-gfull \
	-fsanitize=address \
	$(LDFLAGS)

SOURCES = $(shell find src/ -type f -name '*.c')
CXX_SOURCES = $(shell find src/ -type f -name '*.cpp')

CC = /usr/bin/clang-9
CXX = /usr/bin/clang++-9
LD = /usr/bin/clang++-9

OBJECTS = $(SOURCES:src/%.c=out/obj/%.o) $(CXX_SOURCES:src/%.cpp=out/obj/%.o)

all: out/flutter-pi

foo:
	@echo $(SOURCES)
	@echo $(CXX_SOURCES)
	@echo $(OBJECTS)

out/obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) -c $(REAL_CFLAGS) -MD $< -o $@

out/obj/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) -c $(REAL_CXXFLAGS) -MD $< -o $@

out/flutter-pi: $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) $(OBJECTS) $(REAL_LDFLAGS) -o out/flutter-pi

clean:
	@mkdir -p out
	rm -rf $(OBJECTS) out/flutter-pi out/obj/*

-include $(OBJECTS:%.o=%.d)
