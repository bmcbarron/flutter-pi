#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include <pluginregistry.h>
#include <plugins/testplugin.h>
#include <flutter-pi.h>

#define INDENT_STRING "                    "

int __printJSON(struct json_value *value, int indent) {
    switch (value->type) {
        case kJsonNull:
            printf("null");
            break;
        case kJsonTrue:
            printf("true");
            break;
        case kJsonFalse:
            printf("false");
            break;
        case kJsonNumber:
            printf("%f", value->number_value);
            break;
        case kJsonString:
            printf("\"%s\"", value->string_value);
            break;
        case kJsonArray:
            printf("[\n");
            for (int i = 0; i < value->size; i++) {
                printf("%.*s", indent + 2, INDENT_STRING);
                __printJSON(&(value->array[i]), indent + 2);
                if (i+1 != value->size) printf(",\n");
            }
            printf("\n%.*s]", indent, INDENT_STRING);
            break;
        case kJsonObject:
            printf("{\n");
            for (int i = 0; i < value->size; i++) {
                printf("%.*s\"%s\": ", indent + 2, INDENT_STRING, value->keys[i]);
                __printJSON(&(value->values[i]), indent + 2);
                if (i+1 != value->size) printf(",\n");
            }
            printf("\n%.*s}", indent, INDENT_STRING);
            break;
        default: break;
    }

    return 0;
}
int printJSON(struct json_value *value, int indent) {
    printf("%.*s", indent, INDENT_STRING);
    __printJSON(value, indent);
    printf("\n");
    return 0;
}
int __printStd(struct std_value *value, int indent) {
    switch (value->type) {
        case kStdNull:
            printf("null");
            break;
        case kStdTrue:
            printf("true");
            break;
        case kStdFalse:
            printf("false");
            break;
        case kStdInt32:
            printf("%" PRIi32, value->int32_value);
            break;
        case kStdInt64:
            printf("%" PRIi64, value->int64_value);
            break;
        case kStdFloat64:
            printf("%lf", value->float64_value);
            break;
        case kStdString:
        case kStdLargeInt:
            printf("\"%s\"", value->string_value);
            break;
        case kStdUInt8Array:
            printf("(uint8_t) [");
            for (int i = 0; i < value->size; i++) {
                printf("0x%02X", value->uint8array[i]);
                if (i + 1 != value->size) printf(", ");
            }
            printf("]");
            break;
        case kStdInt32Array:
            printf("(int32_t) [");
            for (int i = 0; i < value->size; i++) {
                printf("%" PRIi32, value->int32array[i]);
                if (i + 1 != value->size) printf(", ");
            }
            printf("]");
            break;
        case kStdInt64Array:
            printf("(int64_t) [");
            for (int i = 0; i < value->size; i++) {
                printf("%" PRIi64, value->int64array[i]);
                if (i + 1 != value->size) printf(", ");
            }
            printf("]");
            break;
        case kStdFloat64Array:
            printf("(double) [");
            for (int i = 0; i < value->size; i++) {
                printf("%f", value->float64array[i]);
                if (i + 1 != value->size) printf(", ");
            }
            printf("]");
            break;
        case kStdList:
            printf("[\n");
            for (int i = 0; i < value->size; i++) {
                printf("%.*s", indent + 2, INDENT_STRING);
                __printStd(&(value->list[i]), indent + 2);
                if (i + 1 != value->size) printf(",\n");
            }
            printf("\n%.*s]", indent, INDENT_STRING);
            break;
        case kStdMap:
            printf("{\n");
            for (int i = 0; i < value->size; i++) {
                printf("%.*s", indent + 2, INDENT_STRING);
                __printStd(&(value->keys[i]), indent + 2);
                printf(": ");
                __printStd(&(value->values[i]), indent + 2);
                if (i + 1 != value->size) printf(",\n");
            }
            printf("\n%.*s}", indent, INDENT_STRING);
            break;
        default:
            break;
    }
    return 0;
}
int printStd(struct std_value *value, int indent) {
    printf("%.*s", indent, INDENT_STRING);
    __printStd(value, indent);
    printf("\n");
    return 0;
}

