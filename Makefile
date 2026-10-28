# yosys-config hardcodes the cross compiler used to build the OSS CAD Suite,
# so only take the flags from it and compile with the local toolchain.
CXX ?= clang++
YOSYS_FLAGS := $(shell yosys-config --cxxflags --ldflags)
PLUGINS := hier_dump.so hier_restore.so

all: $(PLUGINS)

%.so: %.cc
	$(CXX) -shared -o $@ $< $(YOSYS_FLAGS)

test: hier_restore.so
	python3 tests/run.py

clean:
	rm -f $(PLUGINS) hier.json

.PHONY: all test clean
