#ifndef _PLUGINS_FIREBASE_MODULE_H
#define _PLUGINS_FIREBASE_MODULE_H

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <type_traits>

extern "C" {
#include "platformchannel.h"
}

#include "firebase/future.h"
#include "value.h"

enum InvokeResult {
  INVOKE_RESULT,
  INVOKE_TIMEDOUT,
  INVOKE_FAILED,
};

void invoke(std::string channel, std::string method,
            std::unique_ptr<Value> arguments, bool print_response = false);
InvokeResult invoke_sync(std::string channel, std::string method,
                         std::unique_ptr<Value> arguments, timespec* deadline,
                         platch_obj* response);

void on_receive(char* channel, platch_obj* object);
int pending();
int success(FlutterPlatformMessageResponseHandle *handle,
            std::unique_ptr<Value> result = std::unique_ptr<Value>(), bool async = false);
int error_message(FlutterPlatformMessageResponseHandle* handle, const char* msg,
                  ...);
int error(FlutterPlatformMessageResponseHandle *handle, std::string error_code,
          std::string error_message,
          std::unique_ptr<ValueMap> error_details = std::unique_ptr<ValueMap>(),
          bool async = false);
int not_implemented(FlutterPlatformMessageResponseHandle* handle);

template <typename R>
using Wrapper = std::unique_ptr<Value>(*)(R);

template <typename R>
class FutureCompleter {
 public:
  FutureCompleter(FlutterPlatformMessageResponseHandle* handle, Wrapper<const R*> wrap)
      : handle(handle), wrap(wrap) {}

  void OnComplete(const firebase::Future<R>& result) {
    if (result.error() != 0) {
      error(handle, std::to_string(result.error()), result.error_message(),
            std::unique_ptr<ValueMap>(), true);
    } else if constexpr (std::is_same<void, R>::value) {
      success(handle, val(), true);
    } else if (wrap == nullptr) {
      // } else if constexpr (std::is_invocable<W, R>::value) {
      success(handle, val(result.result()), true);
    } else {
      success(handle, wrap(result.result()), true);
    }
  }

 private:
  FlutterPlatformMessageResponseHandle* handle;
  Wrapper<const R*> wrap;
};

class Module {
 public:
  const std::string channel;

  Module(std::string channel);
  virtual ~Module() {}

  virtual int OnMessage(platch_obj* object,
                        FlutterPlatformMessageResponseHandle* handle);

 protected:
  template <typename M>
  void Register(
      std::string method,
      int (M::*handler)(std_value* args,
                        FlutterPlatformMessageResponseHandle* handle)) {
    static_assert(std::is_base_of_v<Module, M>, "M not dervied from Module");
    handlers[method] = static_cast<Handler>(handler);
  }

  template <typename R>
  int Await(const firebase::Future<R>& future,
            FlutterPlatformMessageResponseHandle* handle, Wrapper<const R*> wrap = nullptr) {
    // } else {
    //   //assert(wrap == nullptr);
    // }
    auto params = std::make_unique<FutureCompleter<R>>(handle, wrap);
    future.OnCompletion(
        [](const firebase::Future<R>& result, void* userData) {
          auto completer = std::unique_ptr<FutureCompleter<R>>(
              static_cast<FutureCompleter<R>*>(userData));
          completer->OnComplete(result);
        },
        params.release());
    return pending();
  }

 private:
  typedef int (Module::*Handler)(std_value* args,
                                 FlutterPlatformMessageResponseHandle* handle);
  std::map<std::string, Handler> handlers;
};

#endif
