// 3D case for counttdown clock
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

use <PCBCase/case.scad>

width=28.01953;
height=68.58008;

// Box thickness reference to component cube
base=11;
top=6;

$fn=48;

module pcb(s=0)
{
    translate([-1,-1,0])
    { // 1mm ref edge of PCB vs SVG design
        esp32(s,6.16,-4.7,180);
        switch66(s,6.43,28.414,90,nohole=true);
        spox(s,21.75,30.5,90,4,smd=true,hidden=true);
        spox(s,2.27,38.21,-90,6,smd=true);
        usbc(s,22.45,46.765,90);
        smd1206(s,19.9,47.435,90);
        smd1206(s,19.9,51.835,90);
        d24v5f3(s,15.605,1.635,90);
        bat1220(s,14.819,56.89,180);
        gsm2click(s,1+1.31,16.24-2.54-1.27);
        smd1206(s,1.635,7.655);
        if(!s)
        {
            pads(3.040+0.5,15.74+0.5,ny=8);
            pads(25.9+0.5,15.74+0.5,ny=6);
            pads(1.765+0.5,2.020+0.5);
            pads(3.925+0.5,5.965+0.5);
            pads(3.425+0.5,10.275+0.5);
            hull()
            {
                translate([1.765+0.5,2.020+0.5,-1-1.6])
                cylinder(d=2,h=1);
                translate([3.425+0.5,10.275+0.5,-1-1.6])
                cylinder(d=2,h=1);
                translate([3.925+0.5,5.965+0.5,-8-1.6])
                cylinder(d=5,h=8);
            }
        }
    }
}

case(width,height,base,top,cutoffset=-3,sideedge=2){pcb(0);pcb(-1);pcb(1);};
