// 3D case for counttdown clock
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=30;
height=30;

// Box thickness reference to component cube
base=5.5;
top=7.5;

$fn=48;

module pcb(s=0)
{
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
      l86(s,6.8,6.8,0);
      spox(s,18.75,7.3,-90,6);
    }
}

case(width,height,base,top,cutoffset=2){pcb(0);pcb(-1);pcb(1);};

