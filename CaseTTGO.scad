// 3D case for GPS TTGO based tracker
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=29.21;
height=75;

// Box thickness reference to component cube
base=2;
top=18;

$fn=48;

module pcb(s=0)
{
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
        cube([width,height,16-1.6]);
        translate([0,0,12])usbc(s,0,61,-90);
        translate([20,62,12])cube([20,3,4]);
    }
}

case(width,height,base,top,cutoffset=15){pcb(0);pcb(-1);pcb(1);};

