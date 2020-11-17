#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include <memory>
#include <optional>
#include <vector>

#include "firebase/app.h"
#include "firebase/database.h"

extern "C" {
#include "plugins/firebase.h"
#include "flutter-pi.h"
#include "compositor.h"
#include "pluginregistry.h"
}

#undef True
#undef False

// ***********************************************************

#define INDENT_STRING "                    "

int __stdPrint(struct std_value *value, int indent) {
  switch (value->type) {
    case kStdNull:
      fprintf(stderr, "null");
      break;
    case kStdTrue:
      fprintf(stderr, "true");
      break;
    case kStdFalse:
      fprintf(stderr, "false");
      break;
    case kStdInt32:
      fprintf(stderr, "%" PRIi32, value->int32_value);
      break;
    case kStdInt64:
      fprintf(stderr, "%" PRIi64, value->int64_value);
      break;
    case kStdFloat64:
      fprintf(stderr, "%lf", value->float64_value);
      break;
    case kStdString:
    case kStdLargeInt:
      fprintf(stderr, "\"%s\"", value->string_value);
      break;
    case kStdUInt8Array:
      fprintf(stderr, "(uint8_t) [");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "0x%02X", value->uint8array[i]);
        if (i + 1 != value->size) fprintf(stderr, ", ");
      }
      fprintf(stderr, "]");
      break;
    case kStdInt32Array:
      fprintf(stderr, "(int32_t) [");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%" PRIi32, value->int32array[i]);
        if (i + 1 != value->size) fprintf(stderr, ", ");
      }
      fprintf(stderr, "]");
      break;
    case kStdInt64Array:
      fprintf(stderr, "(int64_t) [");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%" PRIi64, value->int64array[i]);
        if (i + 1 != value->size) fprintf(stderr, ", ");
      }
      fprintf(stderr, "]");
      break;
    case kStdFloat64Array:
      fprintf(stderr, "(double) [");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%f", value->float64array[i]);
        if (i + 1 != value->size) fprintf(stderr, ", ");
      }
      fprintf(stderr, "]");
      break;
    case kStdList:
      fprintf(stderr, "[\n");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%.*s", indent + 2, INDENT_STRING);
        __stdPrint(&(value->list[i]), indent + 2);
        if (i + 1 != value->size) fprintf(stderr, ",\n");
      }
      fprintf(stderr, "\n%.*s]", indent, INDENT_STRING);
      break;
    case kStdMap:
      fprintf(stderr, "{\n");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%.*s", indent + 2, INDENT_STRING);
        __stdPrint(&(value->keys[i]), indent + 2);
        fprintf(stderr, ": ");
        __stdPrint(&(value->values[i]), indent + 2);
        if (i + 1 != value->size) fprintf(stderr, ",\n");
      }
      fprintf(stderr, "\n%.*s}", indent, INDENT_STRING);
      break;
    default:
      break;
  }
  return 0;
}

int stdPrint(struct std_value *value, int indent) {
  fprintf(stderr, "%.*s", indent, INDENT_STRING);
  __stdPrint(value, indent);
  fprintf(stderr, "\n");
  return 0;
}

// ***********************************************************

class Value {
 public:
  virtual ~Value() {}

  virtual std_value build() = 0;
};

class SimpleValue : public Value {
 public:
  SimpleValue(const std_value &value) : value(value) {}

  virtual std_value build() { return value; }

 private:
  const std_value value;
};

class ValueString : public Value {
 public:
  ValueString(std::string value) : value(value) {}

  std_value build() {
    return std_value{
        .type = kStdString,
        .string_value = const_cast<char *>(value.c_str()),
    };
  }

 private:
  const std::string value;
};

class ValueList : public Value {
 public:
  ValueList() {}

  void add(std::unique_ptr<Value> value) {
    values.push_back(std::move(value));
  }

  std_value build() {
    value_array.clear();
    for (auto& v : values) {
      value_array.push_back(v->build());
    }
    auto result = std_value{
        .type = kStdList,
    };
    result.size = value_array.size();
    result.list = &value_array[0];
    return result;
  }

