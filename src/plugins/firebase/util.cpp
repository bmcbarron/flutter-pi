#include "util.h"

#include <inttypes.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cassert>

long gettid() { return syscall(SYS_gettid); }

#define INDENT_STRING "                    "

int __stdPrint(std_value *value, int indent) {
  switch (value->type) {
  case kStdPreEncoded:
    fprintf(stderr, "encoded(type=%d,size=%d)", value->uint8array[0], value->size);
    break;
  case kStdNull:
    fprintf(stderr, "null");
    break;
  case kStdTrue:
    fprintf(stderr, "true");
    break;
  case kStdFalse:
    fprintf(stderr, "false");
    break;
  case kStdInt32:
    fprintf(stderr, "%" PRIi32, value->int32_value);
    break;
  case kStdInt64:
    fprintf(stderr, "%" PRIi64, value->int64_value);
    break;
  case kStdFloat64:
    fprintf(stderr, "%lf", value->float64_value);
    break;
  case kStdString:
  case kStdLargeInt:
    fprintf(stderr, "\"%s\"", value->string_value);
    break;
  case kStdUInt8Array:
    fprintf(stderr, "(uint8_t) [");
    for (int i = 0; i < value->size; i++) {
      fprintf(stderr, "0x%02X", value->uint8array[i]);
      if (i + 1 != value->size)
        fprintf(stderr, ", ");
    }
    fprintf(stderr, "]");
    break;
  case kStdInt32Array:
    fprintf(stderr, "(int32_t) [");
    for (int i = 0; i < value->size; i++) {
      fprintf(stderr, "%" PRIi32, value->int32array[i]);
      if (i + 1 != value->size)
        fprintf(stderr, ", ");
    }
    fprintf(stderr, "]");
    break;
  case kStdInt64Array:
    fprintf(stderr, "(int64_t) [");
    for (int i = 0; i < value->size; i++) {
      fprintf(stderr, "%" PRIi64, value->int64array[i]);
      if (i + 1 != value->size)
        fprintf(stderr, ", ");
    }
    fprintf(stderr, "]");
    break;
  case kStdFloat64Array:
    fprintf(stderr, "(double) [");
    for (int i = 0; i < value->size; i++) {
      fprintf(stderr, "%f", value->float64array[i]);
      if (i + 1 != value->size)
        fprintf(stderr, ", ");
    }
    fprintf(stderr, "]");
    break;
  case kStdList:
    if (value->size == 0) {
      fprintf(stderr, "[]");
    } else {
      fprintf(stderr, "[\n");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%.*s", indent + 2, INDENT_STRING);
        __stdPrint(&(value->list[i]), indent + 2);
        if (i + 1 != value->size)
          fprintf(stderr, ",\n");
      }
      fprintf(stderr, "\n%.*s]", indent, INDENT_STRING);
    }
    break;
  case kStdMap:
    if (value->size == 0) {
      fprintf(stderr, "{}");
    } else {
      fprintf(stderr, "{\n");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%.*s", indent + 2, INDENT_STRING);
        __stdPrint(&(value->keys[i]), indent + 2);
        fprintf(stderr, ": ");
        __stdPrint(&(value->values[i]), indent + 2);
        if (i + 1 != value->size)
          fprintf(stderr, ",\n");
      }
      fprintf(stderr, "\n%.*s}", indent, INDENT_STRING);
    }
    break;
  default:
    break;
  }
  return 0;
}

int stdPrint(std_value *value, int indent) {
  fprintf(stderr, "%.*s", indent, INDENT_STRING);
  __stdPrint(value, indent);
  fprintf(stderr, "\n");
  return 0;
}

namespace firebase {
using ::val;

std::unique_ptr<Value> val(const Variant &value) {
  switch (value.type()) {
  case Variant::Type::kTypeNull:
    return val();
  case Variant::Type::kTypeInt64:
    return val(value.int64_value());
  case Variant::Type::kTypeDouble:
    return val(value.double_value());
  case Variant::Type::kTypeBool:
    return val(value.bool_value());
  case Variant::Type::kTypeStaticString:
    return val(value.string_value());
  case Variant::Type::kTypeMutableString:
    return val(value.string_value());
  case Variant::Type::kTypeVector: {
    auto result = std::make_unique<ValueList>();
    for (auto const &v : value.vector()) {
      result->add(val(v));
    }
    return result;
  }
  case Variant::Type::kTypeMap: {
    auto result = std::make_unique<ValueMap>();
    for (auto const &[k, v] : value.map()) {
      result->add(val(k), val(v));
      // auto vk = val(k);
      // auto vv = val(v);
      // result->add(vk, vv);
    }
    return result;
  }
  }
  fprintf(stderr, "Error converting Variant type: %d\n", value.type());
  return val();
}

} // namespace firebase

std::optional<firebase::Variant> as_variant(std_value *value) {
  switch (value->type) {
  case kStdNull:
    return firebase::Variant::Null();
  case kStdInt32:
    return firebase::Variant::FromInt64(value->int32_value);
  case kStdInt64:
    return firebase::Variant::FromInt64(value->int64_value);
  case kStdFloat64:
    return firebase::Variant::FromDouble(value->float64_value);
  case kStdTrue:
    return firebase::Variant::True();
  case kStdFalse:
    return firebase::Variant::False();
  case kStdString:
    return firebase::Variant::MutableStringFromStaticString(value->string_value);
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

std::optional<firebase::Variant> get_variant(std_value *args, char *key) {
  auto result = stdmap_get_str(args, key);
  if (result == nullptr) {
    return std::nullopt;
  }
  return as_variant(result);
}
