# Makefile for indi-saf-cpp
#
# Layout:
#   lib/            our own library .cpp files (no main())
#   include/        our own .hpp/.h files
#   app/            files WITH a main() -- currently just saf_local.cpp
#   third_party/liblilxml/
#                   vendored liblilxml/base64 source, copied verbatim
#                   from drbitboy/MagAOX (dev-resurrector branch) --
#                   see its own files' header comments for license
#                   (lilxml.c/.h: LGPL v2.1+; base64.c: GPL v2+)
#   ../test/        test .cpp files (sibling directory, outside this
#                   package) -- see the `test` target below
#
# #include lines throughout this package use plain quoted filenames
# (e.g. #include "mailbox.hpp"), not relative paths like
# "../include/mailbox.hpp" -- this Makefile's -I flags are what make
# that resolve correctly regardless of which subdirectory a .cpp file
# lives in. Don't add path components to an #include line; add an -I
# flag here instead.
#
# Usage:
#   make            build the saf_local binary (default target)
#   make saf_local  same, explicitly
#   make debug      build saf_local_debug with -g -O0, objects kept
#                   separately in build/debug/ so it never mixes with the
#                   regular build
#   make test       build and run all five test binaries in ../test/
#   make clean      remove build/ and the saf_local, saf_local_debug binaries

# Extra compiler flags for every compile; `make debug` sets this to
# "-g -O0" via a recursive make (see the debug target below).
EXTRA_FLAGS :=

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude -Ithird_party/liblilxml -Ithird_party/miniz $(EXTRA_FLAGS)

CC       := gcc
CFLAGS   := -Wall $(EXTRA_FLAGS)
# miniz: large-file macro avoids its "may not support large files" #pragma message
MINIZ_CFLAGS := -Wall -D_LARGEFILE64_SOURCE $(EXTRA_FLAGS)

BUILD    := build
TEST_DIR := ../test

.PHONY: all debug test test-mailbox test-config test-lilxml test-archiver test-unpacker clean

all: saf_local

# Debug build: a sub-make with its own object directory, because the
# Makefile does not track which flags an existing .o was built with --
# sharing build/ would silently reuse non-debug objects. The binary is
# named saf_local_debug so it never replaces or masquerades as saf_local.
debug:
	$(MAKE) BUILD=build/debug EXTRA_FLAGS="-g -O0" saf_local_debug

$(BUILD):
	mkdir -p $(BUILD)

# --- our own library object files ---
$(BUILD)/%.o: lib/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- the app object file (currently just saf_local.cpp) ---
$(BUILD)/%.o: app/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- vendored liblilxml/base64 (compiled as C, not held to our own
#     -Wextra standard since it isn't our code to fix warnings in) ---
$(BUILD)/%.o: third_party/liblilxml/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# --- vendored miniz (single miniz.c + miniz.h, MIT) ---
$(BUILD)/%.o: third_party/miniz/%.c | $(BUILD)
	$(CC) $(MINIZ_CFLAGS) -c $< -o $@

LIB_OBJS    := $(BUILD)/mailbox.o $(BUILD)/wire_format.o \
               $(BUILD)/indi_xml_bridge.o $(BUILD)/saf_local_config.o \
               $(BUILD)/outbound_archiver.o $(BUILD)/inbound_unpacker.o \
               $(BUILD)/saf_log.o
VENDOR_OBJS := $(BUILD)/lilxml.o $(BUILD)/base64.o $(BUILD)/miniz.o

saf_local saf_local_debug: $(BUILD)/saf_local.o $(LIB_OBJS) $(VENDOR_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

# --- tests (each links only the objects it actually needs) ---

test: test-mailbox test-config test-lilxml test-archiver test-unpacker

test-mailbox: $(BUILD)/mailbox.o $(BUILD)/wire_format.o $(BUILD)/saf_log.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/mailbox_smoketest.cpp \
	    $(BUILD)/mailbox.o $(BUILD)/wire_format.o $(BUILD)/saf_log.o \
	    -o $(BUILD)/mailbox_smoketest
	$(BUILD)/mailbox_smoketest

test-config: $(BUILD)/saf_local_config.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/saf_local_config_test.cpp \
	    $(BUILD)/saf_local_config.o \
	    -o $(BUILD)/saf_local_config_test
	$(BUILD)/saf_local_config_test

test-lilxml: $(BUILD)/indi_xml_bridge.o $(BUILD)/mailbox.o \
             $(BUILD)/wire_format.o $(BUILD)/saf_log.o $(VENDOR_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/lilxml_integration_test.cpp \
	    $(BUILD)/indi_xml_bridge.o $(BUILD)/mailbox.o \
	    $(BUILD)/wire_format.o $(BUILD)/saf_log.o $(VENDOR_OBJS) \
	    -o $(BUILD)/lilxml_integration_test
	$(BUILD)/lilxml_integration_test

test-archiver: $(BUILD)/outbound_archiver.o $(BUILD)/miniz.o $(BUILD)/saf_log.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/outbound_archiver_test.cpp \
	    $(BUILD)/outbound_archiver.o $(BUILD)/miniz.o $(BUILD)/saf_log.o \
	    -o $(BUILD)/outbound_archiver_test
	$(BUILD)/outbound_archiver_test

test-unpacker: $(BUILD)/inbound_unpacker.o $(BUILD)/outbound_archiver.o \
               $(BUILD)/miniz.o $(BUILD)/saf_log.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/inbound_unpacker_test.cpp \
	    $(BUILD)/inbound_unpacker.o $(BUILD)/outbound_archiver.o \
	    $(BUILD)/miniz.o $(BUILD)/saf_log.o \
	    -o $(BUILD)/inbound_unpacker_test
	$(BUILD)/inbound_unpacker_test

clean:
	rm -rf $(BUILD) saf_local saf_local_debug
