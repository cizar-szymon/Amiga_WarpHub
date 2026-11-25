# WARP HUB for the Amiga - bebbo amiga-gcc (Docker image
# sacredbanana/amiga-compiler:m68k-amigaos, run by tools/build_amiga.sh).
# AmiSSL SDK in $(AMISSL) (C:\amiga-dev\amissl mounted as /amissl).
#
# -mcpu=68020 -msoft-float: runs on 020..060; -mtune=68060: no 64-bit
# MULS.L for divisions by constants (traps on the 060, PORTING_PLAN.md).

AMISSL ?= /amissl
OUT    ?= ../build/amiga
CC      = m68k-amigaos-gcc
CFLAGS  = -mcpu=68020 -msoft-float -mtune=68060 -O1 -noixemul -fomit-frame-pointer \
          -Wall -Wno-pointer-sign -Wno-format -I$(AMISSL)/include -DBUILD_UNIX=$(shell date +%s)
LDFLAGS = -mcpu=68020 -msoft-float -noixemul

OBJS = hub_https.o hub_time.o hub_catalog.o hub_install.o hub_util.o

all: $(OUT)/WarpHubCLI $(OUT)/WarpHub $(OUT)/WBStart $(OUT)/WarpNetTest

# test tool: start a program as from its icon
$(OUT)/WBStart: tools/wbstart.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -s -o $@ $<

$(OUT)/WarpHub: warphub.o hub_worker.o $(OBJS)
	mkdir -p $(OUT)
	$(CC) $(LDFLAGS) -o $@ $^ -lamiga
	m68k-amigaos-nm $@ | grep -q ___stkinit && m68k-amigaos-strip -o $@.s $@ && mv $@.s $@

$(OUT)/WarpHubCLI: warphubcli.o $(OBJS)
	mkdir -p $(OUT)
	$(CC) $(LDFLAGS) -o $@ $^
	m68k-amigaos-nm $@ | grep -q ___stkinit && m68k-amigaos-strip -o $@.s $@ && mv $@.s $@

# network test for players: GitHub and the private source, from the icon
$(OUT)/WarpNetTest: nettest.o hub_https.o hub_time.o hub_util.o
	mkdir -p $(OUT)
	$(CC) $(LDFLAGS) -o $@ $^
	m68k-amigaos-nm $@ | grep -q ___stkinit && m68k-amigaos-strip -o $@.s $@ && mv $@.s $@

%.o: %.c hub_https.h hub_time.h hub_catalog.h hub_install.h hub_util.h hub_worker.h hub_mem.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(OUT)/WarpHubCLI $(OUT)/WarpHub $(OUT)/WBStart $(OUT)/WarpNetTest
