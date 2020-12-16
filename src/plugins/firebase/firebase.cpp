#include "module.h"

#include "auth.h"
#include "core.h"
#include "database.h"
#include "firestore.h"
#include "storage.h"

extern "C" {
#include "pluginregistry.h"
#include "plugins/firebase.h"
}

std::map<std::string, std::unique_ptr<Module>> channel_map;

int on_receive_callback(char *channel, platch_obj *object,
                        FlutterPlatformMessageResponseHandle *handle) {
  on_receive(channel, object);
  auto m = channel_map.find(channel);
  if (m == channel_map.end()) {
    return not_implemented(handle);
  }
  return m->second->OnMessage(object, handle);
}

int on_decode_firestore_type_std(enum std_value_type type, uint8_t **pbuffer, size_t *premaining,
                                 struct std_value *value_out);

int read_std_map(uint8_t **pbuffer, size_t *premaining, struct std_value *value_out,
                 std::initializer_list<const char *> keys, const char *field_value_type = nullptr) {
  value_out->type = kStdMap;
  uint32_t size = keys.size();
  if (field_value_type != nullptr) {
    size += 1;
  }
  value_out->size = size;

  value_out->keys = static_cast<std_value *>(calloc(size * 2, sizeof(struct std_value)));
  if (!value_out->keys)
    return ENOMEM;

  value_out->values = &value_out->keys[size];

  int startIndex = 0;
  if (field_value_type != nullptr) {
    startIndex = 1;
    value_out->keys[0] = {type : kStdString, string_value : strdup(kFieldValueTypeKey)};
    value_out->values[0] = {type : kStdString, string_value : strdup(field_value_type)};
  }
  auto nextKey = keys.begin();
  for (int i = startIndex; i < size; i++) {
    value_out->keys[i] = {type : kStdString, string_value : strdup(*nextKey++)};

    int ok = platch_decode_value_std(pbuffer, premaining, &(value_out->values[i]),
                                     on_decode_firestore_type_std);
    if (ok != 0)
      return ok;
  }
  return 0;
}

int read_std_list(uint8_t **pbuffer, size_t *premaining, struct std_value *value_out) {
  value_out->type = kStdList;
  uint32_t size;
  int ok = _readSize(pbuffer, &size, premaining);
  if (ok != 0)
    return ok;

  value_out->size = size;
  value_out->list = static_cast<std_value *>(calloc(size, sizeof(struct std_value)));

  for (int i = 0; i < size; i++) {
    ok = platch_decode_value_std(pbuffer, premaining, &value_out->list[i],
                                 on_decode_firestore_type_std);
    if (ok != 0)
      return ok;
  }
  return 0;
}

int on_decode_firestore_type_std(enum std_value_type type, uint8_t **pbuffer, size_t *premaining,
                                 struct std_value *value_out) {
  switch (type) {
  case 128: // DateTime
    fprintf(stderr, "on_decode_firestore_type_std(%d) = DateTime\n", type);
    return EBADMSG;
  case 129: // GeoPoint
    fprintf(stderr, "on_decode_firestore_type_std(%d) = GeoPoint\n", type);
    return EBADMSG;
  case 130: // DocumentReference
    return read_std_map(pbuffer, premaining, value_out, {"firestore", "path"});
  case 131: // Blob
    fprintf(stderr, "on_decode_firestore_type_std(%d) = Blob\n", type);
    return EBADMSG;
  case 132: // ArrayUnion
    fprintf(stderr, "on_decode_firestore_type_std(%d) = ArrayUnion\n", type);
    return EBADMSG;
  case 133: // ArrayRemove
    fprintf(stderr, "on_decode_firestore_type_std(%d) = ArrayRemove\n", type);
    return EBADMSG;
  case 134: // Delete
    return read_std_map(pbuffer, premaining, value_out, {}, "Delete");
  case 135: // ServerTimestamp
    return read_std_map(pbuffer, premaining, value_out, {}, "ServerTimestamp");
  case 136: // Timestamp
    return read_std_map(pbuffer, premaining, value_out, {"seconds", "nanos"}, "Timestamp");
  case 137: // IncrementDouble
    fprintf(stderr, "on_decode_firestore_type_std(%d) = IncrementDouble\n", type);
    return EBADMSG;
  case 138: // IncrementInteger
    fprintf(stderr, "on_decode_firestore_type_std(%d) = IncrementInteger\n", type);
    return EBADMSG;
  case 139: // DocumentId
    fprintf(stderr, "on_decode_firestore_type_std(%d) = DocumentId\n", type);
    return EBADMSG;
  case 140: // FieldPath
    return read_std_list(pbuffer, premaining, value_out);
  case 141: // NaN
    fprintf(stderr, "on_decode_firestore_type_std(%d) = NaN\n", type);
    return EBADMSG;
  case 142: // Infinity
    fprintf(stderr, "on_decode_firestore_type_std(%d) = Infinity\n", type);
    return EBADMSG;
  case 143: // NegativeInfinity
    fprintf(stderr, "on_decode_firestore_type_std(%d) = NegativeInfinity\n", type);
    return EBADMSG;
  case 144: // FirestoreInstance
    return read_std_map(pbuffer, premaining, value_out, {"appName", "settings"});
  case 145: // FirestoreQuery
    return platch_decode_value_std(pbuffer, premaining, value_out, on_decode_firestore_type_std);
  case 146: // FirestoreSettings
    return platch_decode_value_std(pbuffer, premaining, value_out, on_decode_firestore_type_std);
  }
  fprintf(stderr, "on_decode_firestore_type_std(%d)\n", type);
  return EBADMSG;
}

int firebase_init() {
  std::vector<std::unique_ptr<Module>> modules;
  modules.emplace_back(new CoreModule());
  modules.emplace_back(new AuthModule());
  modules.emplace_back(new DatabaseModule());
  modules.emplace_back(new StorageModule());
  modules.emplace_back(new FirestoreModule());
  modules.emplace_back(new Module("plugins.flutter.io/firebase_admob"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_analytics"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_functions"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_messaging"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_dynamic_links"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_remote_config"));

  for (auto &m : modules) {
    int result =
        plugin_registry_set_receiver(m->channel.c_str(), kStandardMethodCall, on_receive_callback);
    if (result != 0) {
      fprintf(stderr, "[firebase-plugin] could not set '%s' platform message receiver: %s\n",
              m->channel.c_str(), strerror(result));
      firebase_deinit();
      return result;
    }
    plugin_registry_extend_std_decode(m->channel.c_str(), on_decode_firestore_type_std);
    fprintf(stderr, "[firebase-plugin] Registered channel '%s'.\n", m->channel.c_str());
    channel_map.emplace(m->channel, std::move(m));
  }
  return 0;
}

int firebase_deinit() {
  for (const auto &m : channel_map) {
    plugin_registry_remove_receiver(m.second->channel.c_str());
  }
  channel_map.clear();
  return 0;
}