 private:
  std::vector<std::unique_ptr<Value>> values;
  std::vector<std_value> value_array;
};

class ValueMap : public Value {
 public:
  ValueMap() {}

  void add(std::unique_ptr<Value> key, std::unique_ptr<Value> value) {
    keys.push_back(std::move(key));
    values.push_back(std::move(value));
  }

  std_value build() {
    key_array.clear();
    value_array.clear();
    for (int i = 0; i < keys.size(); ++i) {
      key_array.push_back(keys[i]->build());
      value_array.push_back(values[i]->build());
    }
    auto result = std_value{
        .type = kStdMap,
    };
    result.size = key_array.size();
    result.keys = &key_array[0];
    result.values = &value_array[0];
    return result;
  }

 private:
  std::vector<std::unique_ptr<Value>> keys;
  std::vector<std::unique_ptr<Value>> values;
  std::vector<std_value> key_array;
  std::vector<std_value> value_array;
};

std::unique_ptr<Value> val() {
  return std::unique_ptr<Value>(new SimpleValue({.type = kStdNull}));
}

std::unique_ptr<Value> val(bool value) {
  return std::unique_ptr<Value>(new SimpleValue({.type = value ? kStdTrue : kStdFalse}));
}

std::unique_ptr<Value> val(int value) {
  auto result = std_value{
      .type = kStdInt32,
  };
  result.int32_value = value;
  return std::unique_ptr<Value>(new SimpleValue(result));
}

std::unique_ptr<Value> val(int64_t value) {
  auto result = std_value{
      .type = kStdInt64,
  };
  result.int64_value = value;
  return std::unique_ptr<Value>(new SimpleValue(result));
}

std::unique_ptr<Value> val(double value) {
  auto result = std_value{
      .type = kStdFloat64,
  };
  result.float64_value = value;
  return std::unique_ptr<Value>(new SimpleValue(result));
}

std::unique_ptr<Value> val(const char *value) {
  return std::unique_ptr<Value>(new ValueString(value));
}

std::unique_ptr<Value> val(const std::string& value) {
  return std::unique_ptr<Value>(new ValueString(value));
}

std::unique_ptr<Value> val(const firebase::Variant& value) {
  switch (value.type()) {
    case firebase::Variant::Type::kTypeNull: return val();
    case firebase::Variant::Type::kTypeInt64: return val(value.int64_value());
    case firebase::Variant::Type::kTypeDouble: return val(value.double_value());
    case firebase::Variant::Type::kTypeBool: return val(value.bool_value());
    case firebase::Variant::Type::kTypeStaticString: return val(value.string_value());
    case firebase::Variant::Type::kTypeMutableString: return val(value.string_value());
    case firebase::Variant::Type::kTypeVector: {
      auto result = std::unique_ptr<ValueList>(new ValueList());
      for (auto const& v : value.vector()) {
        result->add(val(v));
      }
      return result;
    }
    case firebase::Variant::Type::kTypeMap: {
      auto result = std::unique_ptr<ValueMap>(new ValueMap());
      for (auto const& [k, v] : value.map()) {
        result->add(val(k), val(v));
      }
      return result;
    }
  }
  fprintf(stderr, "Error converting Variant type: %d\n", value.type());
  return val();
}

std::optional<std::string> get_string(struct std_value *args, char *key) {
	auto result = stdmap_get_str(args, key);
  if (result != nullptr && result->type == kStdString) {
    return result->string_value;
  }
  return std::nullopt;
}

std::optional<int> get_int(struct std_value *args, char *key) {
	auto result = stdmap_get_str(args, key);
  if (result != nullptr && result->type == kStdInt32) {
    return result->int32_value;
  }
  return std::nullopt;
}

std::optional<bool> get_bool(struct std_value *args, char *key) {
	auto result = stdmap_get_str(args, key);
  if (result != nullptr && result->type == kStdTrue) {
    return true;
  }
  if (result != nullptr && result->type == kStdFalse) {
    return false;
  }
  return std::nullopt;
}

