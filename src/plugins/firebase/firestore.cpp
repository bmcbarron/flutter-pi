#include "firestore.h"

#include <cassert>

#include "core.h"
#include "firebase/firestore.h"
#include "util.h"

namespace firebase {
namespace firestore {
using ::val;

std::unique_ptr<Value> val(const FieldValue &value) {
  switch (value.type()) {
  case FieldValue::Type::kNull:
    return val();
  case FieldValue::Type::kBoolean:
    return val(value.boolean_value());
  case FieldValue::Type::kInteger:
    return val(value.integer_value());
  case FieldValue::Type::kDouble:
    return val(value.double_value());
  case FieldValue::Type::kTimestamp: {
    auto result = std::make_unique<ValuePreEncoded>(136, 1 + 8 + 4);
    int ok = 0;
    ok |= _write64(&result->buffer, value.timestamp_value().seconds(), &result->remaining);
    ok |= _write32(&result->buffer, value.timestamp_value().nanoseconds(), &result->remaining);
    assert(ok == 0);
    return result;
  }
  case FieldValue::Type::kString:
    return val(value.string_value());
  // case FieldValue::Type::kBlob: return val();
  // case FieldValue::Type::kReference: return val();
  // case FieldValue::Type::kGeoPoint: return val();
  case FieldValue::Type::kArray: {
    auto result = std::make_unique<ValueList>();
    for (auto const &v : value.array_value()) {
      result->add(val(v));
    }
    return result;
  }
  case FieldValue::Type::kMap: {
    auto result = std::make_unique<ValueMap>();
    for (auto const &[k, v] : value.map_value()) {
      result->set(k, v);
    }
    return result;
  }
    // case FieldValue::Type::kArrayUnion: return val();
    // case FieldValue::Type::kArrayRemove: return val();
    // case FieldValue::Type::kIncrementInteger: return val();
    // case FieldValue::Type::kIncrementDouble: return val();
  }
  fprintf(stderr, "Error converting FieldValue type: %d\n", value.type());
  return val();
}

std::unique_ptr<Value> val(const MapFieldValue &value) {
  auto result = std::make_unique<ValueMap>();
  for (const auto &e : value) {
    result->set(e.first, e.second);
  }
  return std::move(result);
}

std::unique_ptr<Value> val(const SnapshotMetadata *value) {
  auto result = std::make_unique<ValueMap>();
  result->set("hasPendingWrites", value->has_pending_writes());
  result->set("isFromCache", value->is_from_cache());
  return std::move(result);
}

std::unique_ptr<Value> val(const DocumentSnapshot *value) {
  auto result = std::make_unique<ValueMap>();
  result->set("path", value->reference().path());
  if (value->exists()) {
    result->set("data", value->GetData());
  } else {
    result->set("data", val());
  }
  auto metadata = value->metadata();
  result->set("metadata", &metadata);
  return std::move(result);
}

std::unique_ptr<Value> val(const DocumentChange &value) {
  auto result = std::make_unique<ValueMap>();
  switch (value.type()) {
  case DocumentChange::Type::kAdded:
    result->set("type", "DocumentChangeType.added");
    break;
  case DocumentChange::Type::kModified:
    result->set("type", "DocumentChangeType.modified");
    break;
  case DocumentChange::Type::kRemoved:
    result->set("type", "DocumentChangeType.removed");
    break;
  default:
    assert(false);
  }
  result->set("data", value.document().GetData());
  result->set("path", value.document().reference().path());
  result->set("oldIndex", value.old_index());
  result->set("newIndex", value.new_index());
  auto metadata = value.document().metadata();
  result->set("metadata", &metadata);
  return std::move(result);
}

std::unique_ptr<Value> val(const QuerySnapshot *value) {
  auto paths = std::make_unique<ValueList>();
  auto result = std::make_unique<ValueMap>();
  auto documents = std::make_unique<ValueList>();
  auto metadatas = std::make_unique<ValueList>();
  for (const auto &doc : value->documents()) {
    paths->add(val(doc.reference().path()));
    documents->add(val(doc.GetData()));
    auto metadata = doc.metadata();
    metadatas->add(val(&metadata));
  }
  result->set("paths", std::move(paths));
  result->set("documents", std::move(documents));
  result->set("metadatas", std::move(metadatas));
  auto change = value->DocumentChanges();
  result->set("documentChanges", change);
  auto metadata = value->metadata();
  result->set("metadata", &metadata);
  return std::move(result);
}

} // namespace firestore
} // namespace firebase

