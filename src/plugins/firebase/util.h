#ifndef _PLUGINS_FIREBASE_UTIL_H
#define _PLUGINS_FIREBASE_UTIL_H

#include <memory>
#include <optional>

#include "firebase/variant.h"
#include "value.h"

extern "C" {
long gettid();
}

std::unique_ptr<Value> val(const firebase::Variant& value);
std::optional<firebase::Variant> as_variant(std_value* value);
std::optional<firebase::Variant> get_variant(std_value* args, char* key);

template<typename K, typename V>
static V* map_get(const std::map<K, V*>& m, const K& k) {
  auto result = m.find(k);
  return result != m.end() ? result->second : nullptr;
}

enum InvokeResult {
  INVOKE_RESULT,
  INVOKE_TIMEDOUT,
  INVOKE_FAILED,
};

void invoke(std::string channel, std::string method, std::unique_ptr<Value> arguments);
InvokeResult invoke_sync(std::string channel, std::string method, std::unique_ptr<Value> arguments,
                         timespec* deadline, platch_obj *response);

void on_receive(char* channel, platch_obj* object);
int pending();
int success(FlutterPlatformMessageResponseHandle* handle,
            std::unique_ptr<Value> result = std::unique_ptr<Value>());
int error_message(FlutterPlatformMessageResponseHandle* handle, const char* msg, ...);
int error(FlutterPlatformMessageResponseHandle* handle,
          std::string error_code,
          std::string error_message,
          std::unique_ptr<Value> error_details = std::unique_ptr<Value>());
int not_implemented(FlutterPlatformMessageResponseHandle* handle);

#endif
