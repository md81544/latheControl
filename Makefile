.PHONY: clean

els: main.cpp steppermotor.cpp gpio.cpp steppermotor.h igpio.h gpio.h
	g++ -std=c++14 -o els -Wall -Wextra -Wpedantic -Werror *.cpp -lpthread -lpigpio

clean:
	rm els
