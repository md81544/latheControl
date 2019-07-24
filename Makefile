.PHONY: fake, clean

els: main.cpp steppermotor.cpp gpio.cpp curses.cpp steppermotor.h igpio.h gpio.h curses.h
	g++ -std=c++14 -o els -Wall -Wextra -Wpedantic -Werror *.cpp -lpthread -lpigpio -lncurses

fake: main.cpp steppermotor.cpp gpio.cpp curses.cpp steppermotor.h igpio.h test/mockgpio.h curses.h
	g++ -DFAKE -std=c++14 -o els -Wall -Wextra -Wpedantic -Werror curses.cpp main.cpp steppermotor.cpp ui.cpp -lpthread -lncurses


clean:
	rm els
