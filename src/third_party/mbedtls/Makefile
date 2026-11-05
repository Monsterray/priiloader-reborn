#---------------------------------------------------------------------------------
# mbedTLS 3.6 as a static library for the Wii (devkitPPC), no CMake.
# Output: build/libmbedtls.a. Our config: priiloader_mbedtls_config.h.
# The vendored sources are compiled with -w (their warnings stay out of our
# build output); `make warnings` compiles them again with mbedTLS's own flags
# and prints what they say.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

BUILD	:= build
SRCS	:= $(wildcard library/*.c)
OBJS	:= $(patsubst library/%.c,$(BUILD)/%.o,$(SRCS))

DEFS	:= -DMBEDTLS_CONFIG_FILE='"priiloader_mbedtls_config.h"'
INC		:= -I$(CURDIR) -I$(CURDIR)/include -I$(CURDIR)/library
CFLAGS	:= -O2 -g $(MACHDEP) $(DEFS) $(INC) -ffunction-sections -fdata-sections -MMD -MP
WFLAGS	:= -Wall -Wextra -Wwrite-strings -Wmissing-declarations -Wformat=2 -Wno-unused-parameter

all: $(BUILD)/libmbedtls.a

$(BUILD)/libmbedtls.a: $(OBJS)
	@echo archiving $(notdir $@)
	@rm -f $@
	@$(AR) rcs $@ $(OBJS)

$(BUILD)/%.o: library/%.c | $(BUILD)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -w -c $< -o $@

$(BUILD):
	@mkdir -p $@

# Warnings the vendored library produces with its own flags (report only).
warnings: | $(BUILD)
	@for f in $(SRCS); do $(CC) $(filter-out -MMD -MP,$(CFLAGS)) $(WFLAGS) -fsyntax-only $$f 2>&1; done

clean:
	@rm -rf $(BUILD)

.PHONY: all clean warnings
-include $(OBJS:.o=.d)
