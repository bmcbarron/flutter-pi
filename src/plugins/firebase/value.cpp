#include "value.h"

std::unique_ptr<Value> val() {
  return std::unique_ptr<Value>(new SimpleValue({.type = kStdNull}));
}

std::unique_ptr<Value> val(bool value) {
  return std::unique_ptr<Value>(new SimpleValue({.type = value ? kStdTrue : kStdFalse}));
}

std::unique_ptr<Value> val(int value) {
  auto result = std_value{
      .type = kStdInt32,
  };
  result.int32_value = value;
  return std::make_unique<SimpleValue>(result);
}

std::unique_ptr<Value> val(int64_t value) {
  auto result = std_value{
      .type = kStdInt64,
  };
  result.int64_value = value;
  return std::make_unique<SimpleValue>(result);
}

std::unique_ptr<Value> val(uint64_t value) {
  return val(static_cast<int64_t>(value));
}

std::unique_ptr<Value> val(double value) {
  auto result = std_value{
      .type = kStdFloat64,
  };
  result.float64_value = value;
  return std::make_unique<SimpleValue>(result);
}

std::unique_ptr<Value> val(const char *value) {
  return std::make_unique<ValueString>(value);
}

std::unique_ptr<Value> val(const std::string& value) {
  return std::make_unique<ValueString>(value);
}

std::optional<std::string> get_string(std_value *args, char *key) {
	auto result = stdmap_get_str(args, key);
  if (result != nullptr && result->type == kStdString) {
    return result->string_value;
  }
  return std::nullopt;
}

std::optional<int> get_int(std_value *args, char *key) {
	auto result = stdmap_get_str(args, key);
  if (result != nullptr && result->type == kStdInt32) {
    return result->int32_value;
  }
  return std::nullopt;
}

std::optional<bool> get_bool(std_value *args, char *key) {
	auto result = stdmap_get_str(args, key);
  if (result != nullptr && result->type == kStdTrue) {
    return true;
  }
  if (result != nullptr && result->type == kStdFalse) {
    return false;
  }
  return std::nullopt;
}

std_value* get_map(std_value* args, char* key) {
	auto result = stdmap_get_str(args, key);
  return result != nullptr && result->type == kStdMap ? result : nullptr;
}