std_value* get_map(std_value* args, char* key) {
	auto result = stdmap_get_str(args, key);
  return result != nullptr && result->type == kStdMap ? result : nullptr;
}

std::optional<firebase::Variant> get_variant(std_value* args, char* key) {
  auto result = stdmap_get_str(args, key);
  if (result == nullptr) {
    return std::nullopt;
  }
  switch (result->type) {
    case kStdNull: return firebase::Variant::Null();
    case kStdInt32: return firebase::Variant::FromInt64(result->int32_value);
    case kStdInt64: return firebase::Variant::FromInt64(result->int64_value);
    case kStdFloat64: return firebase::Variant::FromDouble(result->float64_value);
    case kStdTrue: return firebase::Variant::True();
    case kStdFalse: return firebase::Variant::False();
    case kStdString: return firebase::Variant::MutableStringFromStaticString(result->string_value);
  }
  return std::nullopt;
}

firebase::database::DatabaseReference get_reference(firebase::database::Database* database,
                                                   std_value* args) {
  auto path = get_string(args, "path");
  auto result = database->GetReference();
  if (path) {
    result = result.Child(*path);
  }
  return result;
}

firebase::database::Query get_query(firebase::database::Database* database, std_value* args) {
  firebase::database::Query query = get_reference(database, args);
  auto parameters = get_map(args, "parameters");
  if (parameters == nullptr) {
    return query;
  }
  auto order_by = get_string(parameters, "orderBy");
  if (order_by) {
    if (*order_by == "child") {
      auto order_by_child_key = get_string(parameters, "orderByChildKey");
      if (order_by_child_key) {
        query = query.OrderByChild(order_by_child_key->c_str());
      }
    } else if (*order_by == "key") {
      query = query.OrderByKey();
    } else if (*order_by == "value") {
      query = query.OrderByValue();
    } else if (*order_by == "priority") {
      query = query.OrderByPriority();
    }
  }
  auto start_at = get_variant(parameters, "startAt");
  if (start_at) {
    auto start_at_key = get_string(parameters, "startAtKey");
    if (start_at_key) {
      query = query.StartAt(*start_at, start_at_key->c_str());
    } else {
      query = query.StartAt(*start_at);
    }
  }
  auto end_at = get_variant(parameters, "endAt");
  if (end_at) {
    auto end_at_key = get_string(parameters, "endAtKey");
    if (end_at_key) {
      query = query.EndAt(*end_at, end_at_key->c_str());
    } else {
      query = query.EndAt(*end_at);
    }
  }  
  auto equal_to = get_variant(parameters, "equalTo");
  if (equal_to) {
    auto equal_to_key = get_string(parameters, "equalToKey");
    if (equal_to_key) {
      query = query.EqualTo(*equal_to, equal_to_key->c_str());
    } else {
      query = query.EqualTo(*equal_to);
    }
  }
  auto limit_to_first = get_int(parameters, "limitToFirst");
  if (limit_to_first) {
    query = query.LimitToFirst(*limit_to_first);
  }
  auto limit_to_last = get_int(parameters, "limitToLast");
  if (limit_to_last) {
    query = query.LimitToLast(*limit_to_last);
  }
  return query;
}

// ***********************************************************

int success(FlutterPlatformMessageResponseHandle* handle,
            std::unique_ptr<Value> value = std::unique_ptr<Value>()) {
  if (!value) {
    value = val();
  }
  auto result = &value->build();
  fprintf(stderr, "<=\n  result: ");
  stdPrint(result, 4);
  fprintf(stderr, "--------------------------------------------------------------------------\n");
  return platch_respond_success_std(handle, result);
}

int error(FlutterPlatformMessageResponseHandle* handle, const char* msg, ...) {
   ValueString error_details("error details");
  char buffer[256];
  va_list args;
  va_start(args, msg);
  vsnprintf(buffer, 256, msg, args);
  buffer[255] = '\0';
  va_end(args);
  fprintf(stderr, "<=\n  error: %s\n", buffer);
  fprintf(stderr, "--------------------------------------------------------------------------\n");
  return platch_respond_error_std(handle, "bpm", buffer, &error_details.build());
}

