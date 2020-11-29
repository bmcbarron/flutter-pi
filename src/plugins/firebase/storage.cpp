#include "storage.h"

#include "debug.h"
#include "module.h"
#include "util.h"

int StorageModule::OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle) {
  return not_implemented(handle);
}
