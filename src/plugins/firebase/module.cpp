#include "module.h"

#include <functional>

int Module::OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle) {
  auto h = handlers.find(object->method);
  if (h == handlers.end()) {
    return not_implemented(handle);
  }
  auto *args = &(object->std_arg);
  if (args->type != kStdMap && args->type != kStdNull) {
    return error_message(handle, "arguments isn't a map");
  }
  return std::invoke(h->second, this, args, handle);
}