int not_implemented(FlutterPlatformMessageResponseHandle* handle) {
  fprintf(stderr, "<= XXXXXXXXXXXXXXXX (not implemented) XXXXXXXXXXXXXXXX\n"
                  "--------------------------------------------------------------------------\n");
  return platch_respond_not_implemented(handle);
}

void on_receive(char* channel, struct platch_obj* object, const char* handlerName) {
  fprintf(stderr,
          "--------------------------------------------------------------------------\n"
          "%s(%s)\n"
          "  method: %s\n"
          "  args: ",
          handlerName, channel, object->method);
  stdPrint(&(object->std_arg), 4);
}

int on_invoke_response(struct platch_obj *object, void *userdata) {
  if (object->codec == kNotImplemented) {
    printf("channel not implemented on flutter side\n");
    return 0;
  }

  if (object->success) {
    printf("on_response_std\n"
           "  result: ");
    stdPrint(&object->std_result, 4);
  } else {
    printf("on_response_std\n");
    printf("  error code: %s\n"
           "  error message: %s\n"
           "  error details: ", object->error_code,
           (object->error_msg != NULL) ? object->error_msg : "null");
    stdPrint(&object->std_error_details, 4);
  }
  return 0;
}

void invoke(std::string channel, std::string method, std::unique_ptr<Value> arguments) {
  fprintf(stderr, "invoke(%s, %s)\n  value: ", channel.c_str(), method.c_str());
  stdPrint(&arguments->build(), 4);
  platch_call_std(const_cast<char*>(channel.c_str()), const_cast<char*>(method.c_str()),
                  &arguments->build(), on_invoke_response, nullptr);
}

// ***********************************************************

static int on_receive_core(
    char *channel, struct platch_obj *object,
    FlutterPlatformMessageResponseHandle *handle) {
  on_receive(channel, object, "on_receive_core");

  if (strcmp(object->method, "Firebase#initializeApp") == 0) {

    // TODO

  } else if (strcmp(object->method, "Firebase#initializeCore") == 0) {

    std::vector<firebase::App *> apps;
    // The default app values are read from google-services.json in the current working directory.
    apps.push_back(firebase::App::Create());

    auto result = std::unique_ptr<ValueList>(new ValueList());
    for (auto &app : apps) {
      auto appMap = std::unique_ptr<ValueMap>(new ValueMap());
      if (strcmp(firebase::kDefaultAppName, app->name()) == 0) {
        // Flutter plugin and desktop API have different constants for the default.
        appMap->add(val("name"), val("[DEFAULT]"));
      } else {
        appMap->add(val("name"), val(app->name()));
      }
      //appMap->add(val("isAutomaticDataCollectionEnabled"), val(app->IsDataCollectionDefaultEnabled()));
      //appMap->add(val("pluginConstants"), val());
      auto options = std::unique_ptr<ValueMap>(new ValueMap());
      options->add(val("apiKey"), val(app->options().api_key()));
      options->add(val("appId"), val(app->options().app_id()));
      options->add(val("messagingSenderId"), val(app->options().messaging_sender_id()));
      options->add(val("projectId"), val(app->options().project_id()));
      options->add(val("databaseURL"), val(app->options().database_url()));
      options->add(val("storageBucket"), val(app->options().storage_bucket()));
      options->add(val("trackingId"), val(app->options().ga_tracking_id()));
      appMap->add(val("options"), std::move(options));
      result->add(std::move(appMap));
    }
    return success(handle, std::move(result));

  } else if (strcmp(object->method, "FirebaseApp#setAutomaticDataCollectionEnabled") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseApp#setAutomaticResourceManagementEnabled") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseApp#delete") == 0) {

    // TODO

  }

  return not_implemented(handle);
}

class SnapshotListener {
public:
  SnapshotListener(std::string channel, std::string eventType, int id): channel(channel), eventType(eventType), id(id) {}