std::optional<firebase::firestore::FieldPath> as_fieldpath(std_value *args) {
  if (args->type != kStdList) {
    fprintf(stderr, "Unexpected type (%d) of fieldpath\n", args->type);
    return std::nullopt;
  }
  std::vector<std::string> fieldNames;
  for (int i = 0; i < args->size; ++i) {
    auto &name = args->list[i];
    if (name.type != kStdString) {
      fprintf(stderr, "Unexpected type (%d) of fieldpath segment[%d]\n", name.type, i);
      return std::nullopt;
    }
    fieldNames.push_back(name.string_value);
  }
  return firebase::firestore::FieldPath(fieldNames);
}

std::optional<firebase::firestore::FieldValue> decode_fieldvalue(std_value *value) {
  // The address of values is the same as the first value.
  auto type = as_string(value->values);
  if (type == "ServerTimestamp") {
    return firebase::firestore::FieldValue::ServerTimestamp();
  } else if (type == "Delete") {
    return firebase::firestore::FieldValue::Delete();
  } else if (type == "Timestamp") {
    return firebase::firestore::FieldValue::Timestamp(
        firebase::Timestamp(get_int64(value, "seconds").value(), get_int(value, "nanos").value()));
  }
  return std::nullopt;
}

std::optional<firebase::firestore::FieldValue> as_fieldvalue(std_value *value) {
  switch (value->type) {
  case kStdNull:
    return firebase::firestore::FieldValue::Null();
  case kStdInt32:
    return firebase::firestore::FieldValue::Integer(value->int32_value);
  case kStdInt64:
    return firebase::firestore::FieldValue::Integer(value->int64_value);
  case kStdFloat64:
    return firebase::firestore::FieldValue::Double(value->float64_value);
  case kStdTrue:
    return firebase::firestore::FieldValue::Boolean(true);
  case kStdFalse:
    return firebase::firestore::FieldValue::Boolean(false);
  case kStdString:
    return firebase::firestore::FieldValue::String(value->string_value);
  case kStdList: {
    std::vector<firebase::firestore::FieldValue> elements;
    for (int i = 0; i < value->size; ++i) {
      auto e = as_fieldvalue(&(value->list[i]));
      assert(e);
      elements.emplace_back(*e);
    }
    return firebase::firestore::FieldValue::Array(elements);
  }
  case kStdMap:
    if (value->size > 0 && value->keys->type == kStdString &&
        strcmp(value->keys->string_value, kFieldValueTypeKey) == 0) {
      return decode_fieldvalue(value);
    } else {
      auto elements = firebase::firestore::MapFieldValue();
      for (int i = 0; i < value->size; ++i) {
        auto k = as_string(value->keys + i);
        auto v = as_fieldvalue(value->values + i);
        assert(k && v);
        elements[*k] = *v;
      }
      return firebase::firestore::FieldValue::Map(elements);
    }
  }
  return std::nullopt;
}

std::optional<firebase::firestore::FieldValue> get_fieldvalue(std_value *args, char *key) {
  auto result = stdmap_get_str(args, key);
  if (result == nullptr) {
    return std::nullopt;
  }
  return as_fieldvalue(result);
}

