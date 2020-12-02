#include "auth.h"

#include "debug.h"
#include "core.h"
#include "util.h"

firebase::auth::Auth* get_auth(std_value* args) {
  auto app = get_app(args);
  return app ? firebase::auth::Auth::GetAuth(app) : nullptr;
}

std::unique_ptr<Value> parsePhotoUrl(std::string photoUrl) {
  if (photoUrl.empty()) {
    return val();
  }
  return val(photoUrl);
}

std::unique_ptr<Value> parseUserInfo(firebase::auth::UserInfoInterface* userInfo) {
  auto output = std::make_unique<ValueMap>();

  output->add(val("displayName"), val(userInfo->display_name()));
  output->add(val("email"), val(userInfo->email()));
  output->add(val("phoneNumber"), val(userInfo->phone_number()));
  output->add(val("photoURL"), parsePhotoUrl(userInfo->photo_url()));
  output->add(val("providerId"), val(userInfo->provider_id()));
  output->add(val("uid"), val(userInfo->uid()));

  return std::move(output);
}

std::unique_ptr<Value> parseUserInfoList(
    const std::vector<firebase::auth::UserInfoInterface *>& userInfoList) {
  auto output = std::make_unique<ValueList>();
  for (auto userInfo : userInfoList) {
    // https://firebase.google.com/docs/reference/android/com/google/firebase/auth/FirebaseAuthProvider#PROVIDER_ID
    if ("firebase" != userInfo->provider_id()) {
      output->add(parseUserInfo(userInfo));
    }
  }
  return std::move(output);
}

std::unique_ptr<Value> parseFirebaseUser(const firebase::auth::User* user) {
  if (user == nullptr) {
    return val();
  }

  auto output = std::make_unique<ValueMap>();
  auto metadata = std::make_unique<ValueMap>();

  output->add(val("displayName"), val(user->display_name()));
  output->add(val("email"), val(user->email()));
  output->add(val("emailVerified"), val(user->is_email_verified()));
  output->add(val("isAnonymous"), val(user->is_anonymous()));

  metadata->add(val("creationTime"), val(user->metadata().creation_timestamp));
  metadata->add(val("lastSignInTime"), val(user->metadata().last_sign_in_timestamp));
  
  output->add(val("metadata"), std::move(metadata));
  output->add(val("phoneNumber"), val(user->phone_number()));
  output->add(val("photoURL"), parsePhotoUrl(user->photo_url()));
  output->add(val("providerData"), parseUserInfoList(user->provider_data()));
  output->add(val("refreshToken"), val(""));
  output->add(val("uid"), val(user->uid()));

  return std::move(output);
}

std::unique_ptr<Value> parseAuthResult(const firebase::auth::User* user) {
  if (user == nullptr) {
    return val();
  }

  auto output = std::make_unique<ValueMap>();

  output->add(val("additionalUserInfo"), val());
  output->add(val("authCredential"), val());
  output->add(val("user"), parseFirebaseUser(user));

  return std::move(output);
}

class AuthStateListenerImpl : public firebase::auth::AuthStateListener {
public:
  AuthStateListenerImpl(std::string channel, std::string appName)
      : channel(channel), appName(appName) {}

  virtual void OnAuthStateChanged(firebase::auth::Auth* auth) {
      auto event = std::make_unique<ValueMap>();
      event->add(val("appName"), val(appName));

      auto user = auth->current_user();
      event->add(val("user"), parseFirebaseUser(user));

      invoke(channel, "Auth#authStateChanges", std::move(event));
  }

private:
  const std::string channel;
  const std::string appName;
};

// Stores the instances of native AuthCredentials by their hashCode
static std::map<int, firebase::auth::Credential*>* authCredentials;
static std::map<std::string, firebase::auth::AuthStateListener*> authListeners;
static std::map<std::string, firebase::auth::IdTokenListener*> idTokenListeners;
static std::map<int, firebase::auth::PhoneAuthProvider::ForceResendingToken*> forceResendingTokens;