  void sendEvent(std::string eventType, const firebase::database::DataSnapshot& snapshot,
                 std::optional<std::string> previousSiblingKey = std::nullopt) {
    if (eventType != this->eventType) {
      return;
    }
    auto arguments = std::unique_ptr<ValueMap>(new ValueMap());
    auto snapshotMap = std::unique_ptr<ValueMap>(new ValueMap());
    snapshotMap->add(val("key"), val(snapshot.key()));
    snapshotMap->add(val("value"), val(snapshot.value()));
    arguments->add(val("handle"), val(id));
    arguments->add(val("snapshot"), std::move(snapshotMap));
    if (previousSiblingKey) {
      arguments->add(val("previousSiblingKey"), val(*previousSiblingKey));
    }
    auto value = val(snapshot.value());
    auto convertedValue = value->build();
    invoke(channel, "Event", std::move(arguments));
  }

  void cancel(const firebase::database::Error& error) {
    auto arguments = std::unique_ptr<ValueMap>(new ValueMap());
    arguments->add(val("handle"), val(id));
    arguments->add(val("error"), val(error));
    invoke(channel, "Error", std::move(arguments));
  }

private:
  const std::string channel;
  const std::string eventType;
  const int id;
};

class ValueListenerImpl : public firebase::database::ValueListener {
public:
  ValueListenerImpl(std::string channel, std::string eventType, int id)
      : listener(channel, eventType, id) {}

  virtual void OnValueChanged(const firebase::database::DataSnapshot& snapshot) {
    listener.sendEvent(EVENT_TYPE_VALUE, snapshot);
  }

  virtual void OnCancelled(const firebase::database::Error& error, const char* error_message) {
    listener.cancel(error);
  }

private:
  SnapshotListener listener;
};

class ChildListenerImpl : public firebase::database::ChildListener {
public:
  ChildListenerImpl(std::string channel, std::string eventType, int id)
      : listener(channel, eventType, id) {}

  virtual void OnChildAdded(const firebase::database::DataSnapshot& snapshot,
                            const char* previousSiblingKey) {
    listener.sendEvent(EVENT_TYPE_CHILD_ADDED, snapshot, previousSiblingKey);
  }

  virtual void OnChildChanged(const firebase::database::DataSnapshot& snapshot,
                              const char* previousSiblingKey) {
    listener.sendEvent(EVENT_TYPE_CHILD_CHANGED, snapshot, previousSiblingKey);
  }

  virtual void OnChildMoved(const firebase::database::DataSnapshot& snapshot,
                            const char* previousSiblingKey) {
    listener.sendEvent(EVENT_TYPE_CHILD_MOVED, snapshot, previousSiblingKey);
  }

  virtual void OnChildRemoved(const firebase::database::DataSnapshot& snapshot) {
    listener.sendEvent(EVENT_TYPE_CHILD_REMOVED, snapshot);
  }

  virtual void OnCancelled(const firebase::database::Error& error, const char* error_message) {
    listener.cancel(error);
  }

private:
  SnapshotListener listener;
};

int next_listener_id = 0;
auto value_listeners = std::map<int, ValueListenerImpl*>();
auto child_listeners = std::map<int, ChildListenerImpl*>();

