#include "database.h"

#include "debug.h"
#include "core.h"
#include "util.h"

firebase::database::DatabaseReference get_reference(firebase::database::Database* database,
                                                   std_value* args) {
  auto path = get_string(args, "path");
  auto result = database->GetReference();
  if (path) {
    result = result.Child(*path);
  }
  return result;
}

firebase::database::Query get_query(firebase::database::Database* database, std_value* args) {
  firebase::database::Query query = get_reference(database, args);
  auto parameters = get_map(args, "parameters");
  if (parameters == nullptr) {
    return query;
  }
  auto order_by = get_string(parameters, "orderBy");
  if (order_by) {
    if (*order_by == "child") {
      auto order_by_child_key = get_string(parameters, "orderByChildKey");
      if (order_by_child_key) {
        query = query.OrderByChild(order_by_child_key->c_str());
      }
    } else if (*order_by == "key") {
      query = query.OrderByKey();
    } else if (*order_by == "value") {
      query = query.OrderByValue();
    } else if (*order_by == "priority") {
      query = query.OrderByPriority();
    }
  }
  auto start_at = get_variant(parameters, "startAt");
  if (start_at) {
    auto start_at_key = get_string(parameters, "startAtKey");
    if (start_at_key) {
      query = query.StartAt(*start_at, start_at_key->c_str());
    } else {
      query = query.StartAt(*start_at);
    }
  }
  auto end_at = get_variant(parameters, "endAt");
  if (end_at) {
    auto end_at_key = get_string(parameters, "endAtKey");
    if (end_at_key) {
      query = query.EndAt(*end_at, end_at_key->c_str());
    } else {
      query = query.EndAt(*end_at);
    }
  }  
  auto equal_to = get_variant(parameters, "equalTo");
  if (equal_to) {
    auto equal_to_key = get_string(parameters, "equalToKey");
    if (equal_to_key) {
      query = query.EqualTo(*equal_to, equal_to_key->c_str());
    } else {
      query = query.EqualTo(*equal_to);
    }
  }
  auto limit_to_first = get_int(parameters, "limitToFirst");
  if (limit_to_first) {
    query = query.LimitToFirst(*limit_to_first);
  }
  auto limit_to_last = get_int(parameters, "limitToLast");
  if (limit_to_last) {
    query = query.LimitToLast(*limit_to_last);
  }
  return query;
}

class SnapshotListener {
public:
  SnapshotListener(std::string channel, std::string eventType, int id): channel(channel), eventType(eventType), id(id) {}

  void sendEvent(std::string eventType, const firebase::database::DataSnapshot& snapshot,
                 std::optional<std::string> previousSiblingKey = std::nullopt) {
    if (eventType != this->eventType) {
      return;
    }
    auto arguments = std::make_unique<ValueMap>();
    auto snapshotMap = std::make_unique<ValueMap>();
    snapshotMap->add(val("key"), val(snapshot.key()));
    snapshotMap->add(val("value"), val(snapshot.value()));
    arguments->add(val("handle"), val(id));
    arguments->add(val("snapshot"), std::move(snapshotMap));
    if (previousSiblingKey) {
      arguments->add(val("previousSiblingKey"), val(*previousSiblingKey));
    }
    auto value = val(snapshot.value());
    auto convertedValue = value->build();
    invoke(channel, "Event", std::move(arguments));
  }

  void cancel(const firebase::database::Error& error) {
    auto arguments = std::make_unique<ValueMap>();
    arguments->add(val("handle"), val(id));
    arguments->add(val("error"), val(error));
    invoke(channel, "Error", std::move(arguments));
  }

private:
  const std::string channel;
  const std::string eventType;
  const int id;
};

class ValueListenerImpl : public firebase::database::ValueListener {
public:
  ValueListenerImpl(std::string channel, std::string eventType, int id)
      : listener(channel, eventType, id) {}

  virtual void OnValueChanged(const firebase::database::DataSnapshot& snapshot) {
    listener.sendEvent(EVENT_TYPE_VALUE, snapshot);
  }

  virtual void OnCancelled(const firebase::database::Error& error, const char* error_message) {
    listener.cancel(error);
  }

private:
  SnapshotListener listener;
};

class ChildListenerImpl : public firebase::database::ChildListener {
public:
  ChildListenerImpl(std::string channel, std::string eventType, int id)
      : listener(channel, eventType, id) {}

  virtual void OnChildAdded(const firebase::database::DataSnapshot& snapshot,
                            const char* previousSiblingKey) {
    listener.sendEvent(EVENT_TYPE_CHILD_ADDED, snapshot, previousSiblingKey);
  }

