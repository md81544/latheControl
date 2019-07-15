.PHONY: clean

els: main.cpp steppermotor.cpp steppermotor.h isteppermotor.h igpio.h
	g++ -std=c++14 -o els -Wall -Wextra -Wpedantic -Werror *.cpp -lpthread -lpigpio

clean:
	rm els
