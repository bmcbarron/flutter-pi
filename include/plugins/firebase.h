#ifndef _PLUGINS_FIREBASE_H
#define _PLUGINS_FIREBASE_H

#include <stdio.h>
#include <string.h>

#define FIREBASE_CHANNEL_CORE "plugins.flutter.io/firebase_core"
#define FIREBASE_CHANNEL_DATABASE "plugins.flutter.io/firebase_database"

extern int firebase_init(void);
extern int firebase_deinit(void);

#endif