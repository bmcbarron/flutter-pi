#include "util.h"

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "debug.h"

long gettid() {
	return syscall(SYS_gettid);
}

std::unique_ptr<Value> val(const firebase::Variant& value) {
  switch (value.type()) {
    case firebase::Variant::Type::kTypeNull: return val();
    case firebase::Variant::Type::kTypeInt64: return val(value.int64_value());
    case firebase::Variant::Type::kTypeDouble: return val(value.double_value());
    case firebase::Variant::Type::kTypeBool: return val(value.bool_value());
    case firebase::Variant::Type::kTypeStaticString: return val(value.string_value());
    case firebase::Variant::Type::kTypeMutableString: return val(value.string_value());
    case firebase::Variant::Type::kTypeVector: {
      auto result = std::make_unique<ValueList>();
      for (auto const& v : value.vector()) {
        result->add(val(v));
      }
      return result;
    }
    case firebase::Variant::Type::kTypeMap: {
      auto result = std::make_unique<ValueMap>();
      for (auto const& [k, v] : value.map()) {
        result->add(val(k), val(v));
      }
      return result;
    }
  }
  fprintf(stderr, "Error converting Variant type: %d\n", value.type());
  return val();
}

std::optional<firebase::Variant> as_variant(std_value* value) {
  switch (value->type) {
    case kStdNull: return firebase::Variant::Null();
    case kStdInt32: return firebase::Variant::FromInt64(value->int32_value);
    case kStdInt64: return firebase::Variant::FromInt64(value->int64_value);
    case kStdFloat64: return firebase::Variant::FromDouble(value->float64_value);
    case kStdTrue: return firebase::Variant::True();
    case kStdFalse: return firebase::Variant::False();
    case kStdString: return firebase::Variant::MutableStringFromStaticString(value->string_value);
    case kStdMap: {
      auto result = firebase::Variant::EmptyMap();
      for (int i = 0; i < value->size; ++i) {
        auto k = as_variant(&(value->keys[i]));
        auto v = as_variant(&(value->values[i]));
        assert(k && v);
        result.map()[*k] = *v;
      }
      return result; 
    }
  }
  return std::nullopt;
}

std::optional<firebase::Variant> get_variant(std_value* args, char* key) {
  auto result = stdmap_get_str(args, key);
  if (result == nullptr) {
    return std::nullopt;
  }
  return as_variant(result);
}

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t invoke_cond = PTHREAD_COND_INITIALIZER;

int success(FlutterPlatformMessageResponseHandle* handle,
            std::unique_ptr<Value> result) {
  if (!result) {
    result = val();
  }
  auto builtResult = result->build();
  fprintf(stderr, "[%d] <== ", gettid());
  stdPrint(&builtResult);
  fprintf(stderr, "--------------------------------------------------------------------------\n");
  return platch_respond_success_std(handle, &builtResult);
}

int error(FlutterPlatformMessageResponseHandle* handle,
          std::string error_code,
          std::string error_message,
          std::unique_ptr<Value> error_details) {
  if (!error_details) {
    error_details = val();
  }
  auto builtDetails = error_details->build();
  fprintf(stderr, "[%d] <== error: %s %s details ",
          gettid(), error_code.c_str(), error_message.c_str());
  stdPrint(&builtDetails);
  fprintf(stderr, "--------------------------------------------------------------------------\n");
  return platch_respond_error_std(handle, const_cast<char*>(error_code.c_str()),
      const_cast<char*>(error_message.c_str()), &builtDetails);
}

int error_message(FlutterPlatformMessageResponseHandle* handle, const char* msg, ...)
  // Not working: __attribute__((format(printf, 2, 3)))
  {
  char message[256];
  va_list args;
  va_start(args, msg);
  vsnprintf(message, 256, msg, args);
  message[255] = '\0';
  va_end(args);
  return error(handle, "unknown code", message, val("error details"));
}

