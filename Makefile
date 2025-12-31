#!/usr/bin/make -f
# Makefile for Remus LV2 Plugin

# Plugin name and URI
PLUGIN_NAME = remus
PLUGIN_SO = $(PLUGIN_NAME).so
PLUGIN_BUNDLE = plugins/remus.lv2

# Installation paths
PREFIX ?= /usr/local
LV2_DIR ?= $(PREFIX)/lib/lv2

# Compiler and flags
CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -fPIC -DPIC
LDFLAGS ?= -shared -lm

# LV2 flags
LV2_CFLAGS = $(shell pkg-config --cflags lv2 2>/dev/null || echo "")

# Source and build directories
SRC_DIR = src
BUILD_DIR = build

# Source files
SRC = $(SRC_DIR)/$(PLUGIN_NAME).c
OBJ = $(BUILD_DIR)/$(PLUGIN_NAME).o

# Build targets
all: $(PLUGIN_BUNDLE)/$(PLUGIN_SO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LV2_CFLAGS) -c $< -o $@

$(PLUGIN_BUNDLE)/$(PLUGIN_SO): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(PLUGIN_BUNDLE)/$(PLUGIN_SO)

install: all
	install -d $(DESTDIR)$(LV2_DIR)/remus.lv2
	install -m 755 $(PLUGIN_BUNDLE)/$(PLUGIN_SO) $(DESTDIR)$(LV2_DIR)/remus.lv2/
	install -m 644 $(PLUGIN_BUNDLE)/manifest.ttl $(DESTDIR)$(LV2_DIR)/remus.lv2/
	install -m 644 $(PLUGIN_BUNDLE)/$(PLUGIN_NAME).ttl $(DESTDIR)$(LV2_DIR)/remus.lv2/

install-user: all
	install -d ~/.lv2/remus.lv2
	install -m 755 $(PLUGIN_BUNDLE)/$(PLUGIN_SO) ~/.lv2/remus.lv2/
	install -m 644 $(PLUGIN_BUNDLE)/manifest.ttl ~/.lv2/remus.lv2/
	install -m 644 $(PLUGIN_BUNDLE)/$(PLUGIN_NAME).ttl ~/.lv2/remus.lv2/

uninstall:
	rm -rf $(DESTDIR)$(LV2_DIR)/remus.lv2

uninstall-user:
	rm -rf ~/.lv2/remus.lv2

.PHONY: all clean install install-user uninstall uninstall-user
