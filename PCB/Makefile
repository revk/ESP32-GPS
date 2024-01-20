stl:	Logger/Logger.stl L86/L86.stl

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

Logger/Logger.scad: Logger/Logger.kicad_pcb PCBCase/case Makefile PCBCase/case.scad Makefile
	PCBCase/case -o $@ $< --top=7 --bottom=7 --pcb=2

L86/L86.scad: L86/L86.kicad_pcb PCBCase/case Makefile PCBCase/case.scad Makefile
	PCBCase/case -o $@ $< --top=8 --bottom=6


