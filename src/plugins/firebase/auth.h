#ifndef _PLUGINS_FIREBASE_AUTH_H
#define _PLUGINS_FIREBASE_AUTH_H

#include "firebase/auth.h"
#include "module.h"
#include "util.h"

firebase::auth::Auth* get_auth(std_value* args);

class AuthModule : public Module {
public:
  AuthModule() : Module("plugins.flutter.io/firebase_auth") {}
  
  virtual int OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle);
};

#endif
