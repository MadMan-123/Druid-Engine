CXX := g++
CFLAGS := -Wall -std=c++20
CFLAGS += -g -std=c++20

LDFLAGS := -L../bin -ldruid -L./deps -lopengl32 -lglew32 -lSDL3 -lwinmm  
INCLUDE := -I../include
SRC := src
OBJ := .obj
BIN := bin
NAME := testbed.exe

# Find all the source files and generate the corresponding object files
SRC_FILES := $(shell find $(SRC) -name '*.cpp')
OBJ_FILES := $(patsubst $(SRC)/%.cpp, $(OBJ)/%.o, $(SRC_FILES))

# Default target to build the executable
all: setup $(BIN)/$(NAME) copy_dll

# Ensure bin and obj directories exist
setup:
	@mkdir -p $(BIN)
	@mkdir -p $(OBJ)

# Link the object files to create the executable
$(BIN)/$(NAME): $(OBJ_FILES)
	$(CXX) -o $(BIN)/$(NAME) $(OBJ_FILES) $(LDFLAGS) $(INCLUDE) $(CFLAGS)

# Copy the DLL to the bin directory to ensure runtime availability
copy_dll:
	@cp ../bin/libdruid.dll $(BIN)/

# Compile each .cpp file to .o object files
$(OBJ)/%.o: $(SRC)/%.cpp
	@mkdir -p $(OBJ)
	$(CXX) $(CFLAGS) $(INCLUDE) -c $< -o $@

# Clean target to remove the executable and object files
clean:
	@echo "Cleaning up..."
	rm -f $(BIN)/$(NAME)      # Remove the executable
	rm -f $(OBJ)/*.o          # Remove object files
	rm -f $(BIN)/libdruid.dll  # Remove copied DLL
	@echo "Clean complete."

.PHONY: all clean setup copy_dll
