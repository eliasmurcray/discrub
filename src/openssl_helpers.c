#include "openssl_helpers.h"

#define RESPONSE_BUFFER_SIZE 1024

struct HTTPResponse *http_request(BIO *connection, const char *request,
                                  enum HTTPError *error) {
  BIO_reset(connection);
  if (BIO_write(connection, request, strlen(request)) <= 0) {
    *error = HTTP_EBIO;
    return NULL;
  }

  char *raw_response = NULL;
  int total_size = 0, size;
  char buffer[RESPONSE_BUFFER_SIZE];

  for (;;) {
    size = BIO_read(connection, buffer, RESPONSE_BUFFER_SIZE - 1);
    if (size < 1) break;
    buffer[size] = '\0';

    raw_response = realloc(raw_response, total_size + size + 1);
    if (!raw_response) {
      *error = HTTP_ENOMEM;
      return NULL;
    }
    memcpy(raw_response + total_size, buffer, size);
    total_size += size;
  }

  if (raw_response) raw_response[total_size] = '\0';

  if (raw_response == NULL) {
    *error = HTTP_EPARSE;
    return NULL;
  }

  char *headers_end = strstr(raw_response, "\r\n\r\n");
  if (headers_end == NULL) {
    *error = HTTP_EPARSE;
    free(raw_response);
    return NULL;
  }

  struct HTTPResponse *parsed_response = malloc(sizeof(struct HTTPResponse));
  if (parsed_response == NULL) {
    *error = HTTP_ENOMEM;
    free(raw_response);
    return NULL;
  }

  if (sscanf(raw_response, "HTTP/1.1 %hd", &(parsed_response->code)) == 0) {
    *error = HTTP_EPARSE;
    free(raw_response);
    free(parsed_response);
    return NULL;
  }

  const char *content_length_start =
      strcasestr(raw_response, "Content-Length: ");
  if (content_length_start && content_length_start <= headers_end) {
    content_length_start += strlen("Content-Length: ");
    sscanf(content_length_start, "%d", &(parsed_response->length));
  }

  parsed_response->data = headers_end + strlen("\r\n\r\n");

  return parsed_response;
}

const char *http_strerror(enum HTTPError *error) {
  switch (*error) {
    case HTTP_ENOMEM: return "Memory allocation failed";
    case HTTP_EBIO: return "BIO operation failed";
    case HTTP_EPARSE: return "Failed to parse response";
    default: return "Unknown HTTP error";
  }
}
