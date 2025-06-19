INCLUDES := -Iinclude -Iinclude/SDL3 -Iinclude/GL
SRCDIR := src
OBJDIR := .obj
BINDIR := bin

CXX := gcc
CXXFLAGS := -Wall -O2 -std=c11 -shared -fPIC -DDRUID_EXPORT -DWIN32 -g
LDFLAGS := -shared -Wl,--out-implib,$(BINDIR)/libdruid.a
LIBS := -Ldeps -lglew32 -lopengl32 -lSDL3 -lgdi32 -luser32

# Recursively find all cpp files in source directory
SOURCES := $(shell find $(SRCDIR) -name '*.c')
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

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
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@


# Clean up build files
clean:
	rm -rf $(OBJDIR) $(BINDIR)

.PHONY: all setup clean generate_project
