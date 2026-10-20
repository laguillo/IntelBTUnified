# Compila IntelBTUnified.kext solo con las Command Line Tools (sin Xcode).
#   make deps      clona MacKernelSDK y Lilu (versiones fijadas)
#   make           compila build/IntelBTUnified.kext (todos los firmwares)
#   make FW="ibt-0041-*"  incluye solo los firmwares que coincidan (p.ej. AX201: ~1 MB en vez de ~11 MB)
#   make DEBUG=1   build de depuracion (DBGLOG activo)
#   make dist      genera dist/IntelBTUnified-<version>-{AX210,full}.zip (para subir a GitHub Releases)

NAME       := IntelBTUnified
VERSION    := 1.0.0
BUNDLE_ID  := local.$(NAME)
FW         ?= *

MKSDK_REF  := 3f750085caa17ec3a7880f11c11bf4f48cd6a164
LILU_REF   := 0515f40b7f2a096adc85e832a4c6104fbd07f936
MKSDK      := deps/MacKernelSDK
LILU       := deps/Lilu

BUILD      := build
KEXT       := $(BUILD)/$(NAME).kext
CC         := xcrun clang
CXX        := xcrun clang++
MINOS      := 12.0

ifdef DEBUG
OPT := -O0 -g -DDEBUG=1
else
OPT := -Os -DNDEBUG
endif

COMMON := -target x86_64-apple-macos$(MINOS) -arch x86_64 -mkernel -fno-builtin -fno-common \
          -fno-stack-protector -fapple-kext -fvisibility=hidden -nostdinc \
          -D_FORTIFY_SOURCE=0 -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
          -DPRODUCT_NAME=$(NAME) -DMODULE_VERSION=$(VERSION) \
          -Wall -Wno-unused-function -Wno-deprecated-declarations $(OPT) \
          -I$(MKSDK)/Headers -I$(LILU) -I$(LILU)/Lilu -I$(BUILD) -Isrc/driver \
          -isysroot $(shell xcrun --sdk macosx --show-sdk-path)
CFLAGS   := $(COMMON) -std=gnu11
CXXFLAGS := $(COMMON) -std=gnu++17 -fno-rtti -fno-exceptions -fno-sized-deallocation -nostdinc++

LILU_SRC := plugin_start.cpp
DRV_SRC  := $(wildcard src/driver/*.cpp)
OBJS     := $(BUILD)/obj/IntelBTUnified.o $(BUILD)/obj/plugin_start.o $(BUILD)/obj/FwBinary.o \
            $(BUILD)/obj/kmod_info.o \
            $(patsubst src/driver/%.cpp,$(BUILD)/obj/%.o,$(DRV_SRC))

.PHONY: all deps clean dist
all: $(KEXT)

deps:
	mkdir -p deps
	[ -d $(MKSDK) ] || git clone https://github.com/acidanthera/MacKernelSDK $(MKSDK)
	git -C $(MKSDK) checkout -q $(MKSDK_REF)
	[ -d $(LILU) ] || git clone https://github.com/acidanthera/Lilu $(LILU)
	git -C $(LILU) checkout -q $(LILU_REF)

$(BUILD)/Devices.h: devices.txt scripts/gen_devices.py
	@mkdir -p $(BUILD)
	python3 -I scripts/gen_devices.py $< $@

$(BUILD)/FwBinary.cpp: $(wildcard fw/*) scripts/gen_fw.py
	@mkdir -p $(BUILD)
	python3 -I scripts/gen_fw.py fw $@ $(foreach p,$(FW),'$(p)')

$(BUILD)/Info.plist: devices.txt scripts/gen_plist.py scripts/gen_devices.py Makefile
	@mkdir -p $(BUILD)
	python3 -I scripts/gen_plist.py devices.txt $@ $(VERSION)

$(BUILD)/kmod_info.c: scripts/kmod_info.c.in Makefile
	@mkdir -p $(BUILD)
	sed -e 's/@MODULE_START@/$(NAME)_kern_start/g' -e 's/@MODULE_STOP@/$(NAME)_kern_stop/g' \
	    -e 's/@BUNDLE_ID@/$(BUNDLE_ID)/g' -e 's/@VERSION@/$(VERSION)/g' $< > $@

$(BUILD)/obj/IntelBTUnified.o: src/IntelBTUnified.cpp $(BUILD)/Devices.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/obj/plugin_start.o: $(LILU)/Lilu/Library/plugin_start.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/obj/FwBinary.o: $(BUILD)/FwBinary.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/obj/%.o: src/driver/%.cpp $(BUILD)/Devices.h $(wildcard src/driver/*.h src/driver/*.hpp)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/obj/kmod_info.o: $(BUILD)/kmod_info.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(NAME): $(OBJS)
	$(CXX) -target x86_64-apple-macos$(MINOS) -arch x86_64 -nostdlib -Xlinker -kext -Xlinker -no_data_const \
	  -isysroot $(shell xcrun --sdk macosx --show-sdk-path) \
	  $(OBJS) -L$(MKSDK)/Library/x86_64 -L$(MKSDK)/Library/universal -lkmodc++ -lkmod -o $@

$(KEXT): $(BUILD)/$(NAME) $(BUILD)/Info.plist
	rm -rf $@
	mkdir -p $@/Contents/MacOS
	cp $(BUILD)/$(NAME) $@/Contents/MacOS/$(NAME)
	cp $(BUILD)/Info.plist $@/Contents/Info.plist
	@echo "OK: $@"

# Dos paquetes: solo AX210/AX211 (firmware ibt-0041-*, ~0.6 MB) y todos los firmwares (~11 MB).
dist:
	rm -rf dist && mkdir -p dist
	$(MAKE) clean && $(MAKE) FW="ibt-0041-*"
	cd $(BUILD) && zip -qry ../dist/$(NAME)-$(VERSION)-AX210.zip $(NAME).kext
	$(MAKE) clean && $(MAKE)
	cd $(BUILD) && zip -qry ../dist/$(NAME)-$(VERSION)-full.zip $(NAME).kext
	$(MAKE) clean
	@ls -la dist

clean:
	rm -rf $(BUILD)
