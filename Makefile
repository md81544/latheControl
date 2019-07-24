.PHONY: clean

els: main.cpp steppermotor.cpp gpio.cpp curses.cpp steppermotor.h igpio.h gpio.h curses.h
	g++ -std=c++14 -o els -Wall -Wextra -Wpedantic -Werror *.cpp -lpthread -lpigpio -lncurses

clean:
	rm els
