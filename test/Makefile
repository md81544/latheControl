.PHONY: clean

test: test.cpp ../stepperControl/steppermotor.cpp ../stepperControl/steppermotor.h ../stepperControl/mockgpio.h
	g++ -std=c++14 -g -o test -Wall -Wextra -Wpedantic -Werror -Wno-psabi -I.. \
		../stepperControl/steppermotor.cpp ../rotaryencoder.cpp ../log.cpp *.cpp -lpthread
	./test -d yes

clean:
	rm test
