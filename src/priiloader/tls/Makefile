#---------------------------------------------------------------------------------
# Priiloader's TLS layer as a static library: build/libpriitls.a (needs ../../third_party/mbedtls built first).
# Not part of priiloader.dol until the updater calls it: the library is linked from an archive, so unused
# code costs nothing.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

MBEDTLS	:= ../../third_party/mbedtls
BUILD	:= build
SRCS	:= $(wildcard source/*.c)
OBJS	:= $(patsubst source/%.c,$(BUILD)/%.o,$(SRCS))

DEFS	:= -DMBEDTLS_CONFIG_FILE='"priiloader_mbedtls_config.h"'
INC		:= -iquote $(CURDIR)/include -I$(CURDIR)/$(MBEDTLS) -I$(CURDIR)/$(MBEDTLS)/include -I$(LIBOGC_INC)
CFLAGS	:= -g -O2 -Wall -Wextra $(MACHDEP) $(DEFS) $(INC) -MMD -MP

all: $(BUILD)/libpriitls.a

$(BUILD)/libpriitls.a: $(OBJS)
	@echo archiving $(notdir $@)
	@rm -f $@
	@$(AR) rcs $@ $(OBJS)

$(BUILD)/%.o: source/%.c | $(BUILD)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD)

.PHONY: all clean
-include $(OBJS:.o=.d)
