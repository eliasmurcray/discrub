#include "input_helpers.h"

char *load_file_as_string(const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long len = ftell(file);
  fseek(file, 0, SEEK_SET);
  char *buffer = (char *)malloc(len + 1);
  if (buffer) {
    fread(buffer, 1, len, file);
    buffer[len] = '\0';
  }
  fclose(file);
  return buffer;
}

char *get_password() {
  char *password = NULL;
  size_t index = 0;
  int ch;
  password = malloc(1);
  if (!password) exit(1);
#ifdef _WIN32
  while ((ch = _getch()) != '\r') {
    if (ch == 8 && index > 0) {
      printf("\b \b");
      index--;
    } else if (ch != 8) {
      password[index++] = (char)ch;
      printf("*");
      password = realloc(password, index + 1);
      if (!password) exit(1);
    }
  }
#else
  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  while ((ch = getchar()) != '\n') {
    password[index++] = (char)ch;
    password = realloc(password, index + 1);
    if (!password) exit(1);
  }
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
  password[index] = '\0';
  printf("\n");
  return password;
}