  virtual void OnChildChanged(const firebase::database::DataSnapshot& snapshot,
                              const char* previousSiblingKey) {
    listener.sendEvent(EVENT_TYPE_CHILD_CHANGED, snapshot, previousSiblingKey);
  }

  virtual void OnChildMoved(const firebase::database::DataSnapshot& snapshot,
                            const char* previousSiblingKey) {
    listener.sendEvent(EVENT_TYPE_CHILD_MOVED, snapshot, previousSiblingKey);
  }

  virtual void OnChildRemoved(const firebase::database::DataSnapshot& snapshot) {
    listener.sendEvent(EVENT_TYPE_CHILD_REMOVED, snapshot);
  }

  virtual void OnCancelled(const firebase::database::Error& error, const char* error_message) {
    listener.cancel(error);
  }

private:
  SnapshotListener listener;
};

int next_listener_id = 0;
auto value_listeners = std::map<int, ValueListenerImpl*>();
auto child_listeners = std::map<int, ChildListenerImpl*>();

class TransactionHandler {
public:
  TransactionHandler(std::string channel,
      FlutterPlatformMessageResponseHandle *handle, firebase::Variant key, int timeout)
      : channel(channel), handle(handle), key(key) {
    auto ok = clock_gettime(CLOCK_REALTIME, &deadline);
    assert(ok == 0);
    deadline.tv_sec += timeout / 1000;
    deadline.tv_nsec += (timeout % 1000) * 1000000;
  }

  firebase::database::TransactionResult OnCallback(firebase::database::MutableData* data) {
    fprintf(stderr, "[%d] TransactionHandler.OnCallback(%s)\n", gettid(), data->key());
    auto transaction = std::make_unique<ValueMap>();
    transaction->add(val("transactionKey"), val(key));
    auto snapshot = std::make_unique<ValueMap>();
    snapshot->add(val("key"), val(data->key()));
    snapshot->add(val("value"), val(data->value()));
    transaction->add(val("snapshot"), std::move(snapshot));

    platch_obj value;
    auto transactionResult = firebase::database::kTransactionResultAbort;
    auto result = invoke_sync(channel.c_str(), "DoTransaction", std::move(transaction), &deadline, &value);
    if (result == INVOKE_RESULT) {
      if (value.success) {
        auto v = get_variant(&value.std_result, "value");
        if (!v) {
          fprintf(stderr, "[%d] TransactionHandler.OnCallback can't parse result type=%d\n",
            gettid(), value.std_result.type);
        } else {
          data->set_value(*v);
          fprintf(stderr, "setting type %s == %s\n", firebase::Variant::TypeName(v->type()),
          firebase::Variant::TypeName(data->value().type()));
          transactionResult = firebase::database::kTransactionResultSuccess;
        }
      }
      platch_free_obj(&value);
    }
    return transactionResult;
  }

private:
  const std::string channel;
  const FlutterPlatformMessageResponseHandle *handle;
  const firebase::Variant key;
  timespec deadline;
};