std::optional<std::vector<firebase::firestore::FieldPath>> get_mergefields(std_value *args) {
  auto mergeFields = get_list(args, "mergeFields");
  if (!mergeFields) {
    return std::nullopt;
  }
  std::vector<firebase::firestore::FieldPath> result;
  for (int i = 0; i < mergeFields->size; ++i) {
    auto path = as_fieldpath(&mergeFields->list[i]);
    if (!path) {
      return std::nullopt;
    }
    result.push_back(*path);
  }
  return result;
}

std::optional<firebase::firestore::SetOptions> get_setoptions(std_value *args) {
  auto options = get_map(args, "options");
  if (options) {
    auto merge = get_bool(options, "merge").value_or(false);
    if (merge) {
      return firebase::firestore::SetOptions::Merge();
    }
    auto mergeFields = get_mergefields(options);
    if (mergeFields) {
      return firebase::firestore::SetOptions::MergeFieldPaths(*mergeFields);
    }
  }
  return std::nullopt;
}

std::unique_ptr<firebase::firestore::Settings> get_settings(std_value *args) {
  auto settings = get_map(args, "settings");
  if (!settings) {
    return nullptr;
  }
  auto result = std::make_unique<firebase::firestore::Settings>();
  auto persistenceEnabled = get_bool(settings, "persistenceEnabled");
  if (persistenceEnabled) {
    result->set_persistence_enabled(*persistenceEnabled);
  }
  auto host = get_string(settings, "host");
  if (host) {
    result->set_host(*host);
    auto sslEnabled = get_bool(settings, "sslEnabled");
    if (sslEnabled) {
      result->set_ssl_enabled(*sslEnabled);
    }
  }
  auto cacheSizeBytes = get_int(settings, "cacheSizeBytes");
  if (cacheSizeBytes) {
    // TODO: Do something with cacheSizeBytes
  }
  return std::move(result);
}

firebase::firestore::Firestore *get_instance(std_value *args) {
  auto firestore = get_map(args, "firestore");
  if (!firestore) {
    return nullptr;
  }
  auto app = get_app(firestore);
  auto settings = get_settings(firestore);
  firebase::firestore::Firestore *instance = nullptr;
  if (strcmp(app->name(), firebase::kDefaultAppName) == 0) {
    instance = firebase::firestore::Firestore::GetInstance();
  } else {
    instance = firebase::firestore::Firestore::GetInstance(app);
  }
  auto currentSettings = instance->settings();
  if (currentSettings.host() != settings->host() ||
      currentSettings.is_persistence_enabled() != settings->is_persistence_enabled() ||
      currentSettings.is_ssl_enabled() != settings->is_ssl_enabled()) {
    // This equality test is in lieu of caching whether or not we've called
    // set_settings on this instance before.
    instance->set_settings(*settings);
  }
  return instance;
}

firebase::firestore::DocumentReference get_docref(std_value *args) {
  auto reference = get_map(args, "reference");
  if (!reference) {
    return firebase::firestore::DocumentReference();
  }
  auto instance = get_instance(reference);
  auto path = get_string(reference, "path");
  return instance->Document(*path);
}

firebase::firestore::Source get_source(std_value *args) {
  auto source = get_string(args, "source");
  if (source) {
    if (source.value() == "server") {
      return firebase::firestore::Source::kServer;
    } else if (source.value() == "cache") {
      return firebase::firestore::Source::kCache;
    }
  }
  return firebase::firestore::Source::kDefault;
}

