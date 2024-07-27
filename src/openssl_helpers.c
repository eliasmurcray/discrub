#include "openssl_helpers.h"

#define RESPONSE_BUFFER_SIZE 1024

struct HTTPResponse *parse_response(const char* response_string, char **error) {
  if (response_string == NULL) {
    *error = "Empty response string";
    return NULL;
  }
  char *headers_end = strstr(response_string, "\r\n\r\n");
  if (headers_end == NULL) {
    *error = "Response not delimited by \\r\\n\\r\\n";
    return NULL;
  }
  struct HTTPResponse *response = malloc(sizeof(struct HTTPResponse));
  if (response == NULL) {
    *error = "Failed to allocate memory for Response";
    return NULL;
  }
  if (sscanf(response_string, "HTTP/1.1 %hd", &(response->code)) == 0) {
    *error = "Failed to get response code from response";
    return NULL;
  }
  const char *content_length_start = strcasestr(response_string, "Content-Length: ");
  if (content_length_start == NULL || content_length_start > headers_end) {
    *error = "Failed to get content length from response";
    return NULL;
  }
  content_length_start += strlen("Content-Length: ");
  if (sscanf(content_length_start, "%d", &(response->length)) == 0) {
    *error = "Failed to get content length from response";
    return NULL;
  }
  response->data = headers_end + strlen("\r\n\r\n");
  return response;
}

char *send_request(BIO *bio, const char *request, char **error) {
  if (BIO_write(bio, request, strlen(request)) <= 0) {
    *error = "Failed to write request to buffer";
    return NULL;
  }
  char *response = NULL;
  int total_size = 0, size;
  char buffer[RESPONSE_BUFFER_SIZE];
  for (;;) {
    size = BIO_read(bio, buffer, RESPONSE_BUFFER_SIZE - 1);
    if (size < 1) break;
    buffer[size] = '\0';
    response = realloc(response, total_size + size + 1);
    if (!response) {
      *error = "Failed to reallocate memory for response";
      return NULL;
    }
    memcpy(response + total_size, buffer, size);
    total_size += size;
  }
  if (response) response[total_size] = '\0';
  return response;
}
