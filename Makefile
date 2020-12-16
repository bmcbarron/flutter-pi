DEP_LIBS = egl gbm glesv2 glib-2.0 libdrm libinput libsystemd libudev xkbcommon

DEBUG_FLAGS = \
	-Og \
	-glldb \
	-g3 \
	-fsanitize=address \
	-fno-omit-frame-pointer \
	-fno-optimize-sibling-calls

#	-funwind-tables \

COMMON_FLAGS = \
  -I./include $(shell pkg-config --cflags $(DEP_LIBS)) \
  -I../firebase-cpp-sdk/app/src/include \
  -I../firebase-cpp-sdk/auth/src/include \
  -I../firebase-cpp-sdk/database/src/include \
  -I../firebase-cpp-sdk/firestore/src/include \
  -I$(FIREBASE_LIB)/external/src/firestore/Firestore/core/include \
	-DBUILD_TEXT_INPUT_PLUGIN \
	-DBUILD_TEST_PLUGIN \
	-DBUILD_OMXPLAYER_VIDEO_PLAYER_PLUGIN \
	-w \
	-Wno-psabi \
	-Wif-not-aligned \
	$(DEBUG_FLAGS)

REAL_CFLAGS = $(CFLAGS) $(COMMON_FLAGS)

REAL_CXXFLAGS = $(CXXFLAGS) $(COMMON_FLAGS) -std=gnu++17

FIREBASE_LIB = ../firebase-cpp-sdk/desktop_build
FIRESTORE_LIB = $(FIREBASE_LIB)/external/src/firestore-build
GRPC_LIB = $(FIRESTORE_LIB)/external/src/grpc-build
ABSL_LIB = $(GRPC_LIB)/third_party/abseil-cpp/absl

REAL_LDFLAGS = \
  -L$(FIREBASE_LIB)/firestore -lfirebase_firestore \
	-L$(FIRESTORE_LIB)/Firestore/core -L$(FIRESTORE_LIB)/Firestore/Protos \
  -lfirestore_core -lfirestore_protos_nanopb -lfirestore_nanopb -lfirestore_util \
	-L$(FIRESTORE_LIB)/external/src/nanopb-build -lprotobuf-nanopb \
	-L$(FIRESTORE_LIB)/external/src/leveldb-build -lleveldb \
	-L$(GRPC_LIB) -L$(GRPC_LIB)/third_party/cares/cares/lib \
	-lgrpc++ -lgrpc -lgpr -lupb -lcares -laddress_sorting \
	-L$(ABSL_LIB)/strings \
	-labsl_strings -labsl_strings_internal -labsl_str_format_internal \
	-L$(ABSL_LIB)/numeric -labsl_int128 \
	-L$(ABSL_LIB)/types -labsl_bad_optional_access -labsl_bad_variant_access \
	-L$(ABSL_LIB)/base \
	-labsl_raw_logging_internal -labsl_dynamic_annotations -labsl_base -labsl_throw_delegate \
	-labsl_log_severity -labsl_spinlock_wait \
  -L$(FIREBASE_LIB)/database -lfirebase_database \
  -L$(FIREBASE_LIB)/auth -lfirebase_auth \
  -L$(FIREBASE_LIB)/app -lfirebase_app \
  -L$(FIREBASE_LIB)/app/rest -lfirebase_rest_lib \
  -L$(FIREBASE_LIB)/external/src/flatbuffers-build -lflatbuffers \
  -L$(FIREBASE_LIB)/external/src/zlib-build -lz \
  -L$(FIREBASE_LIB) -llibuWS \
	-L$(FIRESTORE_LIB)/external/src/leveldb-build -lleveldb \
	$(shell pkg-config --libs $(DEP_LIBS)) \
	-lcurl \
	-lrt \
	-lpthread \
	-ldl \
	-lm \
	-lssl \
	-lcrypto \
	-latomic \
	-rdynamic \
	$(DEBUG_FLAGS) \
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