int DatabaseModule::OnMessage(platch_obj *object, FlutterPlatformMessageResponseHandle *handle) {
  auto *args = &(object->std_arg);
  if (args->type != kStdMap) {
    return error_message(handle, "arguments isn't a map");
  }

  auto appName = get_string(args, "appName");
  auto app = get_app(args);
  if (app == nullptr) {
    return error_message(handle, "App (%s) not initialized.", appName.value_or("<default>").c_str());
  }

  firebase::database::Database *database = nullptr;
  auto databaseUrl = get_string(args, "databaseURL");
  if (databaseUrl) {
    database = firebase::database::Database::GetInstance(app, databaseUrl->c_str());
  } else {
    database = firebase::database::Database::GetInstance(app);
  }
  if (database == nullptr) {
    return error_message(handle, "Database not initialized.");
  }

  if (strcmp(object->method, "FirebaseDatabase#goOnline") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseDatabase#goOffline") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseDatabase#purgeOutstandingWrites") == 0) {

    // TODO

  } else if (strcmp(object->method, "FirebaseDatabase#setPersistenceEnabled") == 0) {

    database->set_persistence_enabled(stdmap_get_str(args, "enabled")->bool_value);
    return success(handle);

  } else if (strcmp(object->method, "FirebaseDatabase#setPersistenceCacheSizeBytes") == 0) {

    // TODO: This setting doesn't seem to exist on desktop.
    // database->set_persistence_cache_size(stdmap_get_str(args, "cacheSize")->int32_value);
    return success(handle);

  } else if (strcmp(object->method, "DatabaseReference#set") == 0) {

    auto value = get_variant(args, "value");
    if (!value) {
      return error_message(handle, "value argument is required");
    }
    auto reference = get_reference(database, args);
    auto priority = get_variant(args, "priority");
    auto future = priority ? reference.SetValueAndPriority(*value, *priority) : reference.SetValue(*value);
    future.OnCompletion([] (
        const firebase::Future<void>& result, void* userData) {
      auto handle = static_cast<FlutterPlatformMessageResponseHandle *>(userData);
      if (result.error() == firebase::database::kErrorNone) {
        success(handle);
      } else {
        error(handle, std::to_string(result.error()), result.error_message());
      }
    }, handle);
    return pending();

  } else if (strcmp(object->method, "DatabaseReference#update") == 0) {

    // TODO

  } else if (strcmp(object->method, "DatabaseReference#setPriority") == 0) {

    // TODO

  } else if (strcmp(object->method, "DatabaseReference#runTransaction") == 0) {

    auto key = get_variant(args, "transactionKey").value_or(firebase::Variant::Null());
    auto timeout = get_int(args, "transactionTimeout");
    if (!timeout) {
      return error_message(handle, "transactionTimeout argument is required");
    }
    auto transaction = new TransactionHandler(
        channel, handle, key, *timeout);
    auto reference = get_reference(database, args);
    auto future = reference.RunTransaction([](
        firebase::database::MutableData* data, void* context) {
      auto self = static_cast<TransactionHandler*>(context);
      return self->OnCallback(data);
    }, transaction);
    future.OnCompletion([handle, key, transaction] (
        const firebase::Future<firebase::database::DataSnapshot>& result) {
      fprintf(stderr, "[%d] runTransaction.OnCompletion(result=%d)\n", gettid(), result.error());
      auto valueMap = std::make_unique<ValueMap>();
      valueMap->add(val("transactionKey"), val(key));
      auto committed = result.error() == firebase::database::kErrorNone;
      if (!committed) {
        auto errorMap = std::make_unique<ValueMap>();
        errorMap->add(val("code"), val(result.error()));
        errorMap->add(val("message"), val(result.error_message()));
        //errorMap.add(val("details"), val(result.details()));
        valueMap->add(val("error"), std::move(errorMap));
      }
      valueMap->add(val("committed"), val(committed));
      if (committed || result.error() == firebase::database::kErrorTransactionAbortedByUser) {
        auto snapshotMap = std::make_unique<ValueMap>();
        snapshotMap->add(val("key"), val(result.result()->key()));
        snapshotMap->add(val("value"), val(result.result()->value()));
        valueMap->add(val("snapshot"), std::move(snapshotMap));
      }
      delete transaction;
      success(handle, std::move(valueMap));
    });
    return pending();

  } else if (strcmp(object->method, "OnDisconnect#set") == 0) {

    // TODO

  } else if (strcmp(object->method, "OnDisconnect#update") == 0) {

    // TODO

  } else if (strcmp(object->method, "OnDisconnect#cancel") == 0) {

    // TODO

  } else if (strcmp(object->method, "Query#keepSynced") == 0) {

    auto query = get_query(database, args);
    auto value = get_bool(args, "value");
    if (value) {
      query.SetKeepSynchronized(*value);
      return success(handle);
    }

  } else if (strcmp(object->method, "Query#observe") == 0) {

    int id = next_listener_id++;
    auto query = get_query(database, args);
    auto eventType = get_string(args, "eventType");
    if (eventType) {
      if (eventType == EVENT_TYPE_VALUE) {
        auto listener = new ValueListenerImpl(channel, *eventType, id);
        value_listeners[id] = listener;
        query.AddValueListener(listener);
      } else {
        auto listener = new ChildListenerImpl(channel, *eventType, id);
        child_listeners[id] = listener;
        query.AddChildListener(listener);
      }
      return success(handle, val(id));
    }

  } else if (strcmp(object->method, "Query#removeObserver") == 0) {

    auto id = get_int(args, "handle");
    auto query = get_query(database, args);    
    if (!id) {
      // Fall through.
    } else if (value_listeners.count(*id)) {
      auto listener = value_listeners[*id];
      value_listeners.erase(*id);
      query.RemoveValueListener(listener);
      delete listener;
      return success(handle);
    } else if (child_listeners.count(*id)) {
      auto listener = child_listeners[*id];
      child_listeners.erase(*id);
      query.RemoveChildListener(listener);
      delete listener;
      return success(handle);
    }

  }

  return not_implemented(handle);
}
