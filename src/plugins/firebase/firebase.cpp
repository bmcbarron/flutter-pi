#include "module.h"
#include "core.h"
#include "auth.h"
#include "database.h"
#include "storage.h"

extern "C" {
#include "plugins/firebase.h"
#include "pluginregistry.h"
}

std::map<std::string, std::unique_ptr<Module>> channel_map;

int on_receive_callback(char *channel, platch_obj *object,
                        FlutterPlatformMessageResponseHandle *handle) {
  on_receive(channel, object);
  auto m = channel_map.find(channel);
  if (m == channel_map.end()) {
    return not_implemented(handle);
  }
  return m->second->OnMessage(object, handle);
}

int firebase_init() {
  std::vector<std::unique_ptr<Module>> modules;
  modules.emplace_back(new CoreModule());
  modules.emplace_back(new AuthModule());
  modules.emplace_back(new DatabaseModule());
  modules.emplace_back(new StorageModule());
  modules.emplace_back(new Module("plugins.flutter.io/firebase_admob"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_analytics"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_firestore"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_functions"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_messaging"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_dynamic_links"));
  modules.emplace_back(new Module("plugins.flutter.io/firebase_remote_config"));

  for (auto& m : modules) {
    int result = plugin_registry_set_receiver(m->channel.c_str(), kStandardMethodCall,
        on_receive_callback);
    if (result != 0) {
      fprintf(stderr, "[firebase-plugin] could not set '%s' platform message receiver: %s\n",
              m->channel.c_str(), strerror(result));
      firebase_deinit();
      return result;
    }
    fprintf(stderr, "[firebase-plugin] Registered channel '%s'.\n", m->channel.c_str());
    channel_map.emplace(m->channel, std::move(m));
  }
  return 0;
}

int firebase_deinit() {
  for (const auto& m : channel_map) {
    plugin_registry_remove_receiver(m.second->channel.c_str());
  }
  channel_map.clear();
  return 0;
}
