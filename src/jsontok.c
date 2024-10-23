#include "jsontok.h"

#include <stdio.h>

static void skip_whitespace(const char **ptr);
static char *jsontok_parse_string(const char **json_string, enum JsonError *error);
static double *jsontok_parse_number(const char **json_string, enum JsonError *error);
static struct JsonObject *jsontok_parse_object(const char **json_string, enum JsonError *error);
static struct JsonArray *jsontok_parse_array(const char **json_string, enum JsonError *error);
static char *jsontok_parse_sub_object(const char **json_string, enum JsonError *error);
static char *jsontok_parse_sub_array(const char **json_string, enum JsonError *error);

const char *jsontok_strerror(enum JsonError error) {
  switch (error) {
    case JSON_ENOERR:
      return "No error";
    case JSON_EFMT:
      return "Invalid format";
    case JSON_ETYPE:
      return "Invalid type";
    case JSON_ENOMEM:
      return "Out of memory";
    default:
      return "Unknown error";
  }
}

void jsontok_free(struct JsonToken *token) {
  if (token == NULL) return;
  switch (token->type) {
    case JSON_ARRAY: {
      size_t i;
      for (i = 0; i < token->as_array->length; i++) {
        jsontok_free(token->as_array->elements[i]);
      }
      free(token->as_array->elements);
      free(token->as_array);
      break;
    }
    case JSON_OBJECT: {
      size_t i;
      for (i = 0; i < token->as_object->count; i++) {
        free(token->as_object->entries[i]->key);
        jsontok_free(token->as_object->entries[i]->value);
        free(token->as_object->entries[i]);
      }
      free(token->as_object->entries);
      free(token->as_object);
      break;
    }
    case JSON_WRAPPED_OBJECT:
    case JSON_WRAPPED_ARRAY:
    case JSON_STRING:
      free(token->as_string);
      break;
    default:
      break;
  }
  free(token);
}

struct JsonToken *jsontok_get(struct JsonObject *object, const char *key) {
  if (!key) {
    return NULL;
  }
  size_t length = strlen(key);
  if (length == 0) {
    return NULL;
  }
  /**
   * Hashes are not used as the number of entries will never exceed the
   * bucket count, therefore complexities are the same with or without
   * hashes (no collisions).
   */
  size_t i;
  for (i = 0; i < object->count; i++) {
    if (strncmp(object->entries[i]->key, key, length) == 0) {
      return object->entries[i]->value;
    }
  }
  return NULL;
}

struct JsonToken *jsontok_parse(const char *json_string, enum JsonError *error) {
  if (!json_string || strlen(json_string) == 0) {
    *error = JSON_EFMT;
    return NULL;
  }
  struct JsonToken *token = malloc(sizeof(struct JsonToken));
  if (!token) {
    *error = JSON_ENOMEM;
    return NULL;
  }
  skip_whitespace(&json_string);
  if (!memcmp(json_string, "true", 4)) {
    token->type = JSON_BOOLEAN;
    token->as_boolean = 1;
    json_string += 4;
  } else if (!memcmp(json_string, "false", 5)) {
    token->type = JSON_BOOLEAN;
    token->as_boolean = 0;
    json_string += 5;
  } else if (!memcmp(json_string, "null", 4)) {
    token->type = JSON_NULL;
    json_string += 4;
  } else {
    switch (*json_string) {
      case '"': {
        char *str = jsontok_parse_string(&json_string, error);
        if (!str) {
          free(token);
          return NULL;
        }
        token->type = JSON_STRING;
        token->as_string = str;
        break;
      }
      case '{': {
        struct JsonObject *object = jsontok_parse_object(&json_string, error);
        if (!object) {
          free(token);
          return NULL;
        }
        token->type = JSON_OBJECT;
        token->as_object = object;
        break;
      }
      case '[': {
        struct JsonArray *array = jsontok_parse_array(&json_string, error);
        if (!array) {
          free(token);
          return NULL;
        }
        token->type = JSON_ARRAY;
        token->as_array = array;
        break;
      }
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '-': {
        double *number = jsontok_parse_number(&json_string, error);
        if (!number) {
          free(token);
          return NULL;
        }
        token->type = JSON_NUMBER;
        token->as_number = *number;
        free(number);
        break;
      }
      default:
        free(token);
        *error = JSON_EFMT;
        return NULL;
    }
  }
  skip_whitespace(&json_string);
  if (*json_string != '\0') {
    free(token);
    *error = JSON_EFMT;
    return NULL;
  }
  return token;
}

