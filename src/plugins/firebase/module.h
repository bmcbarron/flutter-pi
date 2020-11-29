#ifndef _PLUGINS_FIREBASE_MODULE_H
#define _PLUGINS_FIREBASE_MODULE_H

#include <algorithm>
#include <cstring>
#include <vector>

#include "util.h"

class Module {
public:
  const std::string channel;

  Module(std::string channel) : channel(channel) {}
  virtual ~Module() {}

  virtual int OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle) = 0;
};

#endif