firebase::firestore::Query get_query(std_value *args) {
  auto query = get_map(args, "query");
  assert(query != nullptr);

  auto instance = get_instance(query);
  auto isCollectionGroup = get_bool(query, "isCollectionGroup").value();
  auto path = get_string(query, "path").value();

  auto result = (isCollectionGroup) ? instance->CollectionGroup(path) : instance->Collection(path);

  auto parameters = get_map(query, "parameters");
  if (!parameters) {
    return result;
  }

  // "where" filters
  auto filters = get_list(parameters, "where");
  assert(filters != nullptr);
  for (int i = 0; i < filters->size; ++i) {
    auto &condition = filters->list[i];
    assert(condition.type == kStdList);
    assert(condition.size == 3);
    auto fieldPath = as_fieldpath(&condition.list[0]).value();
    auto op = as_string(&condition.list[1]).value();
    auto value = as_fieldvalue(&condition.list[2]).value();

    if ("==" == op) {
      result = result.WhereEqualTo(fieldPath, value);
      // } else if ("!=" == op) {
      //   result = result.WhereNotEqualTo(fieldPath, value);
    } else if ("<" == op) {
      result = result.WhereLessThan(fieldPath, value);
    } else if ("<=" == op) {
      result = result.WhereLessThanOrEqualTo(fieldPath, value);
    } else if (">" == op) {
      result = result.WhereGreaterThan(fieldPath, value);
    } else if (">=" == op) {
      result = result.WhereGreaterThanOrEqualTo(fieldPath, value);
    } else if ("array-contains" == op) {
      result = result.WhereArrayContains(fieldPath, value);
    } else if ("array-contains-any" == op) {
      result = result.WhereArrayContainsAny(fieldPath, value.array_value());
    } else if ("in" == op) {
      result = result.WhereIn(fieldPath, value.array_value());
      // } else if ("not-in" == op) {
      //   result = result.WhereNotIn(fieldPath, value.array_value());
    } else {
      fprintf(stderr, "An invalid query operator %s was received but not handled\n", op.c_str());
    }
  }

  // "limit" filters
  auto limit = get_int(parameters, "limit");
  if (limit)
    result = result.Limit(*limit);

  auto limitToLast = get_int(parameters, "limitToLast");
  if (limitToLast)
    result = result.LimitToLast(*limitToLast);

  // "orderBy" filters
  auto orderBy = get_list(parameters, "orderBy");
  if (!orderBy)
    return result;

  for (int j = 0; j < orderBy->size; ++j) {
    auto &order = orderBy->list[j];
    assert(order.type == kStdList);
    assert(order.size == 2);
    auto fieldPath = as_fieldpath(&order.list[0]).value();
    auto descending = as_bool(&order.list[1]).value();

    auto direction = descending ? firebase::firestore::Query::Direction::kDescending
                                : firebase::firestore::Query::Direction::kAscending;

    result = result.OrderBy(fieldPath, direction);
  }

  // cursor queries
  auto startAt = get_fieldvalue(parameters, "startAt");
  if (startAt && !startAt->is_null())
    result = result.StartAt(startAt->array_value());

  auto startAfter = get_fieldvalue(parameters, "startAfter");
  if (startAfter && !startAfter->is_null())
    result = result.StartAfter(startAfter->array_value());

  auto endAt = get_fieldvalue(parameters, "endAt");
  if (endAt && !endAt->is_null())
    result = result.EndAt(endAt->array_value());

  auto endBefore = get_fieldvalue(parameters, "endBefore");
  if (endBefore && !endBefore->is_null())
    result = result.EndBefore(endBefore->array_value());

  return result;
}

static std::map<int, firebase::firestore::ListenerRegistration> listenerRegistrations;

FirestoreModule::FirestoreModule() : Module("plugins.flutter.io/firebase_firestore") {
  Register("Firestore#removeListener", &FirestoreModule::firestoreRemoveListener);
  // Register("Firestore#disableNetwork",
  // &FirestoreModule::Firestore#disableNetwork);
  // Register("Firestore#enableNetwork",
  // &FirestoreModule::Firestore#enableNetwork);
  // Register("Firestore#addSnapshotsInSyncListener",
  // &FirestoreModule::Firestore#addSnapshotsInSyncListener);
  Register("Transaction#create", &FirestoreModule::transactionCreate);
  Register("Transaction#get", &FirestoreModule::transactionGet);
  // Register("WriteBatch#commit", &FirestoreModule::WriteBatch#commit);
  Register("Query#addSnapshotListener", &FirestoreModule::queryAddSnapshotListener);
  Register("Query#get", &FirestoreModule::queryGet);
  Register("DocumentReference#addSnapshotListener", &FirestoreModule::documentAddSnapshotListener);
  Register("DocumentReference#get", &FirestoreModule::documentGet);
  Register("DocumentReference#set", &FirestoreModule::documentSet);
  Register("DocumentReference#update", &FirestoreModule::documentUpdate);
  Register("DocumentReference#delete", &FirestoreModule::documentDelete);
  // Register("Firestore#clearPersistence",
  // &FirestoreModule::Firestore#clearPersistence);
  // Register("Firestore#waitForPendingWrites",
  // &FirestoreModule::Firestore#waitForPendingWrites);
  // Register("Firestore#terminate", &FirestoreModule::Firestore#terminate);
}

