.PHONY: clean

test: test.cpp ../steppermotor.cpp ../steppermotor.h ../mockgpio.h
	g++ -std=c++14 -g -o test -Wall -Wextra -Wpedantic -Werror -Wno-psabi -I.. ../steppermotor.cpp ../rotaryencoder.cpp ../log.cpp *.cpp -lpthread
	./test -d yes

clean:
	rm test
