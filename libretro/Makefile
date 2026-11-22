# Builds the libretro core.
#
#   make                        a native core for this machine
#   make platform=android-armv7 armeabi-v7a, needs the NDK on PATH
#   make platform=android-arm64 arm64-v8a
#   make platform=win           a Windows DLL, cross-compiled with MinGW
#   make test                   the host test frontend, which needs no RetroArch
#
# The game's own sources are compiled unchanged. Two defines do the work: SDL3/SDL.h resolves to
# the stand-in header shared with the headless test harness, and exit() is redirected so that
# plat_shutdown ends the game thread instead of taking the frontend down with it.

ROOT     := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
GAME     := $(ROOT)/src
BUILD    ?= $(ROOT)/build-libretro
NAME      = apeiron_libretro

BUILD_STAMP ?= $(shell date +%y%m%d%H%M)
BUILD_NAME  ?=
API         ?= 21

SOURCES  := $(GAME)/main.c $(GAME)/platform.c $(GAME)/assets.c $(GAME)/intro.c $(GAME)/game.c \
            $(GAME)/minifont.c $(GAME)/browse.c $(GAME)/diag.c \
            $(ROOT)/libretro/core.c $(ROOT)/libretro/sdl_libretro.c

DEFINES  := -Dmain=apeiron_main -Dexit=lr_exit \
            -DAPEIRON_BUILD_STAMP='"$(BUILD_STAMP)"' -DAPEIRON_BUILD_NAME='"$(BUILD_NAME)"'
INCLUDES := -I$(GAME) -I$(ROOT)/tools/test -I$(ROOT)/libretro
CFLAGS   ?= -O2 -std=gnu11 -fPIC -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter
LDFLAGS  ?= -shared
LIBS     ?= -lm -lpthread
EXT      := so
CC       ?= gcc

ifeq ($(platform),android-armv7)
  TOOL   := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
  CC     := $(TOOL)/armv7a-linux-androideabi$(API)-clang
  CFLAGS += -march=armv7-a -mfpu=neon -mfloat-abi=softfp
  # max-page-size keeps the .so loadable on the 16KB-page devices Android 15 brings.
  LDFLAGS := -shared -Wl,--no-undefined -Wl,-z,max-page-size=16384
  LIBS   := -lm
  OUT    := $(NAME)_android.so
  BUILD  := $(BUILD)/armeabi-v7a
else ifeq ($(platform),android-arm64)
  TOOL   := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
  CC     := $(TOOL)/aarch64-linux-android$(API)-clang
  LDFLAGS := -shared -Wl,--no-undefined -Wl,-z,max-page-size=16384
  LIBS   := -lm
  OUT    := $(NAME)_android.so
  BUILD  := $(BUILD)/arm64-v8a
else ifeq ($(platform),win)
  CC     := x86_64-w64-mingw32-gcc
  LDFLAGS := -shared -static-libgcc
  LIBS   := -lm
  OUT    := $(NAME).dll
  BUILD  := $(BUILD)/win
else
  OUT    := $(NAME).so
  BUILD  := $(BUILD)/native
endif

OBJS := $(patsubst %,$(BUILD)/%.o,$(notdir $(SOURCES)))

all: $(BUILD)/$(OUT)

$(BUILD):
	mkdir -p $(BUILD)

define rule_for
$(BUILD)/$(notdir $(1)).o: $(1) | $(BUILD)
	$$(CC) $$(CFLAGS) $$(DEFINES) $$(INCLUDES) -c $$< -o $$@
endef
$(foreach s,$(SOURCES),$(eval $(call rule_for,$(s))))

$(BUILD)/$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	@echo "built $@"

# The host test frontend: loads the core the same way RetroArch does and writes PNGs.
test: $(BUILD)/$(OUT)
	$(CC) -O2 -std=gnu11 -I$(ROOT)/libretro -o $(BUILD)/frontend \
	      $(ROOT)/libretro/test/frontend.c -ldl -lm
	@echo "built $(BUILD)/frontend"

clean:
	rm -rf $(BUILD)

.PHONY: all clean test
