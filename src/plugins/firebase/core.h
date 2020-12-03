#ifndef _PLUGINS_FIREBASE_CORE_H
#define _PLUGINS_FIREBASE_CORE_H

#include "firebase/app.h"
#include "module.h"
#include "util.h"

firebase::App* get_app(std_value* args);

class CoreModule : public Module {
public:
  CoreModule();

private:
  virtual int initializeCore(std_value *args, FlutterPlatformMessageResponseHandle *handle);
};

#endif
