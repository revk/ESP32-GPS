// 3D case for counttdown clock
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=45;
height=37;

// Box thickness reference to component cube
base=10.4;
top=5;

$fn=48;

module pcb(s=0)
{
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
        esp32(s,26.170,10.448,-90);
        oled(s,1,1);
        d24v5f3(s,12.430,12.659,180);
        spox(s,20.000,30.100,180,4,hidden=true);
        usbc(s,9.715,31.115,180);
        smd1206(s,10.385,28.365);
        smd1206(s,14.785,28.365);
        switch66(s,17.860,3.890,90);
    }
}

case(width,height,base,top,cutoffset=-5){pcb(0);pcb(-1);pcb(1);};

