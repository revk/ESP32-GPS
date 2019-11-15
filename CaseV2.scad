// 3D case for counttdown clock
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=45;
height=56;

// Box thickness reference to component cube
base=2;
top=10;

$fn=48;

module pcb(s=0)
{
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
        usbc(s,0.480,7.290,-90);
        spox(s,1,40,-90,4,smd=true);
        switch66(s,7.750,1,smd=true);
        d24v5f3(s,7,20.050,smd=true);
        esp32(s,17.708,-5.855,180);
        l86(s,12.430,39.115,90);
        bat1220(s,32,41.210,90);
        oled(s,1,1,180,screw=1,smd=true);
    }
}

case(width,height,base,top,cutoffset=5){pcb(0);pcb(-1);pcb(1);};
