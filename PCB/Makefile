stl:	Logger/Logger.stl

PCBCase/case: PCBCase/case.c
	make -C PCBCase

%.stl: %.scad
	echo "Making $@"
	/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD $< -o $@
	echo "Made $@"

update:
	-git pull
	git submodule update --init --recursive --remote
	-git commit -a -m "Library update"
	-git push

Logger/Logger.scad: Logger/Logger.kicad_pcb PCBCase/case Makefile PCBCase/case.scad
	PCBCase/case -o $@ $< --top=5 --bottom=5 --pcb=2

