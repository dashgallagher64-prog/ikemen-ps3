#---------------------------------------------------------------------------------
# Ikemen PS3 (proof of concept) - PSL1GHT Makefile, modeled on the PSL1GHT samples.
#   make        -> ikemen-ps3.self
#   make pkg    -> ikemen-ps3.pkg  (+ .gnpdrm.pkg), installs on CFW and HEN
# Needs PS3DEV and PSL1GHT set (the ps3dev-docker image sets both; see build.sh).
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif

include $(PSL1GHT)/ppu_rules

TARGET		:=	ikemen-ps3
BUILD		:=	build
SOURCES		:=	source
INCLUDES	:=	include

TITLE		:=	Ikemen PS3
APPID		:=	IKEMEN001
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
SFOXML		:=	$(CURDIR)/sfo.xml
ICON0		:=	$(CURDIR)/ICON0.PNG
# everything in here is copied into the package root (so pkgfiles/USRDIR/... ends up in USRDIR/)
PKGFILES	:=	$(CURDIR)/pkgfiles

CFLAGS		=	-O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	$(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBS		:=	-lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm
LIBDIRS		:=

#---------------------------------------------------------------------------------
ifneq ($(INNER),1)   # outer make; the inner one (INNER=1) runs inside build/
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)
export BUILDDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CFILES) $(CPPFILES)),)
$(error No .c files found in '$(SOURCES)/'. The source/ folder is missing from the repo; upload the folders source/, include/ and pkgfiles/ too)
endif

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES	:=	$(addsuffix .o,$(BINFILES)) $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES), -I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					$(LIBPSL1GHT_INC) \
					-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					$(LIBPSL1GHT_LIB)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile INNER=1

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT)*.pkg

pkg:	$(BUILD) $(OUTPUT).pkg

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf:	$(OFILES)

-include $(DEPENDS)

endif
