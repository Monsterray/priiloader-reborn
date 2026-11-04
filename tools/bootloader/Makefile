TARGET		:=	$(notdir $(CURDIR))
SOURCES		:=	source
LOADERPATH	:=	../../src/loader/loader.bin

BIN2S 		:= bin2s
UNAME 		:= $(shell uname)
CPPFILES 	:= $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.cpp))
SFILES      := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.S)) $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.s))
LOADER_SFILE:= $(SOURCES)/loader.bin.S
LOADER_HFILE:= $(SOURCES)/loader.bin.h

ifeq ($(UNAME),Linux)
  OUTPUT := $(TARGET)
else ifeq ($(UNAME),Darwin)
  OUTPUT := $(TARGET)
  BIN2S_FLAGS  := --apple-llvm
else
  OUTPUT := $(TARGET).exe
  # windows: a mingw g++ if there is one, otherwise LLVM's clang++
  ifeq ($(shell command -v x86_64-w64-mingw32-g++),)
    ifneq ($(shell command -v clang++),)
      HOSTCXX ?= clang++
      HOSTFLAGS := -O2 -DWIN32 -D_CRT_SECURE_NO_WARNINGS
    endif
  else
    HOSTCXX ?= x86_64-w64-mingw32-g++
  endif
endif
HOSTCXX ?= g++

.PHONY: clean

all: $(OUTPUT)

$(LOADER_SFILE): $(LOADERPATH)
	@$(BIN2S) $(BIN2S_FLAGS) -H $(LOADER_HFILE) $< > $@

$(OUTPUT): $(CPPFILES) $(SFILES) $(LOADER_SFILE)
	@$(HOSTCXX) $(HOSTFLAGS) $^ -o $@

clean:
	@echo cleaning...
	@rm -fr $(OUTPUT) $(LOADER_SFILE) $(LOADER_HFILE)

