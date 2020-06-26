CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -Wpedantic
LDFLAGS  := -L/usr/lib -lstdc++ -lm  -lncurses -lfmt -pthread -lsfml-graphics -lsfml-window -lsfml-system
BUILD    := ./build
OBJ_DIR  := $(BUILD)/objects
APP_DIR  := $(BUILD)/apps
TARGET   := els
INCLUDE  := -Iinclude/
SRC      := $(wildcard *.cpp stepperControl/*.cpp)

OBJECTS  := $(SRC:%.cpp=$(OBJ_DIR)/%.o)

all: build $(APP_DIR)/$(TARGET)
	ctags -R --c++-kinds=+p --fields=+iaS
	cppcheck -q $(SRC)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -o $@ $(LDFLAGS)

$(APP_DIR)/$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $(APP_DIR)/$(TARGET) $^ $(LDFLAGS)

.PHONY: all build clean debug release

build:
	@mkdir -p $(APP_DIR)
	@mkdir -p $(OBJ_DIR)

fake: CXXFLAGS += -DDEBUG -DFAKE -g
fake: all

release: CXXFLAGS += -O3
release: LDFLAGS += -lpigpio
release: all

clean:
	-@rm -rvf $(OBJ_DIR)/*
	-@rm -rvf $(APP_DIR)/*
