.PHONY: clean

test: test.cpp ../steppermotor.cpp ../steppermotor.h mockgpio.h
	g++ -std=c++14 -o test -Wall -Wextra -Wpedantic -Werror ../steppermotor.cpp *.cpp -lpthread

clean:
	rm test