int not_implemented(FlutterPlatformMessageResponseHandle* handle) {
  fprintf(stderr, "[%d] <== XXXXXXXXXXXXXXXX (not implemented) XXXXXXXXXXXXXXXX\n"
                  "--------------------------------------------------------------------------\n",
                  gettid());
  return platch_respond_not_implemented(handle);
}

int pending() {
  fprintf(stderr, "[%d] <== ???????????????????? (pending) ????????????????????\n"
                  "--------------------------------------------------------------------------\n",
                  gettid());
  return 0;
}

void on_receive(char* channel, platch_obj* object) {
  fprintf(stderr,
          "--------------------------------------------------------------------------\n"
          "[%d] %s\n"
          ">>> %s ",
          gettid(), channel, object->method);
  stdPrint(&(object->std_arg));
}

struct OnInvokeResponseData {
  std::string channel;
  std::string method;
};

int on_invoke_response(platch_obj *object, void *userdata) {
  auto data = std::unique_ptr<OnInvokeResponseData>(static_cast<OnInvokeResponseData*>(userdata));
  fprintf(stderr, "[%d] %s\n==> %s ", gettid(), data->channel.c_str(), data->method.c_str());
  if (object->codec == kNotImplemented) {
    fprintf(stderr, "error: channel not implemented on flutter side\n");
  } else if (object->success) {
    stdPrint(&object->std_result);
  } else {
    fprintf(stderr, "error code: %s\n"
           "error message: %s\n"
           "error details: ", object->error_code,
           (object->error_msg != NULL) ? object->error_msg : "null");
    stdPrint(&object->std_error_details);
  }
  return 0;
}

void invoke(std::string channel, std::string method, std::unique_ptr<Value> arguments) {
  fprintf(stderr, "[%d] %s\n<<< %s ", gettid(), channel.c_str(), method.c_str());
  auto builtArgs = arguments->build();
  stdPrint(&builtArgs);
  platch_call_std(const_cast<char*>(channel.c_str()), const_cast<char*>(method.c_str()),
                  &builtArgs, on_invoke_response, new OnInvokeResponseData {
                    channel: channel,
                    method: method,
                  });
}

struct Invocation {
  bool complete;
  bool cancelled;
  platch_obj result;
};

int on_invoke_sync_response(platch_obj *object, void *userdata) {
  fprintf(stderr, "[%d] (sync) ==> ", gettid());
  if (object->codec == kNotImplemented) {
    fprintf(stderr, "error: channel not implemented on flutter side\n");
  } else if (object->success) {
    stdPrint(&object->std_result);
  } else {
    fprintf(stderr, "error code: %s\n"
           "error message: %s\n"
           "error details: ", object->error_code,
           (object->error_msg != NULL) ? object->error_msg : "null");
    stdPrint(&object->std_error_details);
  }
  Invocation* invocation = static_cast<Invocation*>(userdata);
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
                         timespec* deadline, platch_obj *response) {
  fprintf(stderr, "[%d] (sync) %s\n<<< %s ", gettid(), channel.c_str(), method.c_str());
  auto builtArgs = arguments->build();
  stdPrint(&builtArgs);

  auto invocation = new Invocation{false, false};

  auto ok = platch_call_std(
    const_cast<char*>(channel.c_str()), const_cast<char*>(method.c_str()),
                  &builtArgs,
                  on_invoke_sync_response,
                  invocation);
  if (ok != 0) {
    fprintf(stderr, "invoke_sync error: %d\n", ok);
    return INVOKE_FAILED;
  }

  InvokeResult result = INVOKE_FAILED;
  pthread_mutex_lock(&mutex);
  while (true) {
    auto ok = pthread_cond_timedwait(&invoke_cond, &mutex, deadline);
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

  fprintf(stderr, "[%d] (sync) %s\n<== %s \n", gettid(), channel.c_str(), method.c_str());
  if (response->success) {
    stdPrint(&response->std_result, 4);
  } else {
    fprintf(stderr, "error code: %s\n"
           "error message: %s\n"
           "error details: ", response->error_code,
           (response->error_msg != NULL) ? response->error_msg : "null");
  }
  return result;
}
