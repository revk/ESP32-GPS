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
        switch66(s,6.43,28.414,nohole=true);
        spox(s,21.75,30.5,90,4,smd=true,hidden=true);
        spox(s,2.27,38.21,-90,6,smd=true);
        usbc(s,22.45,46.765,90);
        smd1206(s,19.9,47.435,90);
        smd1206(s,19.9,51.835,90);
        d24v5f3(s,15.605,1.635,90);
        bat1220(s,14.819,56.89,180);
        gsm2click(s,1+1.31,16.24-2.54-1.27a);
        if(!s)
        {
            pads(3.58,16.24,ny=8);
            pads(26.44,16.24,ny=6);
        }
    }
}

case(width,height,base,top,cutoffset=-3){pcb(0);pcb(-1);pcb(1);};
