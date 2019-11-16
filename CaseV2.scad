// 3D case for counttdown clock
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=45;
height=56;

// Box thickness reference to component cube
base=1.6;
top=10.2;

$fn=48;

module pcb(s=0)
{
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
        usbc(s,0.480,7.290,-90);
        spox(s,1,40,-90,4,smd=true,hidden=true);
        switch66(s,7.750,1,smd=true);
        d24v5f3(s,7,20.050,smd=true);
        esp32(s,17.708,-5.855,180);
        l86(s,12.430,39.115,90);
        bat1220(s,32,41.210,90);
        oled(s,1,1,180,screw=1,smd=true);
        hull()
        { // DS18B20
            translate([31.5+3,37,6])
            rotate([-90,0,0])
            cylinder(d=6,h=10);
            translate([31.5,37,0])
            cube([6,10,1]);
        }
    }
}

case(width,height,base,top,cutoffset=4){pcb(0);pcb(-1);pcb(1);};
