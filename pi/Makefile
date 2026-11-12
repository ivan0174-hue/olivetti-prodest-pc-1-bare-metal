#
# Prodest PC1 Emulator - Makefile per Raspberry Pi bare metal (Circle)
#
# Prima configura e compila Circle (vedi README.md), poi:  make
#

CIRCLEHOME ?= ../circle

CORE	= ../core/cpu.o ../core/machine.o ../core/fdc.o ../core/video.o ../core/hlebios.o ../core/ui.o

OBJS	= main.o kernel.o $(CORE)

LIBS	= $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	  $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	  $(CIRCLEHOME)/lib/sound/libsound.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/libcircle.a

OPTIMIZE = -O3

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