int FirestoreModule::firestoreRemoveListener(
    std_value *args, FlutterPlatformMessageResponseHandle *response_handle) {
  auto handle = get_int(args, "handle").value();
  auto listener = map_remove(listenerRegistrations, handle);
  listener.Remove();
  return success(response_handle);
}

class MutexLocker {
public:
  MutexLocker(pthread_mutex_t &m) : mutex(&m) {
    pthread_mutex_lock(mutex);
    isLocked = true;
  }

  void Relock() {
    pthread_mutex_lock(mutex);
    isLocked = true;
  }

  void Unlock() {
    if (isLocked) {
      isLocked = false;
      pthread_mutex_unlock(mutex);
    }
  }

  ~MutexLocker() {
    if (isLocked) {
      pthread_mutex_unlock(mutex);
    }
  }

private:
  bool isLocked;
  pthread_mutex_t *mutex;
};

class TransactionHandler {
public:
  static int Get(FlutterPlatformMessageResponseHandle *response_handle, int transactionId,
                 const firebase::firestore::DocumentReference &document) {
    firebase::firestore::Error error_code = firebase::firestore::kErrorNone;
    std::string error_message;

    MutexLocker lock(transactions_mutex);
    auto transaction = transactions.find(transactionId);
    assert(transaction != transactions.end());
    auto snapshot = transaction->second->Get(document, &error_code, &error_message);
    lock.Unlock();

    if (error_code != firebase::firestore::kErrorNone) {
      return error(response_handle, std::to_string(error_code), error_message);
    }
    return success(response_handle, val(&snapshot));
  }

  TransactionHandler(FlutterPlatformMessageResponseHandle *response_handle, std::string channel,
                     firebase::firestore::Firestore *instance, int transactionId, int timeout)
      : response_handle(response_handle), channel(channel), instance(instance),
        transactionId(transactionId) {
    auto ok = clock_gettime(CLOCK_REALTIME, &deadline);
    assert(ok == 0);
    deadline.tv_sec += timeout / 1000;
    deadline.tv_nsec += (timeout % 1000) * 1000000;
  }

