#ifndef _PLUGINS_FIREBASE_AUTH_H
#define _PLUGINS_FIREBASE_AUTH_H

#include "module.h"

class AuthModule : public Module {
 public:
  AuthModule();

  virtual int OnMessage(platch_obj *object,
                        FlutterPlatformMessageResponseHandle *handle);

  virtual int SignInAnonymously(std_value *args,
                                FlutterPlatformMessageResponseHandle *handle);
};

#endif
