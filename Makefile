INCLUDES := -Iinclude -Iinclude/SDL3 -Iinclude/GL
SRCDIR := src
OBJDIR := .obj
BINDIR := bin

CXX := g++
CXXFLAGS := -Wall -O2 -std=c++17 -shared -fPIC -DDRUID_EXPORT -DWIN32 -g -fpermissive
LDFLAGS := -shared -Wl,--out-implib,$(BINDIR)/libdruid.a
LIBS := -Ldeps -lglew32 -lopengl32 -lSDL3 -lgdi32 -luser32

# Recursively find all cpp files in source directory
SOURCES := $(shell find $(SRCDIR) -name '*.cpp' -o -name '*.c')
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

$(info SOURCES: $(SOURCES))
$(info OBJECTS: $(OBJECTS))

# Main target
all: setup $(BINDIR)/libdruid.dll

# Create necessary directories
setup:
	@mkdir -p $(dir $(OBJECTS))
	@mkdir -p $(BINDIR)

# Build the DLL
$(BINDIR)/libdruid.dll: $(OBJECTS)
	@echo "Building Druid Engine DLL..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# Compile object files (with subdirectory support)
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean up build files
clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Generate a project template for new games
generate_project:
	@echo "Generating new game project..."
	@mkdir -p NewGame/{src,include,bin,obj}
	@echo "#include <SDL.h>\n#include <GL/gl.h>\n\nint main() { return 0; }" > NewGame/src/main.cpp
	@echo "INCLUDES := -I../include\nCXXFLAGS := -Wall -O2 -std=c++17 -g\nLIBS := -lglew32 -lopengl32 -lSDL2 -lSDL2main -lgdi32 -luser32\n" > NewGame/Makefile
	@echo "all: main\n\nmain:\n\tg++ src/main.cpp -o bin/main $(INCLUDES) $(CXXFLAGS) $(LIBS)\n" >> NewGame/Makefile

.PHONY: all setup clean generate_project
