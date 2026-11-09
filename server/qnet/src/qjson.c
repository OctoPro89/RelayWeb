#include <qnet/qjson.h>

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

/* ============================================================
   Utilities
   ============================================================ */

static void skip_whitespace(const char** p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static b8 is_number_start(char c) {
    return (c == '-') || isdigit((unsigned char)c);
}

static b8 match_literal(const char** p, const char* lit) {
    size_t len = strlen(lit);
    if (strncmp(*p, lit, len) == 0) {
        *p += len;
        return true;
    }
    return false;
}

static b8 match_null(const char** p) {
    skip_whitespace(p);
    return match_literal(p, "null");
}

/* ============================================================
   Error helpers
   ============================================================ */

static qjson_result qjson_error_set(
    qjson_error* err,
    qjson_result code,
    const char* field,
    const char* pos
) {
    if (err) {
        err->code = code;
        err->field = field;
        err->position = pos;
    }
    return code;
}

/* ============================================================
   Value skipping (for unknown fields)
   ============================================================ */

static void skip_string(const char** p) {
    (*p)++; /* skip " */
    while (**p && **p != '"') {
        if (**p == '\\' && (*p)[1]) {
            (*p)++;
        }
        (*p)++;
    }
    if (**p == '"') {
        (*p)++;
    }
}

static void skip_value(const char** p) {
    skip_whitespace(p);

    if (**p == '"') {
        skip_string(p);
    }
    else if (**p == '{') {
        int depth = 1;
        (*p)++;
        while (**p && depth) {
            if (**p == '{') depth++;
            else if (**p == '}') depth--;
            (*p)++;
        }
    }
    else if (**p == '[') {
        int depth = 1;
        (*p)++;
        while (**p && depth) {
            if (**p == '[') depth++;
            else if (**p == ']') depth--;
            (*p)++;
        }
    }
    else {
        /* number, true, false, null */
        while (**p && **p != ',' && **p != '}' && **p != ']') {
            (*p)++;
        }
    }
}

/* ============================================================
   Primitive parsing
   ============================================================ */

static i64 parse_i64(const char** p) {
    skip_whitespace(p);

    int sign = 1;
    if (**p == '-') {
        sign = -1;
        (*p)++;
    }

    i64 val = 0;
    while (isdigit((unsigned char)**p)) {
        i64 digit = **p - '0';

        if (val > (INT64_MAX - digit) / 10) {
            val = INT64_MAX;
            while (isdigit((unsigned char)**p)) {
                (*p)++;
            }
            return sign > 0 ? val : -val;
        }

        val = val * 10 + digit;
        (*p)++;
    }

    return val * sign;
}

static float parse_f32(const char** p) {
    skip_whitespace(p);
    float v = (float)atof(*p);
    while (**p && (isdigit((unsigned char)**p) ||
        **p == '-' || **p == '+' ||
        **p == '.' || **p == 'e' || **p == 'E')) {
        (*p)++;
    }
    return v;
}

static b8 parse_bool(const char** p, b8* out) {
    skip_whitespace(p);
    if (match_literal(p, "true")) {
        *out = true;
        return true;
    }
    if (match_literal(p, "false")) {
        *out = false;
        return true;
    }
    return false;
}

static void parse_string(const char** p, char* out, size_t max_len) {
    size_t i = 0;

    (*p)++; /* skip " */
    while (**p && **p != '"') {
        char c = **p;

        if (c == '\\' && (*p)[1]) {
            (*p)++;
            c = **p;
        }

        if (i + 1 < max_len) {
            out[i++] = c;
        }

        (*p)++;
    }

    out[i] = '\0';

    if (**p == '"') {
        (*p)++;
    }
}

static qjson_result parse_string_ex(
    const char** p,
    char* out,
    size_t max_len,
    const qjson_field* field,
    qjson_error* err
) {
    size_t i = 0;

    if (**p != '"') {
        return qjson_error_set(err, QJSON_ERR_TYPE, field->name, *p);
    }

    (*p)++; /* skip opening quote */

    memset(out, '\0', field->string_max_len);

    while (**p && **p != '"') {
        char c = **p;

        if (c == '\\') {
            (*p)++;
            c = **p;

            switch (c) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;

            default:
                return qjson_error_set(
                    err,
                    QJSON_ERR_STRING_ESCAPE,
                    field->name,
                    *p
                );
            }
        }

        if (i + 1 >= max_len) {
            if (!(field->flags & QJSON_F_STRING_TRUNCATE_OK)) {
                return qjson_error_set(
                    err,
                    QJSON_ERR_STRING_TRUNCATED,
                    field->name,
                    *p
                );
            }
        }
        else {
            out[i++] = c;
        }

        (*p)++;
    }

    out[i] = '\0';

    if (**p != '"') {
        return qjson_error_set(err, QJSON_ERR_SYNTAX, field->name, *p);
    }

    (*p)++; /* skip closing quote */
    return QJSON_OK;
}

