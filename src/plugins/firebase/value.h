#ifndef _PLUGINS_FIREBASE_VALUE_H
#define _PLUGINS_FIREBASE_VALUE_H

#include <cassert>
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

std::unique_ptr<Value> val();
std::unique_ptr<Value> val(bool value);
std::unique_ptr<Value> val(int32_t value);
std::unique_ptr<Value> val(uint32_t value);
std::unique_ptr<Value> val(int64_t value);
std::unique_ptr<Value> val(uint64_t value);
std::unique_ptr<Value> val(double value);
std::unique_ptr<Value> val(const char *value);
std::unique_ptr<Value> val(const std::string &value, bool allow_empty = true);

// std::unique_ptr<Value> val(std::unique_ptr<Value>& value) { return std::move(value); }

std::optional<std::string> as_string(std_value *value);
std::optional<std::string> get_string(std_value *args, char *key);
std::optional<int32_t> get_int(std_value *args, char *key);
std::optional<int64_t> get_int64(std_value *args, char *key);
std::optional<bool> as_bool(std_value *value);
std::optional<bool> get_bool(std_value *args, char *key);
std_value *get_map(std_value *args, char *key);
std_value *get_list(std_value *args, char *key);

class ValueList : public Value {
public:
  ValueList() {}

  void add(std::unique_ptr<Value> value) { values.push_back(std::move(value)); }

  std_value build() {
    value_array.clear();
    for (auto &v : values) {
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

template <typename V> std::unique_ptr<Value> val(const std::vector<V> &value) {
  auto result = std::make_unique<ValueList>();
  for (const auto &v : value) {
    result->add(val(v));
  }
  return std::move(result);
}

class ValueMap : public Value {
public:
  ValueMap() {}

  void add(std::unique_ptr<Value> key, std::unique_ptr<Value> value) {
    keys.push_back(std::move(key));
    values.push_back(std::move(value));
  }

  template <typename V>
  std::enable_if_t<!std::is_convertible_v<V, std::unique_ptr<Value>>> set(std::string key,
                                                                          V value) {
    add(val(key), val(value));
    // auto vkey = val(key);
    // auto vvalue = val(value);
    // add(vkey, vvalue);
  }

  template <typename V> void set(std::string key, std::unique_ptr<V> value) {
    add(val(key), std::move(value));
    // auto vkey = val(key);
    // // auto vvalue = std::unique_ptr<Value>(value.release());
    // add(vkey, value);
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

class ValuePreEncoded : public Value {
public:
  ValuePreEncoded(uint8_t type, size_t size) : data(size), buffer(&data[0]), remaining(size) {
    auto ok = _write8(&buffer, type, &remaining);
    assert(ok == 0);
  }

  std_value build() {
    assert(remaining == 0);
    auto result = std_value{
        .type = kStdPreEncoded,
    };
    result.size = data.size();
    result.uint8array = static_cast<uint8_t *>(malloc(data.size()));
    memcpy(result.uint8array, &data[0], data.size());
    return result;
  }

  std::vector<uint8_t> data;
  uint8_t *buffer;
  size_t remaining;
};

// ***********************************************************

#endif
