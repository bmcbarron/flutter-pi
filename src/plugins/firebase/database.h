#ifndef _PLUGINS_FIREBASE_DATABASE_H
#define _PLUGINS_FIREBASE_DATABASE_H

#include "module.h"

class DatabaseModule : public Module {
 public:
  DatabaseModule();

  virtual int OnMessage(platch_obj *object,
                        FlutterPlatformMessageResponseHandle *handle);
};

#endif
