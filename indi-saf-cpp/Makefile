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
#   make test       build and run all three test binaries in ../test/
#   make clean      remove build/ and the saf_local binary

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude -Ithird_party/liblilxml

CC       := gcc
CFLAGS   := -Wall

BUILD    := build
TEST_DIR := ../test

.PHONY: all test test-mailbox test-config test-lilxml clean

all: saf_local

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

LIB_OBJS    := $(BUILD)/mailbox.o $(BUILD)/wire_format.o \
               $(BUILD)/indi_xml_bridge.o $(BUILD)/saf_local_config.o
VENDOR_OBJS := $(BUILD)/lilxml.o $(BUILD)/base64.o

saf_local: $(BUILD)/saf_local.o $(LIB_OBJS) $(VENDOR_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

# --- tests (each links only the objects it actually needs) ---

test: test-mailbox test-config test-lilxml

test-mailbox: $(BUILD)/mailbox.o $(BUILD)/wire_format.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/mailbox_smoketest.cpp \
	    $(BUILD)/mailbox.o $(BUILD)/wire_format.o \
	    -o $(BUILD)/mailbox_smoketest
	$(BUILD)/mailbox_smoketest

test-config: $(BUILD)/saf_local_config.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/saf_local_config_test.cpp \
	    $(BUILD)/saf_local_config.o \
	    -o $(BUILD)/saf_local_config_test
	$(BUILD)/saf_local_config_test

test-lilxml: $(BUILD)/indi_xml_bridge.o $(BUILD)/mailbox.o \
             $(BUILD)/wire_format.o $(VENDOR_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_DIR)/lilxml_integration_test.cpp \
	    $(BUILD)/indi_xml_bridge.o $(BUILD)/mailbox.o \
	    $(BUILD)/wire_format.o $(VENDOR_OBJS) \
	    -o $(BUILD)/lilxml_integration_test
	$(BUILD)/lilxml_integration_test

clean:
	rm -rf $(BUILD) saf_local
