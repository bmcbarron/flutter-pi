#include "core.h"

#include "firebase/log.h"
#include "glib.h"
#include "module.h"
#include "util.h"

firebase::App *get_app(std_value *args) {
  // Flutter plugin and desktop API have different constants for the
  // default.
  auto appName = get_string(args, "appName");
  if (appName && *appName != "[DEFAULT]") {
    return firebase::App::GetInstance(appName->c_str());
  } else {
    return firebase::App::GetInstance();
  }
}

std::string get_app_name(const firebase::App *app) {
  // Flutter plugin and desktop API have different constants for the
  // default.
  if (strcmp(firebase::kDefaultAppName, app->name()) == 0) {
    return "[DEFAULT]";
  } else {
    return app->name();
  }
}

CoreModule::CoreModule() : Module("plugins.flutter.io/firebase_core") {
  firebase::SetLogLevel(firebase::kLogLevelVerbose);
  // Register("Firebase#initializeApp", &CoreModule::initializeApp);
  Register("Firebase#initializeCore", &CoreModule::initializeCore);
  // Register("Firebase#setAutomaticDataCollectionEnabled",
  // &CoreModule::setAutomaticDataCollectionEnabled);
  // Register("Firebase#setAutomaticResourceManagementEnabled",
  // &CoreModule::setAutomaticResourceManagementEnabled);
  // Register("Firebase#delete", &CoreModule::delete);
}

int CoreModule::initializeCore(std_value *args, FlutterPlatformMessageResponseHandle *handle) {
  // The default app values are read from google-services.json in the current
  // working directory.
  auto defaultApp = firebase::App::Create();
  if (defaultApp == nullptr) {
    return error_message(handle, "Failed to initialize Firebase.");
  }

  std::vector<firebase::App *> apps;
  apps.push_back(defaultApp);

  auto result = std::make_unique<ValueList>();
  for (auto &app : apps) {
    auto appMap = std::make_unique<ValueMap>();
    appMap->set("name", get_app_name(app));
    // appMap->set("isAutomaticDataCollectionEnabled",
    // app->IsDataCollectionDefaultEnabled()); appMap->set("pluginConstants", );
    auto options = std::make_unique<ValueMap>();
    options->set("apiKey", app->options().api_key());
    options->set("appId", app->options().app_id());
    options->set("messagingSenderId", app->options().messaging_sender_id());
    options->set("projectId", app->options().project_id());
    options->set("databaseURL", app->options().database_url());
    options->set("storageBucket", app->options().storage_bucket());
    options->set("trackingId", app->options().ga_tracking_id());
    appMap->set("options", std::move(options));
    result->add(std::move(appMap));
  }

  return success(handle, std::move(result));
}