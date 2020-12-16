#include "auth.h"

#include "core.h"
#include "firebase/auth.h"
#include "util.h"

namespace firebase {
namespace auth {
using ::val;

std::unique_ptr<Value> val(const User* user) {
  if (user == nullptr) {
    return val();
  }

  auto output = std::make_unique<ValueMap>();
  auto metadata = std::make_unique<ValueMap>();

  output->set("displayName", user->display_name());
  output->set("email", user->email());
  output->set("emailVerified", user->is_email_verified());
  output->set("isAnonymous", user->is_anonymous());

  metadata->set("creationTime", user->metadata().creation_timestamp);
  metadata->set("lastSignInTime", user->metadata().last_sign_in_timestamp);

  output->add(val("metadata"), std::move(metadata));
  output->set("phoneNumber", user->phone_number());
  output->add(val("photoURL"), val(user->photo_url(), false));
  output->add(val("providerData"), val(user->provider_data()));
  output->set("refreshToken", "");
  output->set("uid", user->uid());

  return std::move(output);
}

std::unique_ptr<Value> val(UserInfoInterface* userInfo) {
  auto output = std::make_unique<ValueMap>();

  output->set("displayName", userInfo->display_name());
  output->set("email", userInfo->email());
  output->set("phoneNumber", userInfo->phone_number());
  output->set("photoURL", val(userInfo->photo_url(), false));
  output->set("providerId", userInfo->provider_id());
  output->set("uid", userInfo->uid());

  return std::move(output);
}

std::unique_ptr<Value> val(
    const std::vector<UserInfoInterface*>& userInfoList) {
  auto output = std::make_unique<ValueList>();
  for (auto userInfo : userInfoList) {
    // https://firebase.google.com/docs/reference/android/com/google/firebase/auth/FirebaseAuthProvider#PROVIDER_ID
    if ("firebase" != userInfo->provider_id()) {
      output->add(val(userInfo));
    }
  }
  return std::move(output);
}

}  // namespace auth
}  // namespace firebase

firebase::auth::Auth* get_auth(std_value* args) {
  auto app = get_app(args);
  return app ? firebase::auth::Auth::GetAuth(app) : nullptr;
}

class AuthStateListenerImpl : public firebase::auth::AuthStateListener {
 public:
  AuthStateListenerImpl(std::string channel, std::string appName)
      : channel(channel), appName(appName) {}

  virtual void OnAuthStateChanged(firebase::auth::Auth* auth) {
    auto event = std::make_unique<ValueMap>();
    event->set("appName", appName);

    auto user = auth->current_user();
    event->add(val("user"), val(user));

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
static std::map<int, firebase::auth::PhoneAuthProvider::ForceResendingToken*>
    forceResendingTokens;

AuthModule::AuthModule() : Module("plugins.flutter.io/firebase_auth") {
  Register("Auth#signInAnonymously", &AuthModule::SignInAnonymously);
}

int AuthModule::OnMessage(platch_obj* object,
                          FlutterPlatformMessageResponseHandle* handle) {
  auto* args = &(object->std_arg);
  if (args->type != kStdMap) {
    return error_message(handle, "arguments isn't a map");
  }

  if (strcmp(object->method, "Auth#registerChangeListeners") == 0) {
    auto appName = get_string(args, "appName");
    auto app = get_app(args);
    if (app == nullptr) {
      return error_message(handle, "App (%s) not initialized.",
                           appName.value_or("<default>").c_str());
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

    //   // The default app values are read from google-services.json in the
    //   current working directory. auto defaultApp = firebase::App::Create();
    //   if (defaultApp == nullptr) {
    //     return error(handle, "Failed to initialize Firebase.");
    //   }

    //   std::vector<firebase::App *> apps;
    //   apps.push_back(defaultApp);

    //   auto result = std::make_unique<ValueList>();
    //   for (auto &app : apps) {
    //     auto appMap = std::make_unique<ValueMap>();
    //     if (strcmp(firebase::kDefaultAppName, app->name()) == 0) {
    //       // Flutter plugin and desktop API have different constants for the
    //       default. appMap->set("name", "[DEFAULT]");
    //     } else {
    //       appMap->set("name", app->name());
    //     }
    //     //appMap->set("isAutomaticDataCollectionEnabled",
    //     app->IsDataCollectionDefaultEnabled());
    //     //appMap->set("pluginConstants", );
    //     auto options = std::make_unique<ValueMap>();
    //     options->set("apiKey", app->options().api_key());
    //     options->set("appId", app->options().app_id());
    //     options->set("messagingSenderId",
    //     app->options().messaging_sender_id()); options->set("projectId",
    //     app->options().project_id()); options->set("databaseURL",
    //     app->options().database_url()); options->set("storageBucket",
    //     app->options().storage_bucket()); options->set("trackingId",
    //     app->options().ga_tracking_id()); appMap->add(val("options"),
    //     std::move(options)); result->add(std::move(appMap));
    //   }
    //   return success(handle, std::move(result));

    // } else if (strcmp(object->method,
    // "FirebaseApp#setAutomaticDataCollectionEnabled") == 0) {

    //   // TODO

    // } else if (strcmp(object->method,
    // "FirebaseApp#setAutomaticResourceManagementEnabled") == 0) {

    //   // TODO

    // } else if (strcmp(object->method, "FirebaseApp#delete") == 0) {

    //   // TODO
  }

  return Module::OnMessage(object, handle);
}

std::unique_ptr<Value> signInAnonymouslyResult(firebase::auth::User* const * user) {
  if (user == nullptr) {
    return val();
  }

  auto output = std::make_unique<ValueMap>();

  output->set("additionalUserInfo", val());
  output->set("authCredential", val());
  output->add(val("user"), val(*user));

  return std::move(output);
}

int AuthModule::SignInAnonymously(
    std_value* args, FlutterPlatformMessageResponseHandle* handle) {
  auto auth = get_auth(args);
  if (auth == nullptr) {
    return error_message(handle, "Unknown app.");
  }
  auto future = auth->SignInAnonymouslyLastResult();
  // fprintf(stderr, "SignInAnonymouslyLastResult status=%d error=%d\n",
  //         future.status(), future.error());
  if (future.status() == firebase::kFutureStatusInvalid ||
      (future.status() == firebase::kFutureStatusComplete &&
       future.error() != firebase::auth::kAuthErrorNone)) {
    future = auth->SignInAnonymously();
  }
  return Await(future, handle, signInAnonymouslyResult);
}
