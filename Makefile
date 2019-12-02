#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := GPS

include $(IDF_PATH)/make/project.mk

update:
	git submodule update --init --recursive --remote
	git commit -a -m "Library update"

SQLlib/sqllib.o: SQLlib/sqllib.c
	make -C SQLlib

SQLINC=$(shell mariadb_config --include)
SQLLIB=$(shell mariadb_config --libs)
SQLVER=$(shell mariadb_config --version | sed 'sx\..*xx')
CCOPTS=${SQLINC} -I. -I/usr/local/ssl/include -D_GNU_SOURCE -g -Wall -funsigned-char -lm
OPTS=-L/usr/local/ssl/lib ${SQLLIB} ${CCOPTS}

gpslog: gpslog.c SQLlib/sqllib.o main/revkgps.h
	cc -O -o $@ $< ${OPTS} -lpopt -lmosquitto -ISQLlib SQLlib/sqllib.o -lcrypto
gpsout: gpsout.c SQLlib/sqllib.o
	cc -O -o $@ $< ${OPTS} -lpopt -lmosquitto -ISQLlib SQLlib/sqllib.o

