#**************************************************************************
# $Id: Makefile 1030 2015-09-09 10:18:12Z alexvol $
# $HeadURL: http://server/svn/Test_Boards/daVinci_Encoder/trunk/Firmware/daVinci_ARM/test/videocap/videoloop/Makefile $
# $LastChangedRevision: 1030 $
# $LastChangedDate: 2015-09-09 13:18:12 +0300 (Wed, 09 Sep 2015) $
# $LastChangedBy: alexvol $
#***************************************************************************

ROOTDIR = /usr/local/dvsdk
TARGET = $(notdir $(CURDIR))

include $(ROOTDIR)/Rules.make

# Where to copy the resulting executables
override EXEC_DIR=/home/nfs/davinci-nfs/

EXTOBJDIRS = $(DM365MM_MODULE_INSTALL_DIR)/interface/release

# Comment this out if you want to see full compiler and linker output.
override VERBOSE = @

# Package path for the XDC tools
XDC_PATH = $(USER_XDC_PATH);$(DMAI_INSTALL_DIR)/packages;$(CE_INSTALL_DIR)/packages;$(FC_INSTALL_DIR)/packages;$(LINK_INSTALL_DIR)/packages;$(XDAIS_INSTALL_DIR)/packages;$(CMEM_INSTALL_DIR)/packages;$(CODEC_INSTALL_DIR)/packages;$(CE_INSTALL_DIR)/examples

# Where to output configuration files
XDC_CFG		= $(TARGET)_config

# Output compiler options
XDC_CFLAGS	= $(XDC_CFG)/compiler.opt

# Output linker file
XDC_LFILE	= $(XDC_CFG)/linker.cmd

# Input configuration file
XDC_CFGFILE	= h264_dei.cfg

# Platform (board) to build for
XDC_PLATFORM = ti.platforms.evmDM365

# Target tools
XDC_TARGET = gnu.targets.arm.GCArmv5T

export CSTOOL_DIR

# The XDC configuration tool command line
CONFIGURO = $(XDC_INSTALL_DIR)/xs xdc.tools.configuro
CONFIG_BLD = config.bld


C_FLAGS += -DSVN= -Wall -g -O2
C_FLAGS	+= -I$(LINUXKERNEL_INSTALL_DIR)/include -I$(LINUXKERNEL_INSTALL_DIR)/arch/arm/include -I$(LINUXKERNEL_INSTALL_DIR)/arch/arm/$(PLATFORM_ARCH)/include -I$(CMEM_INSTALL_DIR)/packages -I$(LINK_INSTALL_DIR) -DDmai_BuildOs_linux -I$(LINUXLIBS_INSTALL_DIR)/include -I$(DM365MM_MODULE_INSTALL_DIR)/Inc 

LD_FLAGS += -L$(LINUXLIBS_INSTALL_DIR)/lib -lpthread -lasound

COMPILE.c = $(VERBOSE) $(CSTOOL_PREFIX)gcc $(CPP_FLAGS) $(C_FLAGS) $(CPP_FLAGS) -c
LINK.c = $(VERBOSE) $(CSTOOL_PREFIX)gcc $(LD_FLAGS)
STRIP.c = $(VERBOSE) $(CSTOOL_PREFIX)strip

SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)

OBJFILES = $(SOURCES:%.c=%.o)
EXTOBJFILES = $(foreach dir,$(EXTOBJDIRS),$(wildcard $(dir)/*.o))

.PHONY: clean install strip

all:	install

strip: $(TARGET)

#install:	$(if $(wildcard $(TARGET)), install_$(TARGET))
install: install_$(TARGET)

install_$(TARGET): $(TARGET)
	@install -d $(EXEC_DIR)
	@install $(TARGET) $(EXEC_DIR)
	@echo
	@echo Installed $(TARGET) binaries to $(EXEC_DIR)..

$(TARGET): $(TARGET)-unstripped
	$(STRIP.c) -o  $(TARGET) $(TARGET)-unstripped


$(TARGET)-unstripped:	$(OBJFILES) $(EXTOBJFILES) $(XDC_LFILE) 
	@echo
	@echo Linking $@ from $^..
	$(LINK.c) -o $@ $^ 

$(OBJFILES):	%.o: %.c $(HEADERS) $(XDC_CFLAGS)
	@echo Compiling $@ from $<..
	$(COMPILE.c) $(shell cat $(XDC_CFLAGS)) -o $@ $<

$(XDC_LFILE) $(XDC_CFLAGS):	$(XDC_CFGFILE)
	@echo
	@echo ======== Building $(TARGET) ========
	@echo Configuring application using $<
	@echo
	$(VERBOSE) XDCPATH="$(XDC_PATH)" $(CONFIGURO) -o $(XDC_CFG) -t $(XDC_TARGET) -p $(XDC_PLATFORM) -b $(CONFIG_BLD) $(XDC_CFGFILE)

clean:
	@echo Removing generated files..
	$(VERBOSE) -$(RM) -rf $(XDC_CFG) $(OBJFILES) $(TARGET) $(TARGET)-unstripped  *~ *.d .dep
