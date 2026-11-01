# Makefile for SAINTCON 2018 Badge MicroPython Core (WebAssembly & Native Headless CLI)
# Uses MicroPython v1.24.1 (ports/webassembly + py core + extmod).

TARGET ?= wasm
MPY_TOP_REL = upstream/micropython
BUILD ?= ../../build/core-2018-obj/$(TARGET)
ASSET_DIR ?= ../../build/core-2018-obj
OUT_WASM_JS ?= ../../web/badges/2018/core.js
OUT_CLI ?= emu-cli

# Set TOP before including mkenv.mk so MicroPython makefiles locate py/ and extmod/.
TOP = $(CURDIR)/$(MPY_TOP_REL)

include $(TOP)/py/mkenv.mk

MICROPY_ROM_TEXT_COMPRESSION = 0
QSTR_DEFS = platform/qstrdefsport.h
vpath %.h platform

include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

ifeq ($(TARGET),wasm)
CC = emcc
LD = emcc
else
CC = gcc
LD = gcc
endif

INC += -I.
INC += -Iplatform
INC += -I$(ASSET_DIR)
INC += -I$(TOP)
INC += -I$(TOP)/ports/webassembly
INC += -I$(BUILD)

CFLAGS += -std=gnu11 -O2 -DNDEBUG -Wall -Wno-unused-function -Wno-deprecated-declarations
CFLAGS += $(INC)

SRC_SHARED = $(addprefix shared/,\
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	timeutils/timeutils.c \
	)

SRC_PORT = \
	platform/sc18_sim.c \
	platform/modsc18.c \

SRC_QSTR += $(SRC_PORT) $(SRC_SHARED)

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_PORT:.c=.o))

ifeq ($(TARGET),native)
SRC_CLI = cli_2018.c
OBJ += $(addprefix $(BUILD)/, $(SRC_CLI:.c=.o))
endif

EXPORTED_FUNCS = ["_emu_init","_emu_step","_emu_framebuffer","_emu_leds","_emu_backlight","_emu_eeprom","_emu_eeprom_dirty","_emu_set_speed","_emu_fb_width","_emu_fb_height","_emu_led_count","_emu_eeprom_size","_emu_2018_ap_info","_emu_2018_http_request","_emu_2018_http_response_len","_emu_2018_mb_set_present","_emu_2018_mb_queue_text","_emu_2018_mb_queue_pixels","_emu_2018_mb_queue_button","_emu_2018_mb_get_info","_malloc","_free"]
EXPORTED_RUNTIME = ["HEAPU8","HEAPU16","wasmMemory","UTF8ToString","stringToUTF8","lengthBytesUTF8"]

WASM_LDFLAGS = \
	-O2 \
	-sASYNCIFY=1 \
	-sASYNCIFY_STACK_SIZE=131072 \
	-sSUPPORT_LONGJMP=emscripten \
	-sFILESYSTEM=1 \
	-sWASM=1 \
	-sMODULARIZE=1 \
	-sEXPORT_NAME=createCore \
	-sEXPORTED_FUNCTIONS='$(EXPORTED_FUNCS)' \
	-sEXPORTED_RUNTIME_METHODS='$(EXPORTED_RUNTIME)' \
	-sSTACK_SIZE=524288 \
	-sINITIAL_MEMORY=33554432 \
	-sALLOW_MEMORY_GROWTH=1 \
	-sENVIRONMENT=web,worker

.PHONY: all

ifeq ($(TARGET),wasm)
all: $(OUT_WASM_JS)

$(OUT_WASM_JS): $(OBJ)
	$(ECHO) "LINK $@"
	@mkdir -p $(dir $@)
	$(Q)$(LD) $(OBJ) $(WASM_LDFLAGS) -o $@
else
all: $(OUT_CLI)

$(OUT_CLI): $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(LD) -O2 $(OBJ) -lz -lm -o $@
endif

include $(TOP)/py/mkrules.mk
