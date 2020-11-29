#ifndef _PLUGINS_FIREBASE_STORAGE_H
#define _PLUGINS_FIREBASE_STORAGE_H

#include "firebase/app.h"
#include "module.h"
#include "util.h"

class StorageModule : public Module {
public:
  StorageModule() : Module("plugins.flutter.io/firebase_storage") {}

  virtual int OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle);
};

#endif
