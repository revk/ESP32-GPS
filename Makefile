#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := GPS
SUFFIX := $(shell components/ESP32-RevK/suffix)
MODELS := Display Glider L86

all:
	@echo Make: build/$(PROJECT_NAME)$(SUFFIX).bin
	@idf.py build
	@cp build/$(PROJECT_NAME).bin build/$(PROJECT_NAME)$(SUFFIX).bin
	@echo Done: build/$(PROJECT_NAME)$(SUFFIX).bin


tools: 	gpslog gpsout

set:    wroom pico

pico:
	@sed -e 's/# CONFIG_ESP32_SPIRAM_SUPPORT is not set/CONFIG_ESP32_SPIRAM_SUPPORT=y/' -e 's/# CONFIG_ESPTOOLPY_FLASHSIZE_8MB is not set/CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y/' -e 's/CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y/# CONFIG_ESPTOOLPY_FLASHSIZE_4MB is not set/' -e 's/CONFIG_FREERTOS_UNICORE=y/# CONFIG_FREERTOS_UNICORE is not set/' sdkconfig > sdkconfig.new
	@mv -f sdkconfig.new sdkconfig
	@make

wroom:
	@sed -e 's/CONFIG_ESP32_SPIRAM_SUPPORT=y/# CONFIG_ESP32_SPIRAM_SUPPORT is not set/' -e 's/# CONFIG_ESPTOOLPY_FLASHSIZE_4MB is not set/CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y/' -e 's/CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y/# CONFIG_ESPTOOLPY_FLASHSIZE_8MB is not set/' -e 's/CONFIG_FREERTOS_UNICORE=y/# CONFIG_FREERTOS_UNICORE is not set/' sdkconfig > sdkconfig.new
	@mv -f sdkconfig.new sdkconfig
	@make

flash:
	idf.py flash

monitor:
	idf.py monitor

clean:
	idf.py clean

menuconfig:
	idf.py menuconfig

#include $(IDF_PATH)/make/project.mk

pull:
	git pull
	git submodule update --recursive

update:
	git submodule update --init --remote --merge --recursive
	git commit -a -m "Library update"

SQLlib/sqllib.o: SQLlib/sqllib.c
	make -C SQLlib

SQLINC=$(shell mariadb_config --include)
SQLLIB=$(shell mariadb_config --libs)
SQLVER=$(shell mariadb_config --version | sed 'sx\..*xx')
CCOPTS=${SQLINC} -I. -I/usr/local/ssl/include -D_GNU_SOURCE -g -Wall -funsigned-char -lm
OPTS=-L/usr/local/ssl/lib ${SQLLIB} ${CCOPTS}

gpslog: gpslog.c SQLlib/sqllib.o main/revkgps.h database.sql
ifneq ($(wildcard /projects/tools/bin/sqlupdate),)
	/projects/tools/bin/sqlupdate gps database.sql
endif
	cc -O -o $@ $< ${OPTS} -lpopt -lmosquitto -ISQLlib SQLlib/sqllib.o -lcrypto ostn02.c OSTN02_OSGM02_GB.c
gpsout: gpsout.c SQLlib/sqllib.o
	cc -O -o $@ $< ${OPTS} -lpopt -lmosquitto -ISQLlib SQLlib/sqllib.o

PCBCase/case: PCBCase/case.c
	make -C PCBCase

scad:	$(patsubst %,KiCad/%.scad,$(MODELS))
stl:	$(patsubst %,KiCad/%.stl,$(MODELS))

%.stl: %.scad
	echo "Making $@"
	/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD $< -o $@
	echo "Made $@"

KiCad/L86.scad: KiCad/L86.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --top=8 --base=2.6

KiCad/Display.scad: KiCad/Display.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --top=10.4 --base=5

KiCad/Glider.scad: KiCad/Glider.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --top=10.4 --base=5
