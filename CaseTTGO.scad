// 3D case for GPS TTGO based tracker
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=29.21;
height=75;

// Box thickness reference to component cube
base=11.4;
top=5;

$fn=48;

module pcb(s=0)
{
    if(!s)
    {
        cube([width,height,3.2]);  // Crude top components
        translate([0,6,-1.6-5])cube([width,height-8,5]); // Crude bottom
        translate([0,6,-1.6-11])cube([width,42,11]);
    }
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
        usbc(s,0,60,-90);
        translate([20,64,0])
        {
            if(!s)cube([20,3,4]);
            else            
            hull()
            {
                    translate([0,0,s])
                    cube([20,3,1]);        // Base PCB
                    translate([0,0-1,s*20])
                    cube([20,5,1]);
            }
        }
    }
}

case(width,height,base,top,cutoffset=1){pcb(0);pcb(-1);pcb(1);};
//!pcb();