#undef INDENT_STRING

uint64_t testplugin_time_offset;

int on_response_json(struct platch_obj *object, void *userdata) {
    uint64_t dt = flutterpi.flutter.libflutter_engine.FlutterEngineGetCurrentTime() - *((uint64_t*) userdata);
    free(userdata);
    
    if (object->codec == kNotImplemented) {
        printf("channel " TESTPLUGIN_CHANNEL_JSON " not implented on flutter side\n");
        return 0;
    }

    if (object->success) {
        printf("on_response_json(dt: %lu)\n"
               "  success\n"
               "  result:\n", dt);
        printJSON(&object->json_result, 4);
    } else {
        printf("testp_on_response_json(dt: %lu)\n", dt);
        printf("  failure\n"
               "  error code: %s\n"
               "  error message: %s\n"
               "  error details:\n", object->error_code, (object->error_msg != NULL) ? object->error_msg : "null");
        printJSON(&object->json_result, 4);
    }

    return 0;
}
int send_json(void) {
    uint64_t* time = malloc(sizeof(uint64_t));
    *time = flutterpi.flutter.libflutter_engine.FlutterEngineGetCurrentTime();

    char *method = "test";
    struct json_value array_values[] = {
        {.type = kJsonString, .string_value = "array1"},
        {.type = kJsonNumber, .number_value = 2}
    };
    struct json_value array =  {.type = kJsonArray, .size = 2};
    array.array = array_values;
    struct json_value argument_values[] = {
        {.type = kJsonString, .string_value = "value1"},
        {.type = kJsonTrue},
        {.type = kJsonNumber, .number_value = -1000},
        {.type = kJsonNumber, .number_value = -5.0005},
        array,
    };
    char* argument_keys[] = {
        "key1",
        "key2",
        "key3",
        "key4",
        "array"
    };
    struct json_value argument = {
        .type = kJsonObject,
    };
    argument.size = 5;
    argument.keys = argument_keys;
    argument.values = argument_values;

    int ok = platch_call_json(TESTPLUGIN_CHANNEL_JSON, method, &argument, on_response_json, time);
    if (ok != 0) {
        printf("Could not MethodCall JSON: %s\n", strerror(ok));
    }
    return 0;
}
int on_response_std(struct platch_obj *object, void *userdata) {
    uint64_t dt = flutterpi.flutter.libflutter_engine.FlutterEngineGetCurrentTime() - *((uint64_t*) userdata);
    free(userdata);

    if (object->codec == kNotImplemented) {
        printf("channel " TESTPLUGIN_CHANNEL_STD " not implented on flutter side\n");
        return 0;
    }

    if (object->success) {
        printf("testp_on_response_std(dt: %lu)\n"
               "  success\n"
               "  result:\n", dt);
        printStd(&object->std_result, 4);
    } else {
        printf("testp_on_response_std(dt: %lu)\n", dt);
        printf("  failure\n"
               "  error code: %s\n"
               "  error message: %s\n"
               "  error details:\n", object->error_code, (object->error_msg != NULL) ? object->error_msg : "null");
        printStd(&object->std_error_details, 4);
    }

    return 0;
}
int send_std() {
    uint64_t *time = malloc(sizeof(uint64_t));
    *time = flutterpi.flutter.libflutter_engine.FlutterEngineGetCurrentTime();

    char *method = "test";
    uint8_t int_array_values[] = {0x00, 0x01, 0x02, 0x03, 0xFF};
    struct std_value int_array = {
        .type = kStdUInt8Array,
    };
    int_array.uint8array = int_array_values;
    int_array.size = 5;
    struct std_value list_values[] = {
        {.type = kStdString, .string_value = "array1"},
        {.type = kStdInt32, .int32_value = 2}
    };
    struct std_value list = {
        .type = kStdList,
    };
    list.size = 2;
    list.list = list_values;
    struct std_value argument_values[] = {
        {.type = kStdString, .string_value = "value1"},
        {.type = kStdTrue},
        {.type = kStdInt32, .int32_value = -1000},
        {.type = kStdFloat64, .float64_value = -5.0005},
        int_array,
        {.type = kStdInt64, .int64_value = *time & 0x7FFFFFFFFFFFFFFF},
        list,
    };
    struct std_value argument_keys[] = {
        {.type = kStdString, .string_value = "key1"},
        {.type = kStdString, .string_value = "key2"},
        {.type = kStdString, .string_value = "key3"},
        {.type = kStdString, .string_value = "key4"},
        {.type = kStdInt32, .int32_value = 5},
        {.type = kStdString, .string_value = "timestamp"},
        {.type = kStdString, .string_value = "array"}
    };
    struct std_value argument = {
        .type = kStdMap,
    };
    argument.size = 7;
    argument.keys = argument_keys;
    argument.values = argument_values;

    platch_call_std(TESTPLUGIN_CHANNEL_STD, method, &argument, on_response_std, time);
    return 0;
}


