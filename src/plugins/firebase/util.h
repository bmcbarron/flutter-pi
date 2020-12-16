#ifndef _PLUGINS_FIREBASE_UTIL_H
#define _PLUGINS_FIREBASE_UTIL_H

#include <optional>

#include "firebase/variant.h"
#include "value.h"

extern "C" {
long gettid();
}

int stdPrint(std_value* value, int indent = 0);

namespace firebase {
std::unique_ptr<Value> val(const Variant& value);
}

std::optional<firebase::Variant> as_variant(std_value* value);
std::optional<firebase::Variant> get_variant(std_value* args, char* key);

template <typename K, typename V>
static V map_get(const std::map<K, V>& m, const K& k) {
  auto result = m.find(k);
  return result != m.end() ? result->second : V();
}

template <typename K, typename V>
static V map_remove(std::map<K, V>& m, const K& k) {
  auto it = m.find(k);
  if (it == m.end()) {
    return V();
  }
  V result = it->second;
  m.erase(it);
  return result;
}

#endif
