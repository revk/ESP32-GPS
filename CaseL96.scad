// 3D case for counttdown clock
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=19.39845-2;
height=24.40039-2;

// Box thickness reference to component cube
base=1;
top=6;

$fn=48;

module pcb(s=0)
{
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
      l96(s,4.899,9.951);
      spox(s,0.999,1.001,0,6,smd=true);
    }
}

case(width,height,base,top,cutoffset=4){pcb(0);pcb(-1);pcb(1);};