  static int Run(std::unique_ptr<TransactionHandler> handler) {
    using namespace std::placeholders;
    auto future = handler->instance->RunTransaction(
        std::bind(&TransactionHandler::OnTransaction, handler.get(), _1, _2));
    future.OnCompletion(std::bind(&TransactionHandler::OnCompletion, handler.release(), _1));
    return pending();
  }

private:
  // Called from an arbitrary thread.
  firebase::firestore::Error OnTransaction(firebase::firestore::Transaction &transaction,
                                           std::string &error_message) {
    fprintf(stderr, "[%d] TransactionHandler.OnTransaction(%d)\n", gettid(), transactionId);
    auto arguments = std::make_unique<ValueMap>();
    arguments->set("transactionId", transactionId);
    arguments->set("appName", get_app_name(instance->app()));

    // TODO: Use RAII to free value in an exception-safe way.
    platch_obj value;
    // TODO: Use RAII to do the erase in an exception-safe way.
    MutexLocker lock(transactions_mutex);
    transactions.insert(std::make_pair(transactionId, &transaction));
    lock.Unlock();
    auto result = invoke_sync(channel.c_str(), "Transaction#attempt", std::move(arguments),
                              &deadline, &value);
    lock.Relock();
    transactions.erase(transactionId);
    lock.Unlock();

    if (result == INVOKE_TIMEDOUT) {
      return firebase::firestore::kErrorDeadlineExceeded;
    }
    if (result == INVOKE_FAILED) {
      return firebase::firestore::kErrorInternal;
    }
    if (!value.success) {
      fprintf(stderr, "[%d] Transaction#attempt error: %s %s ", gettid(), value.error_code,
              value.error_msg);
      stdPrint(&value.std_error_details);
      platch_free_obj(&value);
      return firebase::firestore::kErrorAborted;
    }

    auto type = get_string(&value.std_result, "type").value();
    if (type == "ERROR") {
      // Do nothing - already handled in Dart land.
      platch_free_obj(&value);
      return firebase::firestore::kErrorNone;
    }

    auto commands = get_list(&value.std_result, "commands");
    assert(commands != nullptr);
    for (int i = 0; i < commands->size; ++i) {
      type = get_string(&commands->list[i], "type").value();
      auto path = get_string(&commands->list[i], "path").value();
      auto document = instance->Document(path);
      auto data = get_fieldvalue(&commands->list[i], "data");
      if (type == "DELETE") {
        transaction.Delete(document);
      } else if (type == "UPDATE") {
        transaction.Update(document, data->map_value());
      } else if (type == "SET") {
        auto setOptions = get_setoptions(&commands->list[i]);
        if (setOptions) {
          transaction.Set(document, data->map_value(), *setOptions);
        } else {
          transaction.Set(document, data->map_value());
        }
      } else {
        fprintf(stderr, "Unknown transaction type: %s\n", type.c_str());
        assert(false);
      }
    }
    platch_free_obj(&value);
    return firebase::firestore::kErrorNone;
  }

  static void OnCompletion(TransactionHandler *handler, const firebase::Future<void> &result) {
    // auto unique = std::unique_ptr<TransactionHandler>(handler);
    fprintf(stderr, "[%d] TransactionHandler.OnCompletion(%d)\n", gettid(), handler->transactionId);
    success(handler->response_handle);
    fprintf(stderr, "[%d] TransactionHandler.OnCompletion(%d) done\n", gettid(),
            handler->transactionId);
  }

  FlutterPlatformMessageResponseHandle *response_handle;
  const std::string channel;
  firebase::firestore::Firestore *instance;
  const int transactionId;
  timespec deadline;

  static pthread_mutex_t transactions_mutex;
  static std::map<int, firebase::firestore::Transaction *> transactions;
};

pthread_mutex_t TransactionHandler::transactions_mutex = PTHREAD_MUTEX_INITIALIZER;
std::map<int, firebase::firestore::Transaction *> TransactionHandler::transactions;

int FirestoreModule::transactionCreate(std_value *args,
                                       FlutterPlatformMessageResponseHandle *response_handle) {
  auto instance = get_instance(args);
  auto transactionId = get_int(args, "transactionId").value();
  auto timeout = get_int(args, "timeout").value_or(5000);
  auto handler = std::make_unique<TransactionHandler>(response_handle, channel, instance,
                                                      transactionId, timeout);
  return TransactionHandler::Run(std::move(handler));
}

int FirestoreModule::transactionGet(std_value *args,
                                    FlutterPlatformMessageResponseHandle *response_handle) {
  auto document = get_docref(args);
  auto transactionId = get_int(args, "transactionId").value();
  return TransactionHandler::Get(response_handle, transactionId, document);
}