/* ============================================================
   Presence handling
   ============================================================ */

static void qjson_set_present(void* obj, const qjson_field* f, b8 present) {
    if (f->present_offset) {
        *(b8*)((char*)obj + f->present_offset) = present;
    }
}

/* ============================================================
   Forward declarations
   ============================================================ */

static qjson_result qjson_deserialize_object(
    void* obj,
    const qjson_field* fields,
    u32 field_count,
    const char** json,
    qjson_error* err
);

static qjson_result qjson_parse_array(
    const char** p,
    void* array_base,
    const qjson_field* field,
    qjson_error* err
);

/* ============================================================
   Array parsing
   ============================================================ */

static qjson_result qjson_parse_array(
    const char** p,
    void* array_base,
    const qjson_field* field,
    qjson_error* err
) {
    skip_whitespace(p);

    if (**p != '[') {
        return qjson_error_set(err, QJSON_ERR_TYPE, field->name, *p);
    }

    (*p)++; /* skip '[' */

    void** array_ptr = (void**)array_base;

    size_t capacity = 4;
    size_t count = 0;

    void* array = calloc(1, capacity * field->array_elem_size);
    if (!array) return qjson_error_set(err, QJSON_ERR_OVERFLOW, field->name, *p);

    *array_ptr = array;

    while (**p && **p != ']') {
        skip_whitespace(p);

        if (count >= capacity) {

            size_t new_capacity = capacity * 2;

            if (field->array_max_count && new_capacity > field->array_max_count)
                new_capacity = field->array_max_count;

            void* new_array = realloc(array, new_capacity * field->array_elem_size);
            if (!new_array)
                return qjson_error_set(err, QJSON_ERR_OVERFLOW, field->name, *p);

            array = new_array;
            *array_ptr = array;
            capacity = new_capacity;
        }

        if (field->array_max_count && count >= field->array_max_count) {
            if (field->flags & QJSON_F_ARRAY_TRUNCATE_OK) {
                skip_value(p);
            }
            else {
                return qjson_error_set(
                    err,
                    QJSON_ERR_ARRAY_OVERFLOW,
                    field->name,
                    *p
                );
            }
        }
        else {
            void* elem = (char*)array + count * field->array_elem_size;

            qjson_result r = QJSON_OK;

            switch (field->array_elem_type) {
            case QJSON_TYPE_I32:
                *(i32*)elem = (i32)parse_i64(p);
                break;

            case QJSON_TYPE_I64:
                *(i64*)elem = parse_i64(p);
                break;

            case QJSON_TYPE_F32:
                *(float*)elem = parse_f32(p);
                break;

            case QJSON_TYPE_BOOL: {
                b8 v;
                if (!parse_bool(p, &v))
                    return qjson_error_set(err, QJSON_ERR_TYPE, field->name, *p);
                *(b8*)elem = v;
            } break;

            case QJSON_TYPE_STRING:
                r = parse_string_ex(
                    p,
                    (char*)elem,
                    field->string_max_len,
                    field,
                    err
                );
                if (r != QJSON_OK) return r;
                break;

            case QJSON_TYPE_STRUCT:
                if (**p != '{')
                    return qjson_error_set(err, QJSON_ERR_TYPE, field->name, *p);
                (*p)++;
                r = qjson_deserialize_object(
                    elem,
                    field->subfields,
                    field->subfield_count,
                    p,
                    err
                );
                if (r != QJSON_OK) return r;
                if (**p == '}') (*p)++;
                break;

            default:
                return qjson_error_set(err, QJSON_ERR_TYPE, field->name, *p);
            }

            count++;
        }

        skip_whitespace(p);
        if (**p == ',') {
            (*p)++;
        }
    }

    if (**p != ']') {
        return qjson_error_set(err, QJSON_ERR_SYNTAX, field->name, *p);
    }

    (*p)++; /* skip ']' */

    if (field->array_count_offset) {
        char* struct_base = (char*)array_base - field->offset;
        size_t* count_ptr = (size_t*)(struct_base + field->array_count_offset);
        *count_ptr = count;
    }

    return QJSON_OK;
}

