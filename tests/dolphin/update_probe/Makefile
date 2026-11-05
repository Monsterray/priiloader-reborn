#---------------------------------------------------------------------------------
# update_probe: a test DOL for Priiloader's GitHub updater (HttpGetUrl + update_github). Build the libraries first:
#   make -C ../../../src/third_party/mbedtls && make -C ../../../src/priiloader/tls
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

ROOT	:= $(CURDIR)/../../..
PRII	:= $(ROOT)/src/priiloader
SHARED	:= $(ROOT)/src/Shared
TLS		:= $(PRII)/tls
MBEDTLS	:= $(ROOT)/src/third_party/mbedtls
TARGET	:= update_probe
BUILD	:= build

INC		:= -iquote $(PRII)/include -iquote $(SHARED) -iquote $(PRII)/../loader/include -iquote $(TLS)/include \
		-I$(MBEDTLS) -I$(MBEDTLS)/include -DMBEDTLS_CONFIG_FILE='"priiloader_mbedtls_config.h"' -I$(LIBOGC_INC)
CFLAGS	:= -g -O2 -Wall -Wextra $(MACHDEP) $(INC)
CXXFLAGS:= $(CFLAGS)
LDFLAGS	:= -g $(MACHDEP) -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--gc-sections
LIBS	:= $(TLS)/build/libpriitls.a $(MBEDTLS)/build/libmbedtls.a -lfat -logc -lm
LIBPATH	:= -L$(LIBOGC_LIB)

OBJS	:= $(BUILD)/main.o $(BUILD)/HTTP_Parser.o $(BUILD)/update_github.o $(BUILD)/version.o $(BUILD)/mem2_manager.o
DEPLIBS	:= $(TLS)/build/libpriitls.a $(MBEDTLS)/build/libmbedtls.a

all: $(TARGET).dol

$(TARGET).dol: $(BUILD)/$(TARGET).elf
	@echo creating $@
	@elf2dol $< $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(DEPLIBS)
	@echo linking $@
	@$(CXX) $(LDFLAGS) $(OBJS) $(LIBS) $(LIBPATH) -o $@

$(BUILD)/main.o: source/main.cpp | $(BUILD)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/HTTP_Parser.o: $(PRII)/source/HTTP_Parser.cpp | $(BUILD)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/update_github.o: $(PRII)/source/update_github.cpp | $(BUILD)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/version.o: $(SHARED)/version.cpp | $(BUILD)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/mem2_manager.o: $(PRII)/source/mem2_manager.c | $(BUILD)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).dol

.PHONY: all clean