static void skip_whitespace(const char **ptr) {
  while (**ptr == '\t' || **ptr == '\r' || **ptr == '\n' || **ptr == ' ') {
    (*ptr)++;
  }
}

static struct JsonToken *jsontok_parse_value(const char **ptr, enum JsonError *error) {
  struct JsonToken *token = malloc(sizeof(struct JsonToken));
  if (!token) {
    *error = JSON_ENOMEM;
    return NULL;
  }
  if (!memcmp(*ptr, "true", 4)) {
    token->type = JSON_BOOLEAN;
    token->as_boolean = 1;
    *ptr += 4;
  } else if (!memcmp(*ptr, "false", 5)) {
    token->type = JSON_BOOLEAN;
    token->as_boolean = 0;
    *ptr += 5;
  } else if (!memcmp(*ptr, "null", 4)) {
    token->type = JSON_NULL;
    *ptr += 4;
  } else {
    switch (**ptr) {
      case '"': {
        char *str = jsontok_parse_string(ptr, error);
        if (!str) {
          free(token);
          return NULL;
        }
        token->type = JSON_STRING;
        token->as_string = str;
        break;
      }
      case '{': {
        char *str = jsontok_parse_sub_object(ptr, error);
        if (!str) {
          free(token);
          return NULL;
        }
        token->type = JSON_WRAPPED_OBJECT;
        token->as_string = str;
        break;
      }
      case '[': {
        char *str = jsontok_parse_sub_array(ptr, error);
        if (!str) {
          free(token);
          return NULL;
        }
        token->type = JSON_WRAPPED_ARRAY;
        token->as_string = str;
        break;
      }
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '-': {
        double *number = jsontok_parse_number(ptr, error);
        if (!number) {
          free(token);
          return NULL;
        }
        token->type = JSON_NUMBER;
        token->as_number = *number;
        free(number);
        break;
      }
      default:
        free(token);
        *error = JSON_EFMT;
        return NULL;
    }
  }
  return token;
}

static char *jsontok_parse_string(const char **json_string, enum JsonError *error) {
  const char *start = *json_string;
  char *result = NULL;
  size_t length = 0;
  if (*start != '"') {
    *error = JSON_EFMT;
    return NULL;
  }
  start++;
  while (*start != '"') {
    if (*start == '\0') {
      *error = JSON_EFMT;
      return NULL;
    }
    if (*start == '\\') {
      start++;
      if (*start == 'u') {
        start++;
        unsigned int unicode_value = 0;
        size_t i = 0;
        for (; i < 4; i++) {
          char hex_digit = *start++;
          unicode_value <<= 4;
          if (hex_digit >= '0' && hex_digit <= '9') {
            unicode_value += hex_digit - '0';
          } else if (hex_digit >= 'a' && hex_digit <= 'f') {
            unicode_value += hex_digit - 'a' + 10;
          } else if (hex_digit >= 'A' && hex_digit <= 'F') {
            unicode_value += hex_digit - 'A' + 10;
          } else {
            *error = JSON_EFMT;
            return NULL;
          }
        }
        if (unicode_value <= 0x7F) {
          result = realloc(result, length + 1);
          result[length++] = (char)unicode_value;
        } else if (unicode_value <= 0x7FF) {
          result = realloc(result, length + 2);
          result[length++] = 0xC0 | ((unicode_value >> 6) & 0x1F);
          result[length++] = 0x80 | (unicode_value & 0x3F);
        } else if (unicode_value <= 0xFFFF) {
          result = realloc(result, length + 3);
          result[length++] = 0xE0 | ((unicode_value >> 12) & 0x0F);
          result[length++] = 0x80 | ((unicode_value >> 6) & 0x3F);
          result[length++] = 0x80 | (unicode_value & 0x3F);
        } else if (unicode_value <= 0x10FFFF) {
          result = realloc(result, length + 4);
          result[length++] = 0xF0 | ((unicode_value >> 18) & 0x07);
          result[length++] = 0x80 | ((unicode_value >> 12) & 0x3F);
          result[length++] = 0x80 | ((unicode_value >> 6) & 0x3F);
          result[length++] = 0x80 | (unicode_value & 0x3F);
        }
      } else {
        switch (*start) {
          case 'b':
            result = realloc(result, length + 1);
            result[length++] = '\b';
            break;
          case 'f':
            result = realloc(result, length + 1);
            result[length++] = '\f';
            break;
          case 'n':
            result = realloc(result, length + 1);
            result[length++] = '\n';
            break;
          case 'r':
            result = realloc(result, length + 1);
            result[length++] = '\r';
            break;
          case 't':
            result = realloc(result, length + 1);
            result[length++] = '\t';
            break;
          case '"':
            result = realloc(result, length + 1);
            result[length++] = '"';
            break;
          case '\\':
            result = realloc(result, length + 1);
            result[length++] = '\\';
            break;
          default:
            *error = JSON_EFMT;
            return NULL;
        }
        start++;
      }
    } else {
      result = realloc(result, length + 1);
      result[length++] = *start++;
    }
  }
  result = realloc(result, length + 1);
  result[length] = '\0';
  *json_string = start + 1;
  return result;
}

