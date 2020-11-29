#ifndef _PLUGINS_FIREBASE_DATABASE_H
#define _PLUGINS_FIREBASE_DATABASE_H

#include "firebase/database.h"
#include "module.h"
#include "value.h"

const char EVENT_TYPE_CHILD_ADDED[] = "_EventType.childAdded";
const char EVENT_TYPE_CHILD_REMOVED[] = "_EventType.childRemoved";
const char EVENT_TYPE_CHILD_CHANGED[] = "_EventType.childChanged";
const char EVENT_TYPE_CHILD_MOVED[] = "_EventType.childMoved";
const char EVENT_TYPE_VALUE[] = "_EventType.value";

firebase::database::DatabaseReference get_reference(firebase::database::Database* database,
                                                    std_value* args);
firebase::database::Query get_query(firebase::database::Database* database, std_value* args);

class DatabaseModule : public Module {
public:
  DatabaseModule() : Module("plugins.flutter.io/firebase_database") {}
  
  virtual int OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle);
};

#endif
