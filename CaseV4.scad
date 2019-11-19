// 3D case for counttdown clock
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=45;
height=45;

// Box thickness reference to component cube
base=1;
top=10.2;

$fn=48;

module pcb(s=0)
{
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
        usbc(s,0,7.79,-90);
        smd1206(s,8.3,8.46,90);
        smd1206(s,8.3,12.86,90);
        spox(s,1,39,-180,4,smd=true,hidden=true);
        spox(s,28.6,38,180,6,smd=true);
        switch66(s,7.750,1,smd=true);
        d24v5f3(s,6.715,20.05,smd=true);
        esp32(s,18.75,-0.870,180);
        bat1220(s,14.4,33.4,0);
        oled(s,1,1,180,screw=0.5,smd=true);
    }
}

case(width,height,base,top,cutoffset=6,side=3){pcb(0);pcb(-1);pcb(1);};