static double *jsontok_parse_number(const char **json_string, enum JsonError *error) {
  char *ptr = (char *)(*json_string + 1);
  unsigned char needs_digit = **json_string == '-';
  unsigned char decimal = 1;
  while (1) {
    char c = *ptr++;
    if (c >= '0' && c <= '9') continue;
    if (needs_digit) {
      *error = JSON_EFMT;
      return NULL;
    }
    if (decimal && c == '.') {
      decimal = 0;
      continue;
    }
    break;
  }
  ptr--;
  size_t length = ptr - *json_string;
  char *substr = malloc(length + 1);
  if (!substr) {
    *error = JSON_ENOMEM;
    return NULL;
  }
  strncpy(substr, *json_string, length);
  substr[length] = '\0';
  errno = 0;
  char *endptr = NULL;
  double *number = malloc(sizeof(double));
  if (!number) {
    free(substr);
    return NULL;
  }
  *number = strtod(substr, &endptr);
  if (errno || *endptr != '\0') {
    *error = JSON_EFMT;
    free(number);
    free(substr);
    return NULL;
  }
  free(substr);
  *json_string = ptr;
  return number;
}

static struct JsonObject *jsontok_parse_object(const char **json_string, enum JsonError *error) {
  struct JsonObject *object = malloc(sizeof(struct JsonObject));
  if (!object) {
    *error = JSON_ENOMEM;
    return NULL;
  }
  object->count = 0;
  object->entries = NULL;
  const char *ptr = (char *)(*json_string + 1);
  while (*ptr != '}') {
    skip_whitespace(&ptr);
    if (*ptr != '"') {
      size_t i = 0;
      for (; i < object->count; i++) {
        free(object->entries[i]->key);
        jsontok_free(object->entries[i]->value);
      }
      free(object->entries);
      free(object);
      *error = JSON_EFMT;
      return NULL;
    }
    char *key = jsontok_parse_string((const char **)&ptr, error);
    if (!key) {
      size_t i = 0;
      for (; i < object->count; i++) {
        free(object->entries[i]->key);
        jsontok_free(object->entries[i]->value);
      }
      free(object->entries);
      free(object);
      *error = JSON_ENOMEM;
      return NULL;
    }
    skip_whitespace(&ptr);
    if (*ptr != ':') {
      size_t i = 0;
      for (; i < object->count; i++) {
        free(object->entries[i]->key);
        jsontok_free(object->entries[i]->value);
      }
      free(object->entries);
      free(object);
      free(key);
      *error = JSON_EFMT;
      return NULL;
    }
    ptr++;
    skip_whitespace(&ptr);
    struct JsonToken *token = jsontok_parse_value((const char **)&ptr, error);
    if (!token) {
      size_t i = 0;
      for (; i < object->count; i++) {
        free(object->entries[i]->key);
        jsontok_free(object->entries[i]->value);
      }
      free(object->entries);
      free(object);
      free(key);
      return NULL;
    }
    struct JsonEntry *entry = malloc(sizeof(struct JsonEntry));
    if (!entry) {
      size_t i = 0;
      for (; i < object->count; i++) {
        free(object->entries[i]->key);
        jsontok_free(object->entries[i]->value);
      }
      free(object->entries);
      free(object);
      free(key);
      jsontok_free(token);
      *error = JSON_ENOMEM;
      return NULL;
    }
    entry->key = key;
    entry->value = token;
    struct JsonEntry **new_entries = realloc(object->entries, (object->count + 1) * sizeof(struct JsonEntry *));
    if (!new_entries) {
      size_t i = 0;
      for (; i < object->count; i++) {
        free(object->entries[i]->key);
        jsontok_free(object->entries[i]->value);
      }
      free(object->entries);
      free(object);
      free(key);
      jsontok_free(token);
      free(entry);
      *error = JSON_ENOMEM;
      return NULL;
    }
    object->entries = new_entries;
    object->entries[object->count++] = entry;
    skip_whitespace(&ptr);
    if (*ptr == ',') ptr++;
    skip_whitespace(&ptr);
  }
  *json_string = ptr + 1;
  return object;
}

