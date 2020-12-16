#ifndef _PLUGINS_FIREBASE_H
#define _PLUGINS_FIREBASE_H

int firebase_init();
int firebase_deinit();

int on_decode_firestore_type_std(enum std_value_type type, uint8_t **pbuffer, size_t *premaining,
                                 struct std_value *value_out);

#endif
