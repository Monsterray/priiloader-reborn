#---------------------------------------------------------------------------------
# nand_state: a read-only look at the System Menu's NAND files on a Wii, before and after the installer runs.
# It patches IOS in memory the way the installer does (AHBPROT, NAND permissions, SetUID) and opens every file
# read-only. The output goes to hbc.py run (hbc-reborn's sdk/hbc_netlog.h, HBC_SDK).
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

ROOT	:= $(CURDIR)/../../..
SHARED	:= $(ROOT)/src/Shared
HBC_REBORN ?= $(ROOT)/../hbc-reborn
HBC_SDK	?= $(HBC_REBORN)/sdk
TARGET	:= nand_state
BUILD	:= build

CXXFLAGS:= -g -O2 -Wall $(MACHDEP) -iquote $(SHARED) -iquote $(ROOT)/src/Installer/include -iquote $(HBC_SDK)/hbc_netlog -I$(LIBOGC_INC)
LDFLAGS	:= -g $(MACHDEP) -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS	:= -lfat -logc -lm
OBJS	:= $(BUILD)/main.o $(BUILD)/IOS.o $(BUILD)/gecko.o

all: $(TARGET).dol

$(TARGET).dol: $(BUILD)/$(TARGET).elf
	@echo creating $@
	@elf2dol $< $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo linking $@
	@$(CXX) $(LDFLAGS) $(OBJS) $(LIBS) -L$(LIBOGC_LIB) -o $@

$(BUILD)/main.o: source/main.cpp | $(BUILD)
	@$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/IOS.o: $(SHARED)/IOS.cpp | $(BUILD)
	@$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/gecko.o: $(ROOT)/src/Installer/source/gecko.cpp | $(BUILD)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).dol

.PHONY: all clean
