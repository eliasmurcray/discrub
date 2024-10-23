CC = gcc
CFLAGS = -std=c89 -Ofast -Wall -Wextra
LIBS = `pkg-config openssl --cflags --libs`
TARGET = build/discrub
SRCS = $(wildcard src/**.c)
NCLUDE = -Iinclude

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p $(dir $(TARGET))
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $^ $(LIBS)

clean:
	rm -rf $(dir $(TARGET))

.PHONY: all clean
