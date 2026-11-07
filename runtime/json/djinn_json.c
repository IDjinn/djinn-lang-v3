#include <stdint.h>
#include <stddef.h>
#include "../third_party/yyjson/yyjson.h"

#define DJINN_JSON_NULL 0
#define DJINN_JSON_BOOL 1
#define DJINN_JSON_INT 2
#define DJINN_JSON_REAL 3
#define DJINN_JSON_STRING 4
#define DJINN_JSON_ARRAY 5
#define DJINN_JSON_OBJECT 6

void* __djinn_json_parse(const char* text, int32_t length)
{
    return yyjson_read(text, (size_t)length, 0);
}

void __djinn_json_free(void* document)
{
    yyjson_doc_free((yyjson_doc*)document);
}

void* __djinn_json_root(void* document)
{
    return yyjson_doc_get_root((yyjson_doc*)document);
}

void* __djinn_json_get(void* value, const char* key)
{
    return yyjson_obj_get((yyjson_val*)value, key);
}

void* __djinn_json_at(void* value, int32_t index)
{
    return yyjson_arr_get((yyjson_val*)value, (size_t)index);
}

int32_t __djinn_json_length(void* value)
{
    yyjson_val* v = (yyjson_val*)value;
    if (yyjson_is_arr(v)) return (int32_t)yyjson_arr_size(v);
    if (yyjson_is_obj(v)) return (int32_t)yyjson_obj_size(v);
    if (yyjson_is_str(v)) return (int32_t)yyjson_get_len(v);
    return 0;
}

int32_t __djinn_json_kind(void* value)
{
    yyjson_val* v = (yyjson_val*)value;
    if (v == NULL || yyjson_is_null(v)) return DJINN_JSON_NULL;
    if (yyjson_is_bool(v)) return DJINN_JSON_BOOL;
    if (yyjson_is_int(v)) return DJINN_JSON_INT;
    if (yyjson_is_real(v)) return DJINN_JSON_REAL;
    if (yyjson_is_str(v)) return DJINN_JSON_STRING;
    if (yyjson_is_arr(v)) return DJINN_JSON_ARRAY;
    return DJINN_JSON_OBJECT;
}

int64_t __djinn_json_int(void* value)
{
    return yyjson_get_sint((yyjson_val*)value);
}

double __djinn_json_real(void* value)
{
    return yyjson_get_num((yyjson_val*)value);
}

int32_t __djinn_json_bool(void* value)
{
    return yyjson_get_bool((yyjson_val*)value) ? 1 : 0;
}

const char* __djinn_json_string(void* value)
{
    const char* text = yyjson_get_str((yyjson_val*)value);
    return text != NULL ? text : "";
}
