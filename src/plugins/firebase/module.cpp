#include "module.h"

#include <stdarg.h>
#include <stdio.h>

#include <functional>

extern "C" {
#include "plugins/firebase.h"
}

#include "util.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t invoke_cond = PTHREAD_COND_INITIALIZER;

#define kCommandBreak "==========================================================================\n"
#define kAsyncCommandBreak                                                                         \
  "--------------------------------------------------------------------------\n"

int success(FlutterPlatformMessageResponseHandle *handle, std::unique_ptr<Value> result,
            bool async) {
  if (!result) {
    result = val();
  }
  auto builtResult = result->build();
  if (async)
    fprintf(stderr, kAsyncCommandBreak);
  fprintf(stderr, "[%d] %s<== ", gettid(), async ? "(async) " : "");
  stdPrint(&builtResult);
  fprintf(stderr, async ? kAsyncCommandBreak : kCommandBreak);
  return platch_respond_success_std(handle, &builtResult);
}

int error(FlutterPlatformMessageResponseHandle *handle, std::string error_code,
          std::string error_message, std::unique_ptr<ValueMap> error_details, bool async) {
  if (!error_details) {
    error_details = std::make_unique<ValueMap>();
    error_details->set("code", error_code);
    error_details->set("message", error_message);
  }
  auto builtDetails = error_details->build();
  if (async)
    fprintf(stderr, kAsyncCommandBreak);
  fprintf(stderr, "[%d] %s<== error: %s %s details ", gettid(), error_code.c_str(),
          error_message.c_str(), async ? "(async) " : "");
  stdPrint(&builtDetails);
  fprintf(stderr, async ? kAsyncCommandBreak : kCommandBreak);
  return platch_respond_error_std(handle, const_cast<char *>(error_code.c_str()),
                                  const_cast<char *>(error_message.c_str()), &builtDetails);
}

int error_message(FlutterPlatformMessageResponseHandle *handle, const char *msg, ...)
// Not working: __attribute__((format(printf, 2, 3)))
{
  char message[256];
  va_list args;
  va_start(args, msg);
  vsnprintf(message, 256, msg, args);
  va_end(args);
  message[255] = '\0';
  return error(handle, "unknown code", message);
}

int not_implemented(FlutterPlatformMessageResponseHandle *handle) {
  fprintf(stderr, "[%d] <== XXXXXXXXXXXXXXXX (not implemented) XXXXXXXXXXXXXXXX\n" kCommandBreak,
          gettid());
  return platch_respond_not_implemented(handle);
}

int pending() {
  if (info)
    fprintf(stderr, "[%d] <== ???????????????????? (pending) ????????????????????\n" kCommandBreak,
            gettid());
  return 0;
}

void on_receive(char *channel, platch_obj *object) {
  fprintf(stderr,
          kCommandBreak "[%d] %s\n"
                        ">>> %s ",
          gettid(), channel, object->method);
  stdPrint(&(object->std_arg));
}

struct OnInvokeResponseData {
  std::string channel;
  std::string method;
};

int on_invoke_response(platch_obj *object, void *userdata) {
  auto data = std::unique_ptr<OnInvokeResponseData>(static_cast<OnInvokeResponseData *>(userdata));
  fprintf(stderr, "[%d] %s\n==> %s ", gettid(), data->channel.c_str(), data->method.c_str());
  if (object->codec == kNotImplemented) {
    fprintf(stderr, "error: channel not implemented on flutter side\n");
  } else if (object->success) {
    stdPrint(&object->std_result);
  } else {
    fprintf(stderr,
            "error code: %s\n"
            "error message: %s\n"
            "error details: ",
            object->error_code, (object->error_msg != NULL) ? object->error_msg : "null");
    stdPrint(&object->std_error_details);
  }
  return 0;
}

void invoke(std::string channel, std::string method, std::unique_ptr<Value> arguments,
            bool print_response) {
  fprintf(stderr, kAsyncCommandBreak "[%d] %s\n<<< %s ", gettid(), channel.c_str(), method.c_str());
  auto builtArgs = arguments->build();
  stdPrint(&builtArgs);
  platch_msg_resp_callback callback = nullptr;
  void *userdata = nullptr;
  if (print_response) {
    callback = on_invoke_response;
    userdata = new OnInvokeResponseData{
      channel : channel,
      method : method,
    };
  }
  if (verbose)

    fprintf(stderr, "before invoke platch\n");
  platch_call_std(const_cast<char *>(channel.c_str()), const_cast<char *>(method.c_str()),
                  &builtArgs, callback, userdata, on_decode_firestore_type_std);
  if (verbose)

    fprintf(stderr, "after invoke platch\n");
  fprintf(stderr, kAsyncCommandBreak);
}

