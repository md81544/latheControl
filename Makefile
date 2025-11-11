MAKEFLAGS += --silent

.PHONY: all debug fake clean release test sub

all:
	./m.sh

debug:
	./m.sh debug

fake:
	./m.sh fake

clean:
	./m.sh clean

release:
	./m.sh release

test:
	./m.sh test

sub:
	./m.sh sub

latest:
	./m.sh latest
