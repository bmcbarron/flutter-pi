#include "core.h"

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

int CoreModule::OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle) {
  if (strcmp(object->method, "Firebase#initializeApp") == 0) {

    // TODO

  } else if (strcmp(object->method, "Firebase#initializeCore") == 0) {

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

  } else if (strcmp(object->method, "FirebaseApp#setAutomaticDataCollectionEnabled") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseApp#setAutomaticResourceManagementEnabled") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseApp#delete") == 0) {

    // TODO

  }

  return not_implemented(handle);
}