/* ============================================================
   Object parsing
   ============================================================ */

static qjson_result qjson_deserialize_object(
    void* obj,
    const qjson_field* fields,
    u32 field_count,
    const char** json,
    qjson_error* err
) {
    const char* p = *json;

    while (*p && *p != '}') {
        skip_whitespace(&p);

        if (*p != '"') {
            return qjson_error_set(err, QJSON_ERR_SYNTAX, NULL, p);
        }

        char field_name[128];
        parse_string(&p, field_name, sizeof(field_name));

        skip_whitespace(&p);
        if (*p != ':') {
            return qjson_error_set(err, QJSON_ERR_SYNTAX, field_name, p);
        }
        p++;

        skip_whitespace(&p);

        const qjson_field* f = NULL;
        for (u32 i = 0; i < field_count; ++i) {
            if (strcmp(fields[i].name, field_name) == 0) {
                f = &fields[i];
                break;
            }
        }

        if (!f) {
            skip_value(&p);
        }
        else {
            void* member = (char*)obj + f->offset;

            if ((f->flags & QJSON_F_NULLABLE) && match_null(&p)) {
                switch (f->type) {
                case QJSON_TYPE_STRING:
                    memset(member, '\0', f->string_max_len);
                    break;

                case QJSON_TYPE_ARRAY:
                    *(void**)member = NULL;  // mark array pointer as NULL
                    if (f->array_count_offset) {
                        char* struct_base = (char*)member - f->offset;
                        size_t* count_ptr = (size_t*)(struct_base + f->array_count_offset);
                        *count_ptr = 0;       // zero the count
                    }
                    break;

                default:
                    break;
                }

                qjson_set_present(obj, f, false);
            }
            else {
                qjson_result r = QJSON_OK;

                switch (f->type) {
                case QJSON_TYPE_I32:
                    if (!is_number_start(*p))
                        return qjson_error_set(err, QJSON_ERR_TYPE, f->name, p);
                    *(i32*)member = (i32)parse_i64(&p);
                    break;

                case QJSON_TYPE_I64:
                    if (!is_number_start(*p))
                        return qjson_error_set(err, QJSON_ERR_TYPE, f->name, p);
                    *(i64*)member = parse_i64(&p);
                    break;

                case QJSON_TYPE_F32:
                    if (!is_number_start(*p))
                        return qjson_error_set(err, QJSON_ERR_TYPE, f->name, p);
                    *(float*)member = parse_f32(&p);
                    break;

                case QJSON_TYPE_BOOL: {
                    b8 v;
                    if (!parse_bool(&p, &v))
                        return qjson_error_set(err, QJSON_ERR_TYPE, f->name, p);
                    *(b8*)member = v;
                } break;

                case QJSON_TYPE_STRING:
                    if (*p != '"')
                        return qjson_error_set(err, QJSON_ERR_TYPE, f->name, p);

                    qjson_result r = parse_string_ex(
                        &p,
                        (char*)member,
                        f->string_max_len,
                        f,
                        err
                    );
                    if (r != QJSON_OK) return r;
                    break;

                case QJSON_TYPE_STRUCT:
                    if (*p != '{')
                        return qjson_error_set(err, QJSON_ERR_TYPE, f->name, p);
                    p++;
                    r = qjson_deserialize_object(
                        member,
                        f->subfields,
                        f->subfield_count,
                        &p,
                        err
                    );
                    if (r != QJSON_OK) return r;
                    if (*p == '}') p++;
                    break;

                case QJSON_TYPE_ARRAY:
                    r = qjson_parse_array(&p, member, f, err);
                    if (r != QJSON_OK) return r;
                    break;

                default:
                    return qjson_error_set(err, QJSON_ERR_TYPE, f->name, p);
                }

                qjson_set_present(obj, f, true);
            }
        }

        skip_whitespace(&p);
        if (*p == ',') {
            p++;
        }
    }

    if (*p == '}') {
        p++;
    }

    *json = p;
    return QJSON_OK;
}

/* ============================================================
   Public entry point
   ============================================================ */

QNETAPI qjson_result qjson_deserialize(
    void* obj,
    const qjson_field* fields,
    u32 field_count,
    const char* json,
    qjson_error* err
) {
    const char* p = json;
    skip_whitespace(&p);

    if (*p != '{') {
        return qjson_error_set(err, QJSON_ERR_SYNTAX, NULL, p);
    }

    p++;
    return qjson_deserialize_object(obj, fields, field_count, &p, err);
}