int on_receive_json(char *channel, struct platch_obj *object, FlutterPlatformMessageResponseHandle *responsehandle) {
    printf("[test plugin] on_receive_json(channel: %s)\n"
           "  method: %s\n"
           "  args: \n", channel, object->method);
    printJSON(&(object->json_arg), 4);
    
    send_json();

    struct platch_obj response = {
        .codec = kJSONMethodCallResponse,
    };
    response.success = true;
    response .json_result = {
        .type = kJsonTrue
    };
    return platch_respond(responsehandle, &response);
}
int on_receive_std(char *channel, struct platch_obj *object, FlutterPlatformMessageResponseHandle *responsehandle) {
    printf("[test plugin] on_receive_std(channel: %s)\n"
           "  method: %s\n"
           "  args: \n", channel, object->method);

    printStd(&(object->std_arg), 4);

    send_std();
    
    struct platch_obj response = {
        .codec = kStandardMethodCallResponse,
    };
    response.success = true;
    response.std_result = {
        .type = kStdTrue
    };
    return platch_respond(
        responsehandle,
        &response
    );
}
int on_receive_ping(char *channel, struct platch_obj *object, FlutterPlatformMessageResponseHandle *responsehandle) {
    return platch_respond(
        responsehandle,
        &(struct platch_obj) {
            .codec = kStringCodec,
            .string_value = "pong"
        }
    );
}


int testp_init(void) {
    int ok;

    ok = plugin_registry_set_receiver(TESTPLUGIN_CHANNEL_JSON, kJSONMethodCall, on_receive_json);
    if (ok != 0) return ok;

    ok = plugin_registry_set_receiver(TESTPLUGIN_CHANNEL_STD, kStandardMethodCall, on_receive_std);
    if (ok != 0) {
        plugin_registry_remove_receiver(TESTPLUGIN_CHANNEL_JSON);
        return ok;
    }
    
    ok = plugin_registry_set_receiver(TESTPLUGIN_CHANNEL_PING, kStringCodec, on_receive_ping);
    if (ok != 0) {
        plugin_registry_remove_receiver(TESTPLUGIN_CHANNEL_STD);
        plugin_registry_remove_receiver(TESTPLUGIN_CHANNEL_JSON);
        return ok;
    }

    return 0;
}
int testp_deinit(void) {
    plugin_registry_remove_receiver(TESTPLUGIN_CHANNEL_PING);
    plugin_registry_remove_receiver(TESTPLUGIN_CHANNEL_STD);
    plugin_registry_remove_receiver(TESTPLUGIN_CHANNEL_JSON);
    
    return 0;
}