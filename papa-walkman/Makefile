#---------------------------------------------------------------------------------
# 爬爬的随身听 —— 用 devkitPro 经典 Makefile 模板构建 NRO
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "请先 export DEVKITPRO=<devkitPro 路径>")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# 目标与目录
#---------------------------------------------------------------------------------
TARGET      :=  papa-walkman
BUILD       :=  build
SOURCES     :=  source
INCLUDES    :=  source third_party/minimp3
DATA        :=  data
ROMFS       :=  romfs

APP_TITLE   :=  爬爬的随身听
APP_AUTHOR  :=  jay
APP_VERSION :=  0.1.0

#---------------------------------------------------------------------------------
# 编译选项
#---------------------------------------------------------------------------------
ARCH    :=  -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  :=  -g -Wall -O2 -ffunction-sections -fdata-sections $(ARCH) $(DEFINES)
CFLAGS  +=  $(INCLUDE) -D__SWITCH__

CXXFLAGS := $(CFLAGS) -std=c++17 -fno-rtti -fno-exceptions

LDFLAGS  =  -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    :=  -lstdc++ -lnx
LIBDIRS :=  $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# 由 switch_rules 使用
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   :=  $(CURDIR)/$(TARGET)
export TOPDIR   :=  $(CURDIR)

export VPATH    :=  $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                    $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  :=  $(CURDIR)/$(BUILD)

CFILES      :=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    :=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      :=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    :=  $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OFILES_BIN   :=  $(addsuffix .o,$(BINFILES))
export OFILES_SRC   :=  $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES       :=  $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN   :=  $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  :=  $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                    -I$(CURDIR)/$(BUILD)

export LIBPATHS :=  $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).elf

else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro : $(OUTPUT).elf

$(OUTPUT).elf : $(OFILES)

-include $(DEPENDS)

endif
