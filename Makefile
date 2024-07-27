CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Ofast
LIBS = `pkg-config openssl --cflags --libs`
TARGET = build/main
SRCS = $(wildcard src/**.c)
INCLUDE = -Iinclude

all: $(TARGET)

$(TARGET): $(SRCS)
	mkdir -p $(dir $(TARGET))
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $^ $(LIBS)

clean:
	rm -rf $(TARGET) 

.PHONY: all clean
