// 3D case for counttdown clock
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=45;
height=62;

// Box thickness reference to component cube
base=10.4;
top=5;

$fn=48;

module pcb(s=0)
{
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
        esp32(s,26.400,13.480,-90);
        oled(s,1,1);
        d24v5f3(s,7.985,14.335,0);
        usbc(s,7.985,0.510,0);
        spox(s,27.035,1,0,4,hidden=true);
        smd1206(s,8.655,9.010);
        smd1206(s,13.055,9.010);
        switch66(s,19.415,5.795,90);
        adagps(s,6.250,37.600);
    }
}

case(width,height,base,top,cutoffset=-5){pcb(0);pcb(-1);pcb(1);};

