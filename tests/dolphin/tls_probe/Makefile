#---------------------------------------------------------------------------------
# tls_probe: a test DOL for Priiloader's TLS layer. Build the libraries first:
#   make -C ../../../src/third_party/mbedtls && make -C ../../../src/priiloader/tls
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

ROOT	:= $(CURDIR)/../../..
TLS		:= $(ROOT)/src/priiloader/tls
MBEDTLS	:= $(ROOT)/src/third_party/mbedtls
# NETLOG=1: a bench build (tls_probe_netlog.dol) that also sends its output to hbc.py run, through
# hbc-reborn's sdk/hbc_netlog.h (HBC_SDK)
HBC_REBORN ?= $(ROOT)/../hbc-reborn
HBC_SDK	?= $(HBC_REBORN)/sdk
TARGET	:= tls_probe$(if $(NETLOG),_netlog)
BUILD	:= build$(if $(NETLOG),_netlog)

CFLAGS	:= -g -O2 -Wall -Wextra $(MACHDEP) -iquote $(TLS)/include -I$(MBEDTLS) -I$(MBEDTLS)/include \
		-DMBEDTLS_CONFIG_FILE='"priiloader_mbedtls_config.h"' -I$(LIBOGC_INC)
ifneq ($(NETLOG),)
CFLAGS	+= -DPROBE_NETLOG -iquote $(HBC_SDK)/hbc_netlog
endif
LDFLAGS	:= -g $(MACHDEP) -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--gc-sections
LIBS	:= $(TLS)/build/libpriitls.a $(MBEDTLS)/build/libmbedtls.a -lfat -logc -lm
LIBPATH	:= -L$(LIBOGC_LIB)

all: $(TARGET).dol

$(TARGET).dol: $(BUILD)/$(TARGET).elf
	@echo creating $@
	@elf2dol $< $@

$(BUILD)/$(TARGET).elf: $(BUILD)/main.o $(TLS)/build/libpriitls.a $(MBEDTLS)/build/libmbedtls.a
	@echo linking $@
	@$(CC) $(LDFLAGS) $(BUILD)/main.o $(LIBS) $(LIBPATH) -o $@

$(BUILD)/main.o: source/main.c | $(BUILD)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).dol

.PHONY: all clean
