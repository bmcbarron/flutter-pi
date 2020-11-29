#ifndef _PLUGINS_FIREBASE_CORE_H
#define _PLUGINS_FIREBASE_CORE_H

#include "firebase/app.h"
#include "module.h"
#include "util.h"

firebase::App* get_app(std_value* args);

class CoreModule : public Module {
public:
  CoreModule() : Module("plugins.flutter.io/firebase_core") {}

  virtual int OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle);
};

#endif
