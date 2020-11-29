#ifndef _PLUGINS_FIREBASE_DEBUG_H
#define _PLUGINS_FIREBASE_DEBUG_H

extern "C" {
#include "platformchannel.h"
}

#include <cassert>

// ***********************************************************

int stdPrint(std_value *value, int indent = 0);

// ***********************************************************

#endif
