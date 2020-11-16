#include "plugins/firebase.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include <memory>
#include <optional>
#include <vector>

#include "firebase/app.h"
#include "firebase/database.h"
#include "flutter-pi.h"
#include "compositor.h"
#include "pluginregistry.h"

#undef True
#undef False

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

static struct {
  char label[256];
  uint32_t primary_color;  // ARGB8888 (blue is the lowest byte)
  char isolate_id[32];
} services = {0};

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
  }
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
    case kStdInt32: return firebase::Variant(result->int32_value);
    case kStdInt64: return firebase::Variant(result->int64_value);
    case kStdFloat64: return firebase::Variant(result->float64_value);
    case kStdTrue: return firebase::Variant::True();
    case kStdFalse: return firebase::Variant::False();
    case kStdString: return firebase::Variant(result->string_value);
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

int success(FlutterPlatformMessageResponseHandle* handle,
            std::unique_ptr<Value> value = std::unique_ptr<Value>()) {
  if (!value) {
    value = val();
  }
  auto result = &value->build();
  fprintf(stderr, "<=\n  result:\n");
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

static int on_receive_core(
    char *channel, struct platch_obj *object,
    FlutterPlatformMessageResponseHandle *handle) {
  fprintf(stderr,
          "--------------------------------------------------------------------------\n"
          "on_receive_core(%s)\n"
          "  method: %s\n"
          "  args: \n",
          channel, object->method);
  stdPrint(&(object->std_arg), 4);

  if (strcmp(object->method, "Firebase#initializeApp") == 0) {
  } else if (strcmp(object->method, "Firebase#initializeCore") == 0) {
    std::vector<firebase::App *> apps;
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

      // options->add(val("apiKey"),
      // val("AIzaSyD7wXmhIQGeSjvF_09HX6-SH4aMVtsks1I"));
      // options->add(val("appId"),
      //               val("1:1052108509598:android:8f97ded05f4ad9539e868f"));
      // options->add(val("messagingSenderId"), val("1052108509598"));
      // options->add(val("projectId"), val("sopicade"));
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
    fprintf(stderr, "<=\n  result:\n");
    auto builtResult = result->build();
    stdPrint(&builtResult, 4);
    fprintf(stderr, "--------------------------------------------------------------------------\n");
    return platch_respond_success_std(handle, &builtResult);
  } else if (strcmp(object->method,
                    "FirebaseApp#setAutomaticDataCollectionEnabled") == 0) {
  } else if (strcmp(object->method,
                    "FirebaseApp#setAutomaticResourceManagementEnabled") == 0) {
  } else if (strcmp(object->method, "FirebaseApp#delete") == 0) {
  }

  fprintf(stderr, "<= XXXXXXXXXXXXXXXX (not implemented) XXXXXXXXXXXXXXXX\n"
                  "--------------------------------------------------------------------------\n");
  return platch_respond_not_implemented(handle);
}

int on_invoke_response(struct platch_obj *object, void *userdata) {
    if (object->codec == kNotImplemented) {
      printf("channel not implemented on flutter side\n");
      return 0;
    }

    if (object->success) {
        printf("on_response_std\n"
               "  success\n"
               "  result:\n");
        stdPrint(&object->std_result, 4);
    } else {
        printf("on_response_std\n");
        printf("  failure\n"
               "  error code: %s\n"
               "  error message: %s\n"
               "  error details:\n", object->error_code,
               (object->error_msg != NULL) ? object->error_msg : "null");
        stdPrint(&object->std_error_details, 4);
    }
    return 0;
}

class ValueListenerImpl : public firebase::database::ValueListener {
public:
  ValueListenerImpl(std::string channel, int id): channel(channel), id(id) {}

  virtual void OnValueChanged(const firebase::database::DataSnapshot& snapshot) {
    auto arguments = std::unique_ptr<ValueMap>(new ValueMap());
    auto snapshotMap = std::unique_ptr<ValueMap>(new ValueMap());
    snapshotMap->add(val("key"), val(snapshot.key()));
    snapshotMap->add(val("value"), val(snapshot.value()));
    arguments->add(val("handle"), val(id));
    arguments->add(val("snapshot"), std::move(snapshotMap));
    //arguments->add(val("previousSiblingKey"), val(id));
    auto value = val(snapshot.value());
    auto convertedValue = value->build();
    fprintf(stderr, "OnValueChanged(%s)\n  value:\n", snapshot.key());
    stdPrint(&convertedValue, 4);
    platch_call_std(const_cast<char*>(channel.c_str()), "Event", &arguments->build(),
                    on_invoke_response, nullptr);
  }

  virtual void OnCancelled(const firebase::database::Error& error, const char* error_message) {
    fprintf(stderr, "OnCancelled()\n");
  }

private:
  const std::string channel;
  const int id;
};

class ChildListenerImpl : public firebase::database::ChildListener {
public:
  virtual void OnChildAdded(const firebase::database::DataSnapshot& snapshot,
                            const char* previous_sibling_key) {
    fprintf(stderr, "OnChildAdded()\n");
  }

  virtual void OnChildChanged(const firebase::database::DataSnapshot& snapshot,
                              const char* previous_sibling_key) {
    fprintf(stderr, "OnChildChanged()\n");
  }

  virtual void OnChildMoved(const firebase::database::DataSnapshot& snapshot,
                            const char* previous_sibling_key) {
    fprintf(stderr, "OnChildMoved()\n");
  }

  virtual void OnChildRemoved(const firebase::database::DataSnapshot& snapshot) {
    fprintf(stderr, "OnChildRemoved()\n");
  }

  virtual void OnCancelled(const firebase::database::Error& error, const char* error_message) {
    fprintf(stderr, "OnCancelled()\n");
  }
};

int next_listener_id = 0;
auto value_listeners = std::map<int, ValueListenerImpl*>();
auto child_listeners = std::map<int, ChildListenerImpl*>();

static int on_receive_database(
    char *channel, struct platch_obj *object,
    FlutterPlatformMessageResponseHandle *handle) {
  fprintf(stderr,
          "--------------------------------------------------------------------------\n"
          "on_receive_database(%s)\n"
          "  method: %s\n"
          "  args: \n",
          channel, object->method);
  stdPrint(&(object->std_arg), 4);

  auto *arg = &(object->std_arg);
  if (arg->type != kStdMap) {
    return error(handle, "args isn't a args");
  }

  firebase::App *app = nullptr;
  auto appName = get_string(arg, "app");
  if (appName) {
    app = firebase::App::GetInstance(appName->c_str());
  } else {
    app = firebase::App::GetInstance();
  }
  if (app == nullptr) {
    return error(handle, "App (%s) not initialized.", appName.value_or("<default>"));
  }

  firebase::database::Database *database = nullptr;
  auto databaseUrl = get_string(arg, "databaseURL");
  if (databaseUrl) {
    database = firebase::database::Database::GetInstance(app, databaseUrl->c_str());
  } else {
    database = firebase::database::Database::GetInstance(app);
  }
  if (database == nullptr) {
    return error(handle, "Database not initialized.");
  }

  if (strcmp(object->method, "FirebaseDatabase#goOnline") == 0) {
  } else if (strcmp(object->method, "FirebaseDatabase#goOffline") == 0) {
  } else if (strcmp(object->method, "FirebaseDatabase#purgeOutstandingWrites") == 0) {
  } else if (strcmp(object->method, "FirebaseDatabase#setPersistenceEnabled") == 0) {

    database->set_persistence_enabled(stdmap_get_str(arg, "enabled")->bool_value);
    return success(handle);

  } else if (strcmp(object->method, "FirebaseDatabase#setPersistenceCacheSizeBytes") == 0) {

    // TODO(bpm)
    // database->set_persistence_cache_size(stdmap_get_str(arg, "cacheSize")->int32_value);
    return success(handle);

  } else if (strcmp(object->method, "DatabaseReference#set") == 0) {
  } else if (strcmp(object->method, "DatabaseReference#update") == 0) {
  } else if (strcmp(object->method, "DatabaseReference#setPriority") == 0) {
  } else if (strcmp(object->method, "DatabaseReference#runTransaction") == 0) {
  } else if (strcmp(object->method, "OnDisconnect#set") == 0) {
  } else if (strcmp(object->method, "OnDisconnect#update") == 0) {
  } else if (strcmp(object->method, "OnDisconnect#cancel") == 0) {
  } else if (strcmp(object->method, "Query#keepSynced") == 0) {

    auto query = get_query(database, arg);
    auto value = get_bool(arg, "value");
    if (value) {
      query.SetKeepSynchronized(*value);
      return success(handle);
    }

  } else if (strcmp(object->method, "Query#observe") == 0) {

    int id = next_listener_id++;
    auto query = get_query(database, arg);
    auto eventType = get_string(arg, "eventType");
    if (eventType.value_or("") == EVENT_TYPE_VALUE) {
      auto listener = new ValueListenerImpl(channel, id);
      value_listeners[id] = listener;
      query.AddValueListener(listener);
    } else {
      auto listener = new ChildListenerImpl();
      child_listeners[id] = listener;
      query.AddChildListener(listener);
    }
    return success(handle, val(id));

  } else if (strcmp(object->method, "Query#removeObserver") == 0) {

    auto id = get_int(arg, "handle");

  }

  return not_implemented(handle);
}

// static int on_receive_isolate(char *channel, struct platch_obj *object,
// FlutterPlatformMessageResponseHandle *handle) {
//     if (object->binarydata_size > sizeof(services.isolate_id)) {
//         return EINVAL;
//     } else {
//         memcpy(services.isolate_id, object->binarydata,
//         object->binarydata_size);
//     }

//     return platch_respond_not_implemented(handle);
// }

// static int on_receive_platform(char *channel, struct platch_obj *object,
// FlutterPlatformMessageResponseHandle *handle) {
//     struct json_value *value;
//     struct json_value *arg = &(object->json_arg);
//     int ok;

//     if (strcmp(object->method, "Clipboard.setData") == 0) {
//         /*
//          *  Clipboard.setData(Map data)
//          *      Places the data from the text entry of the argument,
//          *      which must be a Map, onto the system clipboard.
//          */
//     } else if (strcmp(object->method, "Clipboard.getData") == 0) {
//         /*
//          *  Clipboard.getData(String format)
//          *      Returns the data that has the format specified in the
//          argument
//          *      from the system clipboard. The only currently supported is
//          "text/plain".
//          *      The result is a Map with a single key, "text".
//          */
//     } else if (strcmp(object->method, "HapticFeedback.vibrate") == 0) {
//         /*
//          *  HapticFeedback.vibrate(void)
//          *      Triggers a system-default haptic response.
//          */
//     } else if (strcmp(object->method, "SystemSound.play") == 0) {
//         /*
//          *  SystemSound.play(String soundName)
//          *      Triggers a system audio effect. The argument must
//          *      be a String describing the desired effect; currently only
//          "click" is
//          *      supported.
//          */
//     } else if (strcmp(object->method,
//     "SystemChrome.setPreferredOrientations") == 0) {
//         /*
//          *  SystemChrome.setPreferredOrientations(DeviceOrientation[])
//          *      Informs the operating system of the desired orientation of
//          the display. The argument is a [List] of
//          *      values which are string representations of values of the
//          [DeviceOrientation] enum.
//          *
//          *  enum DeviceOrientation {
//          *      portraitUp, landscapeLeft, portraitDown, landscapeRight
//          *  }
//          */

//         value = &object->json_arg;

//         if ((value->type != kJsonArray) || (value->size == 0)) {
//             return platch_respond_illegal_arg_json(
//                 handle,
//                 "Expected `arg` to be an array with minimum size 1."
//             );
//         }

//         bool preferred_orientations[kLandscapeRight+1] = {0};

//         for (int i = 0; i < value->size; i++) {

//             if (value->array[i].type != kJsonString) {
//                 return platch_respond_illegal_arg_json(
//                     handle,
//                     "Expected `arg` to to only contain strings."
//                 );
//             }

//             enum device_orientation o =
//             ORIENTATION_FROM_STRING(value->array[i].string_value);

//             if (o == -1) {
//                 return platch_respond_illegal_arg_json(
//                     handle,
//                     "Expected `arg` to only contain stringifications of the "
//                     "`DeviceOrientation` enum."
//                 );
//             }

//             // if the list contains the current orientation, we just return
//             and don't change the current orientation at all. if (o ==
//             flutterpi.view.orientation) {
//                 return 0;
//             }

//             preferred_orientations[o] = true;
//         }

//         // if we have to change the orientation, we go through the
//         orientation enum in the defined order and
//         // select the first one that is preferred by flutter.
//         for (int i = kPortraitUp; i <= kLandscapeRight; i++) {
//             if (preferred_orientations[i]) {
//                 FlutterEngineResult result;

//                 flutterpi_fill_view_properties(true, i, false, 0);

//                 compositor_apply_cursor_state(true, flutterpi.view.rotation,
//                 flutterpi.display.pixel_ratio);

//                 // send updated window metrics to flutter
//                 result =
//                 flutterpi.flutter.libflutter_engine.FlutterEngineSendWindowMetricsEvent(flutterpi.flutter.engine,
//                 &(const FlutterWindowMetricsEvent) {
//                     .struct_size = sizeof(FlutterWindowMetricsEvent),
//                     .width = flutterpi.view.width,
//                     .height = flutterpi.view.height,
//                     .pixel_ratio = flutterpi.display.pixel_ratio
//                 });
//                 if (result != kSuccess) {
//                     fprintf(stderr, "[services] Could not send updated window
//                     metrics to flutter. FlutterEngineSendWindowMetricsEvent:
//                     %s\n", FLUTTER_RESULT_TO_STRING(result)); return
//                     platch_respond_error_json(handle, "engine-error",
//                     "Could not send updated window metrics to flutter",
//                     NULL);
//                 }

//                 return platch_respond_success_json(handle, NULL);
//             }
//         }

//         return platch_respond_illegal_arg_json(
//             handle,
//             "Expected `arg` to contain at least one element."
//         );
//     } else if (strcmp(object->method,
//     "SystemChrome.setApplicationSwitcherDescription") == 0) {
//         /*
//          *  SystemChrome.setApplicationSwitcherDescription(Map description)
//          *      Informs the operating system of the desired label and color
//          to be used
//          *      to describe the application in any system-level application
//          lists (e.g application switchers)
//          *      The argument is a Map with two keys, "label" giving a string
//          description,
//          *      and "primaryColor" giving a 32 bit integer value (the lower
//          eight bits being the blue channel,
//          *      the next eight bits being the green channel, the next eight
//          bits being the red channel,
//          *      and the high eight bits being set, as from Color.value for an
//          opaque color).
//          *      The "primaryColor" can also be zero to indicate that the
//          system default should be used.
//          */

//         value = jsobject_get(arg, "label");
//         if (value && (value->type == kJsonString))
//             snprintf(services.label, sizeof(services.label), "%s",
//             value->string_value);

//         return platch_respond_success_json(handle, NULL);
//     } else if (strcmp(object->method,
//     "SystemChrome.setEnabledSystemUIOverlays") == 0) {
//         /*
//          *  SystemChrome.setEnabledSystemUIOverlays(List overlays)
//          *      Specifies the set of system overlays to have visible when the
//          application
//          *      is running. The argument is a List of values which are
//          *      string representations of values of the SystemUIOverlay enum.
//          *
//          *  enum SystemUIOverlay {
//          *      top, bottom
//          *  }
//          *
//          */
//     } else if (strcmp(object->method, "SystemChrome.restoreSystemUIOverlays")
//     == 0) {
//         /*
//          * SystemChrome.restoreSystemUIOverlays(void)
//          */
//     } else if (strcmp(object->method, "SystemChrome.setSystemUIOverlayStyle")
//     == 0) {
//         /*
//          *  SystemChrome.setSystemUIOverlayStyle(struct SystemUIOverlayStyle)
//          *
//          *  enum Brightness:
//          *      light, dark
//          *
//          *  struct SystemUIOverlayStyle:
//          *      systemNavigationBarColor: null / uint32
//          *      statusBarColor: null / uint32
//          *      statusBarIconBrightness: null / Brightness
//          *      statusBarBrightness: null / Brightness
//          *      systemNavigationBarIconBrightness: null / Brightness
//          */
//     } else if (strcmp(object->method, "SystemNavigator.pop") == 0) {
//         flutterpi_schedule_exit();
//     }

//     return platch_respond_not_implemented(handle);
// }

// static int on_receive_accessibility(char *channel, struct platch_obj *object,
// FlutterPlatformMessageResponseHandle *handle) {
//     return platch_respond_not_implemented(handle);
// }

// static int on_receive_platform_views(char *channel, struct platch_obj
// *object, FlutterPlatformMessageResponseHandle *handle) {
//     struct json_value *value;
//     struct json_value *arg = &(object->json_arg);
//     int ok;

//     if STREQ("create", object->method) {
//         return platch_respond_not_implemented(handle);
//     } else if STREQ("dispose", object->method) {
//         return platch_respond_not_implemented(handle);
//     }

//     return platch_respond_not_implemented(handle);
// }

int firebase_init(void) {
  int ok;

  ok = plugin_registry_set_receiver(FIREBASE_CHANNEL_CORE, kStandardMethodCall,
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

  // ok = plugin_registry_set_receiver("flutter/platform", kJSONMethodCall,
  // on_receive_platform); if (ok != 0) {
  //     fprintf(stderr, "[services-plugin] could not set \"flutter/platform\"
  //     ChannelObject receiver: %s\n", strerror(ok)); goto
  //     fail_remove_isolate_receiver;
  // }

  // ok = plugin_registry_set_receiver("flutter/accessibility", kBinaryCodec,
  // on_receive_accessibility); if (ok != 0) {
  //     fprintf(stderr, "[services-plugin] could not set
  //     \"flutter/accessibility\" ChannelObject receiver: %s\n", strerror(ok));
  //     goto fail_remove_platform_receiver;
  // }

  // ok = plugin_registry_set_receiver("flutter/platform_views",
  // kStandardMethodCall, on_receive_platform_views); if (ok != 0) {
  //     fprintf(stderr, "[services-plugin] could not set
  //     \"flutter/platform_views\" ChannelObject receiver: %s\n",
  //     strerror(ok)); goto fail_remove_accessibility_receiver;
  // }

  return 0;

  // fail_remove_platform_views_receiver:
  // plugin_registry_remove_receiver("flutter/platform_views");

  // fail_remove_accessibility_receiver:
  // plugin_registry_remove_receiver("flutter/accessibility");

  // fail_remove_platform_receiver:
  // plugin_registry_remove_receiver("flutter/platform");

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
  // plugin_registry_remove_receiver("flutter/isolate");
  // plugin_registry_remove_receiver("flutter/platform");
  // plugin_registry_remove_receiver("flutter/accessibility");
  // plugin_registry_remove_receiver("flutter/platform_views");

  return 0;
}
