#include "core.h"

#include "glib.h"
#include "libsecret/secret.h"

#include "firebase/log.h"

#include "debug.h"
#include "module.h"
#include "util.h"

firebase::App* get_app(std_value* args) {
  auto appName = get_string(args, "app");
  if (appName) {
    return firebase::App::GetInstance(appName->c_str());
  } else {
    return firebase::App::GetInstance();
  }
}

CoreModule::CoreModule() : Module("plugins.flutter.io/firebase_core") {
  firebase::SetLogLevel(firebase::kLogLevelVerbose);
  // Register("Firebase#initializeApp", &CoreModule::initializeApp);
  Register("Firebase#initializeCore", &CoreModule::initializeCore);
  // Register("Firebase#setAutomaticDataCollectionEnabled", &CoreModule::setAutomaticDataCollectionEnabled);
  // Register("Firebase#setAutomaticResourceManagementEnabled", &CoreModule::setAutomaticResourceManagementEnabled);
  // Register("Firebase#delete", &CoreModule::delete);
  GError* error = NULL;
  auto result = secret_password_store_sync(SECRET_SCHEMA_NOTE, SECRET_COLLECTION_DEFAULT, "bpm", "pass", NULL, &error, NULL);
  if (!result) {
    fprintf(stderr, "secret_password_store_sync error: %s\n", error->message);
  }
}

int CoreModule::initializeCore(std_value *args, FlutterPlatformMessageResponseHandle *handle) {
  // The default app values are read from google-services.json in the current working directory.
  auto defaultApp = firebase::App::Create();
  if (defaultApp == nullptr) {
    return error_message(handle, "Failed to initialize Firebase.");
  }

  std::vector<firebase::App *> apps;
  apps.push_back(defaultApp);

  auto result = std::make_unique<ValueList>();
  for (auto &app : apps) {
    auto appMap = std::make_unique<ValueMap>();
    if (strcmp(firebase::kDefaultAppName, app->name()) == 0) {
      // Flutter plugin and desktop API have different constants for the default.
      appMap->add(val("name"), val("[DEFAULT]"));
    } else {
      appMap->add(val("name"), val(app->name()));
    }
    //appMap->add(val("isAutomaticDataCollectionEnabled"), val(app->IsDataCollectionDefaultEnabled()));
    //appMap->add(val("pluginConstants"), val());
    auto options = std::make_unique<ValueMap>();
    options->add(val("apiKey"), val(app->options().api_key()));
    options->add(val("appId"), val(app->options().app_id()));
    options->add(val("messagingSenderId"), val(app->options().messaging_sender_id()));
    options->add(val("projectId"), val(app->options().project_id()));
    options->add(val("databaseURL"), val(app->options().database_url()));
    options->add(val("storageBucket"), val(app->options().storage_bucket()));
    options->add(val("trackingId"), val(app->options().ga_tracking_id()));
    appMap->add(val("options"), std::move(options));
    result->add(std::move(appMap));
  }

  return success(handle, std::move(result));
}