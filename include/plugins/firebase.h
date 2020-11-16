#ifndef _PLUGINS_FIREBASE_H
#define _PLUGINS_FIREBASE_H

#include <stdio.h>
#include <string.h>

#define FIREBASE_CHANNEL_CORE "plugins.flutter.io/firebase_core"
#define FIREBASE_CHANNEL_DATABASE "plugins.flutter.io/firebase_database"

extern int firebase_init(void);
extern int firebase_deinit(void);

const char EVENT_TYPE_CHILD_ADDED[] = "_EventType.childAdded";
const char EVENT_TYPE_CHILD_REMOVED[] = "_EventType.childRemoved";
const char EVENT_TYPE_CHILD_CHANGED[] = "_EventType.childChanged";
const char EVENT_TYPE_CHILD_MOVED[] = "_EventType.childMoved";
const char EVENT_TYPE_VALUE[] = "_EventType.value";

#endif
