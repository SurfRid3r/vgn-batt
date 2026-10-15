CC      ?= clang
CFLAGS  ?= -O2 -Wall
HS_DIR  := $(HOME)/.hammerspoon
FRAMES  := -framework IOKit -framework CoreFoundation

.PHONY: all install install-hs clean

all: vgnbatt probe

vgnbatt: vgnbatt.c
	$(CC) $(CFLAGS) $< -o $@ $(FRAMES)

probe: probe.c
	$(CC) $(CFLAGS) $< -o $@ $(FRAMES)

install: vgnbatt
	install -d $(HS_DIR)/bin
	install -m 755 vgnbatt $(HS_DIR)/bin/

install-hs: install
	install -m 644 hammerspoon/vgnbatt.lua $(HS_DIR)/vgnbatt.lua
	@echo "已部署。若 init.lua 尚未加载, 请添加:  vgnbatt = require(\"vgnbatt\")"

clean:
	rm -f vgnbatt probe