static int on_receive_database(
    char *channel, struct platch_obj *object,
    FlutterPlatformMessageResponseHandle *handle) {
  on_receive(channel, object, "on_receive_database");

  auto *args = &(object->std_arg);
  if (args->type != kStdMap) {
    return error(handle, "arguments isn't a map");
  }

  firebase::App *app = nullptr;
  auto appName = get_string(args, "app");
  if (appName) {
    app = firebase::App::GetInstance(appName->c_str());
  } else {
    app = firebase::App::GetInstance();
  }
  if (app == nullptr) {
    return error(handle, "App (%s) not initialized.", appName.value_or("<default>"));
  }

  firebase::database::Database *database = nullptr;
  auto databaseUrl = get_string(args, "databaseURL");
  if (databaseUrl) {
    database = firebase::database::Database::GetInstance(app, databaseUrl->c_str());
  } else {
    database = firebase::database::Database::GetInstance(app);
  }
  if (database == nullptr) {
    return error(handle, "Database not initialized.");
  }

  if (strcmp(object->method, "FirebaseDatabase#goOnline") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseDatabase#goOffline") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseDatabase#purgeOutstandingWrites") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseDatabase#setPersistenceEnabled") == 0) {

    database->set_persistence_enabled(stdmap_get_str(args, "enabled")->bool_value);
    return success(handle);

  } else if (strcmp(object->method, "FirebaseDatabase#setPersistenceCacheSizeBytes") == 0) {

    // TODO: This setting doesn't seem to exist on desktop.
    // database->set_persistence_cache_size(stdmap_get_str(args, "cacheSize")->int32_value);
    return success(handle);

  } else if (strcmp(object->method, "DatabaseReference#set") == 0) {

    // TODO

  } else if (strcmp(object->method, "DatabaseReference#update") == 0) {

    // TODO

  } else if (strcmp(object->method, "DatabaseReference#setPriority") == 0) {

    // TODO

  } else if (strcmp(object->method, "DatabaseReference#runTransaction") == 0) {

    // TODO

  } else if (strcmp(object->method, "OnDisconnect#set") == 0) {

    // TODO

  } else if (strcmp(object->method, "OnDisconnect#update") == 0) {

    // TODO

  } else if (strcmp(object->method, "OnDisconnect#cancel") == 0) {

    // TODO

  } else if (strcmp(object->method, "Query#keepSynced") == 0) {

    auto query = get_query(database, args);
    auto value = get_bool(args, "value");
    if (value) {
      query.SetKeepSynchronized(*value);
      return success(handle);
    }

  } else if (strcmp(object->method, "Query#observe") == 0) {

    int id = next_listener_id++;
    auto query = get_query(database, args);
    auto eventType = get_string(args, "eventType");
    if (eventType) {
      if (eventType == EVENT_TYPE_VALUE) {
        auto listener = new ValueListenerImpl(channel, *eventType, id);
        value_listeners[id] = listener;
        query.AddValueListener(listener);
      } else {
        auto listener = new ChildListenerImpl(channel, *eventType, id);
        child_listeners[id] = listener;
        query.AddChildListener(listener);
      }
      return success(handle, val(id));
    }

  } else if (strcmp(object->method, "Query#removeObserver") == 0) {

    auto id = get_int(args, "handle");
    auto query = get_query(database, args);    
    if (!id) {
      // Fall through.
    } else if (value_listeners.count(*id)) {
      auto listener = value_listeners[*id];
      value_listeners.erase(*id);
      query.RemoveValueListener(listener);
      delete listener;
      return success(handle);
    } else if (child_listeners.count(*id)) {
      auto listener = child_listeners[*id];
      child_listeners.erase(*id);
      query.RemoveChildListener(listener);
      delete listener;
      return success(handle);
    }

  }

  return not_implemented(handle);
}

int firebase_init(void) {
  int ok = plugin_registry_set_receiver(FIREBASE_CHANNEL_CORE, kStandardMethodCall,
      on_receive_core);
  if (ok != 0) {
    fprintf(
        stderr,
        "[firebase-plugin] could not set \%s\" platform message receiver: %s\n",
        FIREBASE_CHANNEL_CORE, strerror(ok));
    goto fail_return_ok;
  }

  ok = plugin_registry_set_receiver(FIREBASE_CHANNEL_DATABASE,
                                    kStandardMethodCall, on_receive_database);
  if (ok != 0) {
    fprintf(stderr,
            "[firebase-plugin] could not set  \%s\" platform message receiver: "
            "%s\n",
            FIREBASE_CHANNEL_DATABASE, strerror(ok));
    goto fail_remove_core_receiver;
  }

  return 0;

fail_remove_database_receiver:
  plugin_registry_remove_receiver(FIREBASE_CHANNEL_DATABASE);

fail_remove_core_receiver:
  plugin_registry_remove_receiver(FIREBASE_CHANNEL_CORE);

fail_return_ok:
  return ok;
}

int firebase_deinit(void) {
  plugin_registry_remove_receiver(FIREBASE_CHANNEL_CORE);
  plugin_registry_remove_receiver(FIREBASE_CHANNEL_DATABASE);
  return 0;
}
