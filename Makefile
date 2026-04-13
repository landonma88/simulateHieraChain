# Makefile for building the shard project

# Compiler and Linker
CXX = g++
CXXFLAGS = -std=c++11 -g -Wall
LDFLAGS = -pthread

# Directories (optional)
SRC_DIR = ./source
OBJ_DIR = ./source

# Source files
SRCS = $(SRC_DIR)/main.cpp $(SRC_DIR)/common.cpp $(SRC_DIR)/shard.cpp $(SRC_DIR)/shardsManager.cpp

# Object files
OBJS = $(OBJ_DIR)/main.o $(OBJ_DIR)/common.o $(OBJ_DIR)/shard.o $(OBJ_DIR)/shardsManager.o

# Output executable
EXEC = hierachain

# Targets
all: $(EXEC)

# Link the object files into the final executable
$(EXEC): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $(EXEC)

# Rule for compiling .cpp files into .o object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(SRC_DIR)/shard.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean rule to remove compiled files
clean:
	rm -f $(OBJ_DIR)/*.o $(EXEC)

# Phony targets
.PHONY: all clean