int AuthModule::OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle) {
  auto *args = &(object->std_arg);
  if (args->type != kStdMap) {
    return error_message(handle, "arguments isn't a map");
  }

  if (strcmp(object->method, "Auth#registerChangeListeners") == 0) {

    auto appName = get_string(args, "appName");
    auto app = get_app(args);
    if (app == nullptr) {
      return error_message(handle, "App (%s) not initialized.", appName.value_or("<default>").c_str());
    }
    auto auth = get_auth(args);

    auto authStateListener = map_get(authListeners, *appName);
    auto idTokenListener = map_get(idTokenListeners, *appName);

    if (authStateListener == nullptr) {
      auto newAuthStateListener = new AuthStateListenerImpl(channel, *appName);


      auth->AddAuthStateListener(newAuthStateListener);
      authListeners[*appName] = newAuthStateListener;
    }

    return success(handle);

  } else {

  // case "Auth#registerChangeListeners":
  // case "Auth#applyActionCode":
  // case "Auth#checkActionCode":
  // case "Auth#confirmPasswordReset":
  // case "Auth#createUserWithEmailAndPassword":
  // case "Auth#fetchSignInMethodsForEmail":
  // case "Auth#sendPasswordResetEmail":
  // case "Auth#sendSignInLinkToEmail":
  // case "Auth#signInWithCredential":
  // case "Auth#setLanguageCode":
  // case "Auth#setSettings":
  // case "Auth#signInAnonymously":
  // case "Auth#signInWithCustomToken":
  // case "Auth#signInWithEmailAndPassword":
  // case "Auth#signInWithEmailLink":
  // case "Auth#signOut":
  // case "Auth#verifyPasswordResetCode":
  // case "Auth#verifyPhoneNumber":
  // case "User#delete":
  // case "User#getIdToken":
  // case "User#linkWithCredential":
  // case "User#reauthenticateUserWithCredential":
  // case "User#reload":
  // case "User#sendEmailVerification":
  // case "User#unlink":
  // case "User#updateEmail":
  // case "User#updatePassword":
  // case "User#updatePhoneNumber":
  // case "User#updateProfile":
  // case "User#verifyBeforeUpdateEmail":
  
  // } else if (strcmp(object->method, "Firebase#initializeCore") == 0) {

  //   // The default app values are read from google-services.json in the current working directory.
  //   auto defaultApp = firebase::App::Create();
  //   if (defaultApp == nullptr) {
  //     return error(handle, "Failed to initialize Firebase.");
  //   }

  //   std::vector<firebase::App *> apps;
  //   apps.push_back(defaultApp);

  //   auto result = std::make_unique<ValueList>();
  //   for (auto &app : apps) {
  //     auto appMap = std::make_unique<ValueMap>();
  //     if (strcmp(firebase::kDefaultAppName, app->name()) == 0) {
  //       // Flutter plugin and desktop API have different constants for the default.
  //       appMap->add(val("name"), val("[DEFAULT]"));
  //     } else {
  //       appMap->add(val("name"), val(app->name()));
  //     }
  //     //appMap->add(val("isAutomaticDataCollectionEnabled"), val(app->IsDataCollectionDefaultEnabled()));
  //     //appMap->add(val("pluginConstants"), val());
  //     auto options = std::make_unique<ValueMap>();
  //     options->add(val("apiKey"), val(app->options().api_key()));
  //     options->add(val("appId"), val(app->options().app_id()));
  //     options->add(val("messagingSenderId"), val(app->options().messaging_sender_id()));
  //     options->add(val("projectId"), val(app->options().project_id()));
  //     options->add(val("databaseURL"), val(app->options().database_url()));
  //     options->add(val("storageBucket"), val(app->options().storage_bucket()));
  //     options->add(val("trackingId"), val(app->options().ga_tracking_id()));
  //     appMap->add(val("options"), std::move(options));
  //     result->add(std::move(appMap));
  //   }
  //   return success(handle, std::move(result));

  // } else if (strcmp(object->method, "FirebaseApp#setAutomaticDataCollectionEnabled") == 0) {

  //   // TODO

  // } else if (strcmp(object->method, "FirebaseApp#setAutomaticResourceManagementEnabled") == 0) {

  //   // TODO

  // } else if (strcmp(object->method, "FirebaseApp#delete") == 0) {

  //   // TODO

  }

  return Module::OnMessage(object, handle);
}


int AuthModule::SignInAnonymously(std_value *args, FlutterPlatformMessageResponseHandle *handle) {
  auto auth = get_auth(args);
  auto future = auth->SignInAnonymouslyLastResult();
  fprintf(stderr, "SignInAnonymouslyLastResult status=%d error=%d\n",
          future.status(), future.error());
  if (future.status() == firebase::kFutureStatusInvalid ||
      (future.status() == firebase::kFutureStatusComplete &&
        future.error() != firebase::auth::kAuthErrorNone)) {
    future = auth->SignInAnonymously();
  }
  future.OnCompletion([] (
      const firebase::Future<firebase::auth::User*>& result, void* userData) {
    auto handle = static_cast<FlutterPlatformMessageResponseHandle *>(userData);
    if (result.error() == firebase::auth::kAuthErrorNone) {
      success(handle, parseAuthResult(*result.result()));
    } else {
      // TODO: Test failure by disabling anonymous login.
      auto authError = static_cast<firebase::auth::AuthError>(result.error());
      error(handle, std::to_string(authError), result.error_message());
    }
  }, handle);
  return pending();
}
