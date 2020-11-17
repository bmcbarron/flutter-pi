#include "std_codec.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include <memory>
#include <optional>
#include <vector>

#include <unistd.h>
#include <string.h>
#include <sys/select.h>

#include "flutter-pi.h"
#include <platformchannel.h>

#define INDENT_STRING "                    "

int __stdPrint(struct std_value *value, int indent) {
  switch (value->type) {
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
        if (i + 1 != value->size) fprintf(stderr, ", ");
      }
      fprintf(stderr, "]");
      break;
    case kStdInt32Array:
      fprintf(stderr, "(int32_t) [");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%" PRIi32, value->int32array[i]);
        if (i + 1 != value->size) fprintf(stderr, ", ");
      }
      fprintf(stderr, "]");
      break;
    case kStdInt64Array:
      fprintf(stderr, "(int64_t) [");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%" PRIi64, value->int64array[i]);
        if (i + 1 != value->size) fprintf(stderr, ", ");
      }
      fprintf(stderr, "]");
      break;
    case kStdFloat64Array:
      fprintf(stderr, "(double) [");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%f", value->float64array[i]);
        if (i + 1 != value->size) fprintf(stderr, ", ");
      }
      fprintf(stderr, "]");
      break;
    case kStdList:
      fprintf(stderr, "[\n");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%.*s", indent + 2, INDENT_STRING);
        __stdPrint(&(value->list[i]), indent + 2);
        if (i + 1 != value->size) fprintf(stderr, ",\n");
      }
      fprintf(stderr, "\n%.*s]", indent, INDENT_STRING);
      break;
    case kStdMap:
      fprintf(stderr, "{\n");
      for (int i = 0; i < value->size; i++) {
        fprintf(stderr, "%.*s", indent + 2, INDENT_STRING);
        __stdPrint(&(value->keys[i]), indent + 2);
        fprintf(stderr, ": ");
        __stdPrint(&(value->values[i]), indent + 2);
        if (i + 1 != value->size) fprintf(stderr, ",\n");
      }
      fprintf(stderr, "\n%.*s}", indent, INDENT_STRING);
      break;
    default:
      break;
  }
  return 0;
}

int stdPrint(struct std_value *value, int indent) {
  fprintf(stderr, "%.*s", indent, INDENT_STRING);
  __stdPrint(value, indent);
  fprintf(stderr, "\n");
  return 0;
}