struct Invocation {
  bool complete;
  bool cancelled;
  platch_obj result;
};

int on_invoke_sync_response(platch_obj *object, void *userdata) {
  fprintf(stderr, kAsyncCommandBreak "[%d] (unblock) ==> ", gettid());
  if (object->codec == kNotImplemented) {
    fprintf(stderr, "error: channel not implemented on flutter side\n");
  } else if (object->success) {
    stdPrint(&object->std_result);
  } else {
    fprintf(stderr,
            "error code: %s\n"
            "error message: %s\n"
            "error details: ",
            object->error_code, (object->error_msg != NULL) ? object->error_msg : "null");
    stdPrint(&object->std_error_details);
  }
  fprintf(stderr, kAsyncCommandBreak);
  Invocation *invocation = static_cast<Invocation *>(userdata);
  pthread_mutex_lock(&mutex);
  if (!invocation->cancelled) {
    invocation->result = *object;
    invocation->complete = true;
    invocation = nullptr;
  }
  pthread_mutex_unlock(&mutex);
  if (invocation) {
    delete invocation;
    return 0;
  }
  pthread_cond_broadcast(&invoke_cond);
  return 202;
}

InvokeResult invoke_sync(std::string channel, std::string method, std::unique_ptr<Value> arguments,
                         timespec *deadline, platch_obj *response) {
  fprintf(stderr, kAsyncCommandBreak "[%d] (blocking) %s\n<<< %s ", gettid(), channel.c_str(),
          method.c_str());
  auto builtArgs = arguments->build();
  stdPrint(&builtArgs);

  auto invocation = new Invocation{false, false};

  auto ok = platch_call_std(const_cast<char *>(channel.c_str()), const_cast<char *>(method.c_str()),
                            &builtArgs, on_invoke_sync_response, invocation,
                            on_decode_firestore_type_std);
  if (ok != 0) {
    fprintf(stderr, "invoke_sync error: %d\n", ok);
    fprintf(stderr, kAsyncCommandBreak);
    return INVOKE_FAILED;
  }
  fprintf(stderr, kAsyncCommandBreak);

  InvokeResult result = INVOKE_FAILED;
  pthread_mutex_lock(&mutex);
  while (true) {
    ok = pthread_cond_timedwait(&invoke_cond, &mutex, deadline);
    if (invocation->complete) {
      *response = invocation->result;
      result = INVOKE_RESULT;
      break;
    }
    if (ok == 0) {
      continue;
    }
    invocation->cancelled = true;
    invocation = nullptr;
    result = (ok == ETIMEDOUT) ? INVOKE_TIMEDOUT : INVOKE_FAILED;
    break;
  }
  pthread_mutex_unlock(&mutex);
  delete invocation;

  fprintf(stderr, kAsyncCommandBreak "[%d] (blocking) %s\n==> %s\n", gettid(), channel.c_str(),
          method.c_str());
  if (result == INVOKE_TIMEDOUT) {
    fprintf(stderr, "  INVOKE_TIMEDOUT\n");
  } else if (result == INVOKE_FAILED) {
    fprintf(stderr, "  INVOKE_FAILED: %d\n", ok);
  } else if (response->success) {
    stdPrint(&response->std_result, 2);
  } else {
    fprintf(stderr,
            "error code: %s\n"
            "error message: %s\n"
            "error details: ",
            response->error_code, (response->error_msg != NULL) ? response->error_msg : "null");
  }
  fprintf(stderr, kAsyncCommandBreak);
  return result;
}

Module::Module(std::string channel) : channel(channel) {}

int Module::OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle) {
  auto h = handlers.find(object->method);
  if (h == handlers.end()) {
    return not_implemented(handle);
  }
  auto *args = &(object->std_arg);
  if (args->type != kStdMap && args->type != kStdNull) {
    return error_message(handle, "arguments isn't a map");
  }
  return std::invoke(h->second, this, args, handle);
}
