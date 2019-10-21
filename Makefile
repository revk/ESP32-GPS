#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := GPS

include $(IDF_PATH)/make/project.mk

update:
	git submodule update --init --recursive --remote
	git commit -a -m "Library update"
