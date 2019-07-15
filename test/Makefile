.PHONY: clean

test: test.cpp mocksteppermotor.cpp mocksteppermotor.h ../isteppermotor.h igpio.h
	g++ -std=c++14 -o test -Wall -Wextra -Wpedantic -Werror *.cpp -lpthread

clean:
	rm test
