#pragma once

#include "common.h"
#include <stddef.h>

typedef enum {
    QJSON_OK = 0,

    QJSON_ERR_SYNTAX,
    QJSON_ERR_TYPE,
    QJSON_ERR_RANGE,
    QJSON_ERR_OVERFLOW,
    QJSON_ERR_UNKNOWN_FIELD,
    QJSON_ERR_STRING_TRUNCATED,
    QJSON_ERR_STRING_ESCAPE,
    QJSON_ERR_ARRAY_OVERFLOW
} qjson_result;

typedef enum {
    QJSON_TYPE_I32,
    QJSON_TYPE_I64,
    QJSON_TYPE_F32,
    QJSON_TYPE_BOOL,
    QJSON_TYPE_STRING,
    QJSON_TYPE_STRUCT,
    QJSON_TYPE_ARRAY
} qjson_type;

typedef enum {
    QJSON_F_NONE = 0,
    QJSON_F_NULLABLE = 1 << 0,
    QJSON_F_STRING_TRUNCATE_OK = 1 << 1,
    QJSON_F_ARRAY_TRUNCATE_OK = 1 << 2,
    QJSON_F_ARRAY_ERROR_ON_OVER = 1 << 3
} qjson_flags;

typedef struct qjson_field {
    const char* name;
    qjson_type type;
    qjson_flags flags;
    size_t offset;
    size_t present_offset;
    const struct qjson_field* subfields;
    u32 subfield_count;
    size_t struct_size;
    size_t string_max_len;
    qjson_type array_elem_type;
    size_t array_elem_size;
    size_t array_max_count;
    size_t array_count_offset;
} qjson_field;

typedef struct {
    qjson_result code;
    const char* field;     // field name (if known)
    const char* position;  // pointer into JSON text
} qjson_error;

QNETAPI qjson_result qjson_deserialize(
    void* obj,
    const qjson_field* fields,
    u32 field_count,
    const char* json,
    qjson_error* err /* nullable */
);

#define QJSON_FIELD_NULLABLE_BOOL(json_name, parent_struct) \
{ \
    #json_name, \
    QJSON_TYPE_BOOL, \
    QJSON_F_NULLABLE, \
    offsetof(parent_struct, json_name), \
    offsetof(parent_struct, json_name##_present), \
    NULL, \
    0, \
    0, \
    0, \
    0, \
    0, \
    0, \
    0 \
}

#define QJSON_FIELD_NULLABLE_STRING(json_name, parent_struct, max_length) \
{ \
    #json_name, \
    QJSON_TYPE_STRING, \
    QJSON_F_NULLABLE, \
    offsetof(parent_struct, json_name), \
    offsetof(parent_struct, json_name##_present), \
    NULL, \
    0, \
    0, \
    max_length, \
    0, \
    0, \
    0, \
    0 \
}

#define QJSON_FIELD_STRUCT_ARRAY(json_name, flags, parent_struct, struct_type, struct_subfields, max_count, max_count_name) \
{ \
    #json_name, \
    QJSON_TYPE_ARRAY, \
    flags, \
    offsetof(parent_struct, json_name), \
    0, \
    struct_subfields, \
    _countof(struct_subfields), \
    sizeof(struct_type), \
    0, \
    QJSON_TYPE_STRUCT, \
    sizeof(struct_type), \
    max_count, \
    offsetof(parent_struct, max_count_name) \
}