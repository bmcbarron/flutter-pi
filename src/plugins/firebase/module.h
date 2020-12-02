#ifndef _PLUGINS_FIREBASE_MODULE_H
#define _PLUGINS_FIREBASE_MODULE_H

#include <algorithm>
#include <cstring>
#include <vector>
#include <type_traits>

#include "util.h"

class Module {
public:

  const std::string channel;

  Module(std::string channel) : channel(channel) {}
  virtual ~Module() {}

  virtual int OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle);

protected:
  template <typename M>
  void Register(std::string method,
                int (M::*handler)(std_value *args, FlutterPlatformMessageResponseHandle *handle)) {
    static_assert(std::is_base_of_v<Module, M>, "M not dervied from Module");
    handlers[method] = static_cast<Handler>(handler);
  }

private:
  typedef int (Module::*Handler)(std_value *args, FlutterPlatformMessageResponseHandle *handle);
  std::map<std::string, Handler> handlers;
};

#endif
