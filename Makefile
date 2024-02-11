#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := GPS
SUFFIX := $(shell components/ESP32-RevK/buildsuffix)
MODELS := Display Glider L86

all:	settings.h json2gpx POSTCODE.DAT
	@echo Make: build/$(PROJECT_NAME)$(SUFFIX).bin
	@idf.py build
	@cp build/$(PROJECT_NAME).bin $(PROJECT_NAME)$(SUFFIX).bin
	@echo Done: build/$(PROJECT_NAME)$(SUFFIX).bin

beta:   
	-git pull
	-git submodule update --recursive
	-git commit -a -m checkpoint
	@make set
	cp $(PROJECT_NAME)*.bin betarelease
	git commit -a -m beta
	git push

issue:
	-git pull
	-git submodule update --recursive
	-git commit -a -m checkpoint
	@make set
	cp $(PROJECT_NAME)*.bin betarelease
	cp $(PROJECT_NAME)*.bin release
	git commit -a -m release
	git push

settings.h:     components/ESP32-RevK/revk_settings settings.def components/ESP32-RevK/settings.def
	components/ESP32-RevK/revk_settings $^

components/ESP32-RevK/revk_settings: components/ESP32-RevK/revk_settings.c
	make -C components/ESP32-RevK revk_settings

components/ESP32-RevK/idfmon: components/ESP32-RevK/idfmon.c
	make -C components/ESP32-RevK idfmon

set:    s3

s3:
	components/ESP32-RevK/setbuildsuffix -S3-MINI-N4-R2
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

CCOPTS=-I. -D_GNU_SOURCE -g -Wall -funsigned-char -lm
OPTS=${CCOPTS}

AJL/ajl.o:
	make -C AJL

json2gpx: json2gpx.c AJL/ajl.o
	gcc -O -o $@ $< -IAJL ${OPTS} -lpopt AJL/ajl.o

makepostcodes: makepostcodes.c AJL/ajl.o OSTN02_OSGM02_GB.o ostn02.o
	gcc -O -o $@ $< OSTN02_OSGM02_GB.o ostn02.o ${OPTS}

ostn02.o: ostn02.c
	gcc -fPIC -O -DLIB -c -o $@ $< ${CCOPTS}

OSTN02_OSGM02_GB.o: OSTN02_OSGM02_GB.c
	gcc -fPIC -O -DLIB -c -o $@ $< ${CCOPTS}

POSTCODE.DAT: makepostcodes
	./makepostcodes OSCodePoint/*.csv > POSTCODE.NEW
	mv -f POSTCODE.NEW POSTCODE.DAT
