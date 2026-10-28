# yosys-config hardcodes the cross compiler used to build the OSS CAD Suite,
# so only take the flags from it and compile with the local toolchain.
CXX ?= clang++
YOSYS_FLAGS := $(shell yosys-config --cxxflags --ldflags)
PLUGINS := hier_dump.so

all: $(PLUGINS)

%.so: %.cc
	$(CXX) -shared -o $@ $< $(YOSYS_FLAGS)

clean:
	rm -f $(PLUGINS)

.PHONY: all clean
