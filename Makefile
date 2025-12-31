#!/usr/bin/make -f
# Makefile for Remus LV2 Plugin

# Plugin name and URI
PLUGIN_NAME = remus
PLUGIN_SO = $(PLUGIN_NAME).so
PLUGIN_URI_PATH = remus.lv2

# Installation paths
PREFIX ?= /usr/local
LV2_DIR ?= $(PREFIX)/lib/lv2

# Compiler and flags
CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -fPIC -DPIC
LDFLAGS ?= -shared -lm

# LV2 flags
LV2_CFLAGS = $(shell pkg-config --cflags lv2 2>/dev/null || echo "")

# Source files
SRC = $(PLUGIN_NAME).c
OBJ = $(SRC:.c=.o)

# Build targets
all: $(PLUGIN_SO)

$(PLUGIN_SO): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(LV2_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(PLUGIN_SO)
	rm -rf $(PLUGIN_URI_PATH)

install: all
	install -d $(DESTDIR)$(LV2_DIR)/$(PLUGIN_URI_PATH)
	install -m 644 $(PLUGIN_SO) $(DESTDIR)$(LV2_DIR)/$(PLUGIN_URI_PATH)/
	install -m 644 manifest.ttl $(DESTDIR)$(LV2_DIR)/$(PLUGIN_URI_PATH)/
	install -m 644 $(PLUGIN_NAME).ttl $(DESTDIR)$(LV2_DIR)/$(PLUGIN_URI_PATH)/

install-user: all
	install -d ~/.lv2/$(PLUGIN_URI_PATH)
	install -m 644 $(PLUGIN_SO) ~/.lv2/$(PLUGIN_URI_PATH)/
	install -m 644 manifest.ttl ~/.lv2/$(PLUGIN_URI_PATH)/
	install -m 644 $(PLUGIN_NAME).ttl ~/.lv2/$(PLUGIN_URI_PATH)/

uninstall:
	rm -rf $(DESTDIR)$(LV2_DIR)/$(PLUGIN_URI_PATH)

uninstall-user:
	rm -rf ~/.lv2/$(PLUGIN_URI_PATH)

.PHONY: all clean install install-user uninstall uninstall-user
