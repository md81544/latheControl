CC      = g++
CFLAGS  = -Wall -Wextra -Wpedantic -Werror -g
LIBS = -lncurses -pthread

SOURCES := $(wildcard *.cpp)
FAKESOURCES := $(shell ls *.cpp | grep -v gpio.cpp)

BINARY = els

.PHONY: all clean fake

all: $(BINARY)


$(BINARY): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(BINARY) $(LIBS) -lpigpio
	ctags -R --c++-kinds=+p --fields=+iaS

# Make binary without need for pigpio lib for testing UI
fake: $(SOURCES)
	$(CC) -DFAKE $(CFLAGS) $(FAKESOURCES) -o $(BINARY) $(LIBS)
	ctags -R --c++-kinds=+p --fields=+iaS

clean:
	rm -f $(BINARY)
	rm -f tags
