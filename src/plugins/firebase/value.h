#ifndef _PLUGINS_FIREBASE_VALUE_H
#define _PLUGINS_FIREBASE_VALUE_H

#include <cstring>
#include <memory>
#include <optional>
#include <vector>

extern "C" {
#include "platformchannel.h"
}

// ***********************************************************

class Value {
 public:
  virtual ~Value() {}

  virtual std_value build() = 0;
};

class SimpleValue : public Value {
 public:
  SimpleValue(const std_value &value) : value(value) {}

  virtual std_value build() { return value; }

 private:
  const std_value value;
};

class ValueString : public Value {
 public:
  ValueString(std::string value) : value(value) {}

  std_value build() {
    return std_value{
        .type = kStdString,
        .string_value = const_cast<char *>(value.c_str()),
    };
  }

 private:
  const std::string value;
};

class ValueList : public Value {
 public:
  ValueList() {}

  void add(std::unique_ptr<Value> value) {
    values.push_back(std::move(value));
  }

  std_value build() {
    value_array.clear();
    for (auto& v : values) {
      value_array.push_back(v->build());
    }
    auto result = std_value{
        .type = kStdList,
    };
    result.size = value_array.size();
    result.list = &value_array[0];
    return result;
  }

 private:
  std::vector<std::unique_ptr<Value>> values;
  std::vector<std_value> value_array;
};

class ValueMap : public Value {
 public:
  ValueMap() {}

  void add(std::unique_ptr<Value> key, std::unique_ptr<Value> value) {
    keys.push_back(std::move(key));
    values.push_back(std::move(value));
  }

  std_value build() {
    key_array.clear();
    value_array.clear();
    for (int i = 0; i < keys.size(); ++i) {
      key_array.push_back(keys[i]->build());
      value_array.push_back(values[i]->build());
    }
    auto result = std_value{
        .type = kStdMap,
    };
    result.size = key_array.size();
    result.keys = &key_array[0];
    result.values = &value_array[0];
    return result;
  }

 private:
  std::vector<std::unique_ptr<Value>> keys;
  std::vector<std::unique_ptr<Value>> values;
  std::vector<std_value> key_array;
  std::vector<std_value> value_array;
};

std::unique_ptr<Value> val();
std::unique_ptr<Value> val(bool value);
std::unique_ptr<Value> val(int value);
std::unique_ptr<Value> val(int64_t value);
std::unique_ptr<Value> val(uint64_t value);
std::unique_ptr<Value> val(double value);
std::unique_ptr<Value> val(const char *value);
std::unique_ptr<Value> val(const std::string& value);
std::optional<std::string> get_string(std_value *args, char *key);
std::optional<int> get_int(std_value *args, char *key);
std::optional<bool> get_bool(std_value *args, char *key);
std_value* get_map(std_value* args, char* key);

// ***********************************************************

#endif
