#include "value.h"

std::unique_ptr<Value> val() {
  auto result = std_value{.type = kStdNull};
  return std::make_unique<SimpleValue>(result);
}

std::unique_ptr<Value> val(bool value) {
  auto result = std_value{.type = value ? kStdTrue : kStdFalse};
  return std::make_unique<SimpleValue>(result);
}

std::unique_ptr<Value> val(int32_t value) {
  auto result = std_value{.type = kStdInt32};
  result.int32_value = value;
  return std::make_unique<SimpleValue>(result);
}

std::unique_ptr<Value> val(uint32_t value) { return val(static_cast<int32_t>(value)); }

std::unique_ptr<Value> val(int64_t value) {
  auto result = std_value{.type = kStdInt64};
  result.int64_value = value;
  return std::make_unique<SimpleValue>(result);
}

std::unique_ptr<Value> val(uint64_t value) { return val(static_cast<int64_t>(value)); }

std::unique_ptr<Value> val(double value) {
  auto result = std_value{.type = kStdFloat64};
  result.float64_value = value;
  return std::make_unique<SimpleValue>(result);
}

std::unique_ptr<Value> val(const char *value) { return std::make_unique<ValueString>(value); }

std::unique_ptr<Value> val(const std::string &value, bool allow_empty) {
  if (!allow_empty && value.empty()) {
    return val();
  }
  return std::make_unique<ValueString>(value);
}

std::optional<std::string> as_string(std_value *value) {
  if (value != nullptr && value->type == kStdString) {
    return value->string_value;
  }
  return std::nullopt;
}

std::optional<std::string> get_string(std_value *args, char *key) {
  return as_string(stdmap_get_str(args, key));
}

std::optional<int32_t> get_int(std_value *args, char *key) {
  auto result = stdmap_get_str(args, key);
  if (result != nullptr && result->type == kStdInt32) {
    return result->int32_value;
  }
  return std::nullopt;
}

std::optional<int64_t> get_int64(std_value *args, char *key) {
  auto result = stdmap_get_str(args, key);
  if (result != nullptr && result->type == kStdInt64) {
    return result->int64_value;
  }
  return std::nullopt;
}

std::optional<bool> as_bool(std_value *value) {
  if (value != nullptr && value->type == kStdTrue) {
    return true;
  }
  if (value != nullptr && value->type == kStdFalse) {
    return false;
  }
  return std::nullopt;
}

std::optional<bool> get_bool(std_value *args, char *key) {
  return as_bool(stdmap_get_str(args, key));
}

std_value *get_map(std_value *args, char *key) {
  auto result = stdmap_get_str(args, key);
  return result != nullptr && result->type == kStdMap ? result : nullptr;
}

std_value *get_list(std_value *args, char *key) {
  auto result = stdmap_get_str(args, key);
  return result != nullptr && result->type == kStdList ? result : nullptr;
}