static struct JsonArray *jsontok_parse_array(const char **json_string, enum JsonError *error) {
  struct JsonArray *array = malloc(sizeof(struct JsonArray));
  if (!array) {
    *error = JSON_ENOMEM;
    return NULL;
  }
  array->length = 0;
  array->elements = NULL;
  const char *ptr = *json_string + 1;
  while (*ptr != ']') {
    skip_whitespace(&ptr);
    if (*ptr == '\0') {
      size_t i = 0;
      for (; i < array->length; i++) jsontok_free(array->elements[i]);
      free(array->elements);
      free(array);
      *error = JSON_EFMT;
      return NULL;
    }
    struct JsonToken *token = jsontok_parse_value((const char **)&ptr, error);
    if (!token) {
      size_t i = 0;
      for (; i < array->length; i++) jsontok_free(array->elements[i]);
      free(array->elements);
      free(array);
      return NULL;
    }
    void *new_elements = realloc(array->elements, (array->length + 1) * sizeof(struct JsonToken));
    if (!new_elements) {
      size_t i = 0;
      for (; i < array->length; i++) jsontok_free(array->elements[i]);
      free(array->elements);
      free(array);
      *error = JSON_ENOMEM;
      return NULL;
    }
    array->elements = new_elements;
    array->elements[array->length++] = token;
    skip_whitespace(&ptr);
    if (*ptr == ',') ptr++;
    skip_whitespace(&ptr);
  }
  *json_string = ptr + 1;
  return array;
}

static char *jsontok_parse_sub_object(const char **json_string, enum JsonError *error) {
  char *ptr = (char *)(*json_string + 1);
  size_t counter = 1;
  while (counter > 1 || *ptr != '}') {
    if (*ptr == '\0') {
      *error = JSON_EFMT;
      return NULL;
    }
    if (*ptr == '{')
      counter++;
    else if (*ptr == '}')
      counter--;
    ptr += (*ptr == '\\') + 1;
  }
  size_t length = ptr - *json_string + 1;
  char *substr = malloc(length + 1);
  if (!substr) {
    *error = JSON_ENOMEM;
    return NULL;
  }
  strncpy(substr, *json_string, length);
  substr[length] = '\0';
  *json_string = ptr + 1;
  return substr;
}

static char *jsontok_parse_sub_array(const char **json_string, enum JsonError *error) {
  const char *ptr = *json_string;
  if (*ptr != '[') {
    *error = JSON_EFMT;
    return NULL;
  }
  ptr++;
  size_t counter = 1;
  while (counter > 0) {
    if (*ptr == '\0') {
      *error = JSON_EFMT;
      return NULL;
    }
    if (*ptr == '[')
      counter++;
    else if (*ptr == ']')
      counter--;
    ptr++;
    if (*(ptr - 1) == '\\' && *ptr != '\0') ptr++;
  }
  size_t length = ptr - *json_string;
  char *substr = malloc(length + 1);
  if (!substr) {
    *error = JSON_ENOMEM;
    return NULL;
  }
  strncpy(substr, *json_string, length);
  substr[length] = '\0';
  *json_string = ptr;
  return substr;
}
