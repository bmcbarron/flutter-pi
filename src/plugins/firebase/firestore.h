#ifndef _PLUGINS_FIREBASE_FIRESTORE_H
#define _PLUGINS_FIREBASE_FIRESTORE_H

#include "module.h"

const char kFieldValueTypeKey[] = "_fieldValueType";

class FirestoreModule : public Module {
 public:
  FirestoreModule();

  virtual int firestoreRemoveListener(std_value *args,
                                      FlutterPlatformMessageResponseHandle *response_handle);
  virtual int transactionCreate(std_value *args,
                                FlutterPlatformMessageResponseHandle *response_handle);
  virtual int transactionGet(std_value *args,
                             FlutterPlatformMessageResponseHandle *response_handle);
  virtual int queryAddSnapshotListener(
      std_value *args, FlutterPlatformMessageResponseHandle *response_handle);
  virtual int queryGet(std_value *args,
                       FlutterPlatformMessageResponseHandle *response_handle);
  virtual int documentAddSnapshotListener(
      std_value *args, FlutterPlatformMessageResponseHandle *response_handle);
  virtual int documentGet(
      std_value *args, FlutterPlatformMessageResponseHandle *response_handle);
  virtual int documentSet(
      std_value *args, FlutterPlatformMessageResponseHandle *response_handle);
  virtual int documentUpdate(
      std_value *args, FlutterPlatformMessageResponseHandle *response_handle);
  virtual int documentDelete(
      std_value *args, FlutterPlatformMessageResponseHandle *response_handle);
};

#endif
