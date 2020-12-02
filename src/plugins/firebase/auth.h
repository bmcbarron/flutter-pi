#ifndef _PLUGINS_FIREBASE_AUTH_H
#define _PLUGINS_FIREBASE_AUTH_H

#include "firebase/auth.h"
#include "module.h"
#include "util.h"

firebase::auth::Auth* get_auth(std_value* args);

class AuthModule : public Module {
public:
  AuthModule() : Module("plugins.flutter.io/firebase_auth") {
    Register("Auth#signInAnonymously", &AuthModule::SignInAnonymously);
  }
  
  virtual int OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle);

  virtual int SignInAnonymously(std_value *args, FlutterPlatformMessageResponseHandle *handle);
};

#endif