int FirestoreModule::queryAddSnapshotListener(
    std_value *args, FlutterPlatformMessageResponseHandle *response_handle) {
  auto handle = get_int(args, "handle").value();

  auto metadataChanges = get_bool(args, "includeMetadataChanges").value()
                             ? firebase::firestore::MetadataChanges::kInclude
                             : firebase::firestore::MetadataChanges::kExclude;

  auto query = get_query(args);

  auto listener = query.AddSnapshotListener(
      metadataChanges,
      [this, handle](const firebase::firestore::QuerySnapshot &snapshot,
                     firebase::firestore::Error error, const std::string &errorMessage) {
        fprintf(stderr, "Got snapshot: error: %d errorMessage: %s\n", error, errorMessage.c_str());
        auto eventMap = std::make_unique<ValueMap>();

        eventMap->set("handle", handle);

        if (error != firebase::firestore::Error::kErrorOk) {
          auto exceptionMap = std::make_unique<ValueMap>();
          exceptionMap->set("code", error);
          exceptionMap->set("message", errorMessage);
          eventMap->set("error", std::move(exceptionMap));
          invoke(channel, "QuerySnapshot#error", std::move(eventMap));
        } else {
          eventMap->set("snapshot", &snapshot);
          invoke(channel, "QuerySnapshot#event", std::move(eventMap));
        }
      });

  listenerRegistrations[handle] = listener;
  return success(response_handle);
}

int FirestoreModule::queryGet(std_value *args,
                              FlutterPlatformMessageResponseHandle *response_handle) {
  auto source = get_source(args);
  auto query = get_query(args);
  return Await(query.Get(source), response_handle, firebase::firestore::val);
}

int FirestoreModule::documentAddSnapshotListener(
    std_value *args, FlutterPlatformMessageResponseHandle *response_handle) {
  auto handle = get_int(args, "handle").value();

  auto metadataChanges = get_bool(args, "includeMetadataChanges").value()
                             ? firebase::firestore::MetadataChanges::kInclude
                             : firebase::firestore::MetadataChanges::kExclude;

  auto document = get_docref(args);
  document.AddSnapshotListener(
      metadataChanges,
      [this, handle](const firebase::firestore::DocumentSnapshot &snapshot,
                     firebase::firestore::Error error, const std::string &errorMessage) {
        fprintf(stderr, "Got snapshot: error: %d errorMessage: %s\n", error, errorMessage.c_str());
        auto eventMap = std::make_unique<ValueMap>();

        eventMap->set("handle", handle);

        if (error != firebase::firestore::Error::kErrorOk) {
          auto exceptionMap = std::make_unique<ValueMap>();
          exceptionMap->set("code", error);
          exceptionMap->set("message", errorMessage);
          eventMap->set("error", std::move(exceptionMap));
          invoke(channel, "DocumentSnapshot#error", std::move(eventMap));
        } else {
          eventMap->set("snapshot", &snapshot);
          invoke(channel, "DocumentSnapshot#event", std::move(eventMap));
        }
      });

  return success(response_handle);
}

int FirestoreModule::documentGet(std_value *args,
                                 FlutterPlatformMessageResponseHandle *response_handle) {
  auto source = get_source(args);
  auto document = get_docref(args);
  return Await(document.Get(source), response_handle, firebase::firestore::val);
}

int FirestoreModule::documentSet(std_value *args,
                                 FlutterPlatformMessageResponseHandle *response_handle) {
  auto document = get_docref(args);
  auto data = get_fieldvalue(args, "data");
  auto setOptions = get_setoptions(args);
  auto op =
      setOptions ? document.Set(data->map_value(), *setOptions) : document.Set(data->map_value());
  return Await(op, response_handle);
}

int FirestoreModule::documentUpdate(std_value *args,
                                    FlutterPlatformMessageResponseHandle *response_handle) {
  auto document = get_docref(args);
  auto data = get_fieldvalue(args, "data");
  return Await(document.Update(data->map_value()), response_handle);
}

int FirestoreModule::documentDelete(std_value *args,
                                    FlutterPlatformMessageResponseHandle *response_handle) {
  auto document = get_docref(args);
  return Await(document.Delete(), response_handle);
}
