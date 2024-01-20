// Generated case design for Logger/Logger.kicad_pcb
// By https://github.com/revk/PCBCase
// Generated 2024-01-20 13:58:11
// title:	GPS reference
// date:	${DATE}
// rev:	5
// company:	Adrian Kennard Andrews & Arnold Ltd
// comment:	www.me.uk
// comment:	@TheRealRevK
//

// Globals
margin=0.200000;
lip=2.000000;
casebottom=5.000000;
casetop=5.000000;
casewall=3.000000;
fit=0.000000;
edge=1.000000;
pcbthickness=0.800000;
nohull=false;
hullcap=1.000000;
hulledge=1.000000;
useredge=false;

module outline(h=pcbthickness,r=0){linear_extrude(height=h)offset(r=r)polygon(points=[[18.000000,26.000000],[1.000000,26.000000],[0.617317,25.923880],[0.292894,25.707107],[0.076121,25.382683],[0.000000,25.000000],[0.000000,1.000000],[0.076120,0.617317],[0.292893,0.292894],[0.617317,0.076121],[1.000000,0.000000],[42.000000,0.000000],[42.382683,0.076120],[42.707106,0.292893],[42.923879,0.617317],[43.000000,1.000000],[43.000000,25.000000],[42.923880,25.382683],[42.707107,25.707106],[42.382683,25.923879],[42.000000,26.000000],[34.000000,26.000000],[33.977164,25.885195],[33.912132,25.787868],[33.814805,25.722836],[33.700000,25.700000],[18.300000,25.700000],[18.185195,25.722836],[18.087868,25.787868],[18.022836,25.885195]],paths=[[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29]]);}

module pcb(h=pcbthickness,r=0){linear_extrude(height=h)offset(r=r)polygon(points=[[18.000000,26.000000],[1.000000,26.000000],[0.617317,25.923880],[0.292894,25.707107],[0.076121,25.382683],[0.000000,25.000000],[0.000000,1.000000],[0.076120,0.617317],[0.292893,0.292894],[0.617317,0.076121],[1.000000,0.000000],[42.000000,0.000000],[42.382683,0.076120],[42.707106,0.292893],[42.923879,0.617317],[43.000000,1.000000],[43.000000,25.000000],[42.923880,25.382683],[42.707107,25.707106],[42.382683,25.923879],[42.000000,26.000000],[34.000000,26.000000],[33.977164,25.885195],[33.912132,25.787868],[33.814805,25.722836],[33.700000,25.700000],[18.300000,25.700000],[18.185195,25.722836],[18.087868,25.787868],[18.022836,25.885195]],paths=[[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29]]);}
spacing=59.000000;
pcbwidth=43.000000;
pcblength=26.000000;
// Parts to go on PCB (top)
module parts_top(part=false,hole=false,block=false){
translate([17.700000,18.900000,0.800000])rotate([0,0,-90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([22.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([4.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([9.800000,7.575000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([9.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([28.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([17.100000,25.100000,0.800000])rotate([0,0,90.000000])m1(part,hole,block,casetop); // D12 (back)
translate([25.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([17.700000,22.400000,0.800000])rotate([0,0,90.000000])m2(part,hole,block,casetop); // RevK:R_0402 R_0402_1005Metric (back)
translate([6.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([38.300000,23.300000,0.800000])rotate([0,0,180.000000])m3(part,hole,block,casetop); // C2 (back)
translate([13.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([7.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([26.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([38.000000,10.200000,0.800000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([16.250000,6.500000,0.800000])rotate([0,0,90.000000])m4(part,hole,block,casetop); // RevK:R_1218 R_1218_3246Metric (back)
translate([34.750000,10.000000,0.800000])rotate([0,0,-90.000000])m5(part,hole,block,casetop); // RevK:C_0805 C_0805_2012Metric (back)
translate([8.400000,7.600000,0.800000])rotate([0,0,-90.000000])m5(part,hole,block,casetop); // RevK:C_0805 C_0805_2012Metric (back)
translate([27.000000,6.200000,0.800000])rotate([0,0,90.000000])m6(part,hole,block,casetop); // U3 (back)
translate([18.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([32.200000,3.500000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([11.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([41.700000,9.400000,0.800000])rotate([0,0,90.000000])m2(part,hole,block,casetop); // RevK:R_0402 R_0402_1005Metric (back)
translate([0.700000,7.100000,0.800000])rotate([0,0,-90.000000])m2(part,hole,block,casetop); // RevK:R_0402 R_0402_1005Metric (back)
translate([31.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([27.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([30.800000,8.800000,0.800000])m7(part,hole,block,casetop); // D2 (back)
translate([10.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([23.000000,6.000000,0.800000])rotate([0,0,180.000000])m6(part,hole,block,casetop); // U3 (back)
translate([38.300000,16.035000,0.800000])rotate([0,0,90.000000])translate([0.000000,-1.050000,0.000000])rotate([90.000000,-0.000000,-0.000000])m8(part,hole,block,casetop); // RevK:USB-C-Socket-H CSP-USC16-TR (back)
translate([5.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([26.000000,18.000000,0.800000])m9(part,hole,block,casetop); // U1 (back)
translate([12.050000,6.937500,0.800000])rotate([0,0,90.000000])m10(part,hole,block,casetop); // Q2 (back)
translate([23.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([25.600000,8.800000,0.800000])m2(part,hole,block,casetop); // RevK:R_0402 R_0402_1005Metric (back)
translate([16.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([8.600000,16.050000,0.800000])rotate([0,0,180.000000])m11(part,hole,block,casetop); // J6 (back)
translate([20.000000,5.750000,0.800000])rotate([0,0,-90.000000])m5(part,hole,block,casetop); // RevK:C_0805 C_0805_2012Metric (back)
translate([1.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([27.500000,8.800000,0.800000])rotate([0,0,180.000000])m2(part,hole,block,casetop); // RevK:R_0402 R_0402_1005Metric (back)
translate([29.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([2.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([21.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([12.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([17.700000,15.700000,0.800000])rotate([0,0,-90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([0.700000,9.000000,0.800000])rotate([0,0,-90.000000])m2(part,hole,block,casetop); // RevK:R_0402 R_0402_1005Metric (back)
translate([15.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([16.600000,23.450000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([20.800000,8.200000,0.800000])m12(part,hole,block,casetop); // RevK:C_0603 C_0603_1608Metric (back)
translate([20.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([30.937500,6.000000,0.800000])m10(part,hole,block,casetop); // Q2 (back)
translate([32.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([3.600000,10.000000,0.800000])rotate([0,0,180.000000])m12(part,hole,block,casetop); // RevK:C_0603 C_0603_1608Metric (back)
translate([24.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([3.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([30.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([6.900000,4.800000,0.800000])rotate([0,0,-90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([12.000000,4.600000,0.800000])rotate([0,0,180.000000])m2(part,hole,block,casetop); // RevK:R_0402 R_0402_1005Metric (back)
translate([40.700000,9.400000,0.800000])rotate([0,0,-90.000000])m2(part,hole,block,casetop); // RevK:R_0402 R_0402_1005Metric (back)
translate([19.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([38.000000,5.000000,0.800000])m13(part,hole,block,casetop,02); // J1 (back)
// Missing model U2.1 LGA-16_3x3mm_P0.5mm_LayoutBorder3x5y
translate([17.700000,20.600000,0.800000])rotate([0,0,-90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([8.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([0.700000,5.300000,0.800000])rotate([0,0,-90.000000])m2(part,hole,block,casetop); // RevK:R_0402 R_0402_1005Metric (back)
translate([17.000000,2.900000,0.800000])rotate([0,0,90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
translate([14.000000,1.500000,0.800000])rotate([0,0,45.000000])m1(part,hole,block,casetop); // D12 (back)
translate([17.700000,17.300000,0.800000])rotate([0,0,-90.000000])m0(part,hole,block,casetop); // RevK:DFN1006-2L C_0402_1005Metric (back)
}

parts_top=26;
// Parts to go on PCB (bottom)
module parts_bottom(part=false,hole=false,block=false){
// Missing model J3.1 1815154 (back)
// Missing model J3.2 1778793 (back)
}

parts_bottom=0;
module b(cx,cy,z,w,l,h){translate([cx-w/2,cy-l/2,z])cube([w,l,h]);}
module m0(part=false,hole=false,block=false,height)
{ // RevK:DFN1006-2L C_0402_1005Metric
// 0402 Capacitor
if(part)
{
	b(0,0,0,1.0,0.5,1); // Chip
	b(0,0,0,1.5,0.65,0.2); // Pad size
}
}

module m1(part=false,hole=false,block=false,height)
{ // D12
// 1x1mm LED
if(part)
{
        b(0,0,0,1,1,.8);
}
if(hole)
{
        hull()
        {
                b(0,0,.8,1,1,1);
                translate([0,0,height])cylinder(d=2,h=1,$fn=16);
        }
}
if(block)
{
        hull()
        {
                b(0,0,0,2,2,1);
                translate([0,0,height])cylinder(d=4,h=1,$fn=16);
        }
}
}

module m2(part=false,hole=false,block=false,height)
{ // RevK:R_0402 R_0402_1005Metric
// 0402 Resistor
if(part)
{
	b(0,0,0,1.5,0.65,0.2); // Pad size
	b(0,0,0,1.0,0.5,0.5); // Chip
}
}

module m3(part=false,hole=false,block=false,height)
{ // C2
if(part)
{
	b(0,0,0,7.5,4.5,3.1);
}
}

module m4(part=false,hole=false,block=false,height)
{ // RevK:R_1218 R_1218_3246Metric
if(part)
{
	b(0,0,0,3.1,4.6,0.5); // Chip
	b(-1.475,0,0,1.05,4.75,0.1); // Pad
	b(1.475,0,0,1.05,4.75,0.1); // Pad
}
}

module m5(part=false,hole=false,block=false,height)
{ // RevK:C_0805 C_0805_2012Metric
// 0805 Capacitor
if(part)
{
	b(0,0,0,2,1.2,1); // Chip
	b(0,0,0,2,1.45,0.2); // Pad size
}
}

module m6(part=false,hole=false,block=false,height)
{ // U3
// SOT-23-5
if(part)
{
	b(0,0,0,1.726,3.026,1.2); // Part
	b(0,0,0,3.6,2.5,0.5); // Pins (well, one extra)
}
}

module m7(part=false,hole=false,block=false,height)
{ // D2
// SOD-123 Diode
if(part)
{
	b(0,0,0,2.85,1.8,1.35); // part
	b(0,0,0,4.2,1.2,0.7); // pads
}
}

module m8(part=false,hole=false,block=false,height)
{ // RevK:USB-C-Socket-H CSP-USC16-TR
// USB connector
rotate([-90,0,0])translate([-4.47,-3.84,0])
{
	if(part)
	{
		b(4.47,7,0,7,2,0.2);	// Pads
		translate([1.63,-0.2,1.63])
		rotate([-90,0,0])
		hull()
		{
			cylinder(d=3.26,h=7.55,$fn=24);
			translate([5.68,0,0])
			cylinder(d=3.26,h=7.55,$fn=24);
		}
		translate([0,6.25,0])cube([8.94,1.1,1.63]);
		translate([0,1.7,0])cube([8.94,1.6,1.63]);
	}
	if(hole)
	{
		// Plug
		translate([1.63,-20,1.63])
		rotate([-90,0,0])
		hull()
		{
			cylinder(d=2.5,h=21,$fn=24);
			translate([5.68,0,0])
			cylinder(d=2.5,h=21,$fn=24);
		}
		translate([1.63,-22.5,1.63])
		rotate([-90,0,0])
		hull()
		{
			cylinder(d=7,h=21,$fn=24);
			translate([5.68,0,0])
			cylinder(d=7,h=21,$fn=24);
		}
	}
}
}

module m9(part=false,hole=false,block=false,height)
{ // U1
// ESP32-S3-MINI-1
translate([-15.4/2,-15.45/2,0])
{
	if(part)
	{
		cube([15.4,20.5,0.8]);
		translate([0.7,0.5,0])cube([14,13.55,2.4]);
	}
	if(hole)
	{
		cube([15.4,20.5,0.8]);
	}
}
}

module m10(part=false,hole=false,block=false,height)
{ // Q2
// SOT-23
if(part)
{
	b(0,0,0,1.4,3.0,1.1); // Body
	b(-0.9375,-0.95,0,1.475,0.6,0.5); // Pad
	b(-0.9375,0.95,0,1.475,0.6,0.5); // Pad
	b(0.9375,0,0,1.475,0.6,0.5); // Pad
}
}

module m11(part=false,hole=false,block=false,height)
{ // J6
if(part)
{
	b(0,-2.45,0,14.85,14.5,2); // Main case
	b(-7.75,4.3,0,1.2,1.5,0.2); // Tab
	b(-7.75,-5.3,0,1.2,2.2,0.2); // Tab
	b(7.75,-5.3,0,1.2,2.2,0.2); // Tab
	for(i=[0:8])b(2.25-i*1.1,5.3,0,0.7,1.6,0.4); // Pins
	b(-0.95,-4.7,0.75,10,15,1);	// Card
}
if(hole)
{
	b(-0.95,-4.7,0.75,10,15,1);	// Card
}
}

module m12(part=false,hole=false,block=false,height)
{ // RevK:C_0603 C_0603_1608Metric
// 0603 Capacitor
if(part)
{
	b(0,0,0,1.6,0.8,1); // Chip
	b(0,0,0,1.6,0.95,0.2); // Pad size
}
}

module m13(part=false,hole=false,block=false,height,N=0)
{ // J1
if(part)
{
	b(2*(N/2)-1,3.6,0,2*N+2,6,4);
	b(2*(N/2)-1,0,0,2*N+2,3.2,1.5);
	for(a=[0:1:N-1])translate([2*a,0,-3])cylinder(d1=0.5,d2=2,h=3,$fn=12);
}
if(hole)
{
	b(2*(N/2)-1,5+3.6,0,2*N+2,6+10,4);
}
}

// Generate PCB casework

height=casebottom+pcbthickness+casetop;
$fn=48;

module pyramid()
{ // A pyramid
 polyhedron(points=[[0,0,0],[-height,-height,height],[-height,height,height],[height,height,height],[height,-height,height]],faces=[[0,1,2],[0,2,3],[0,3,4],[0,4,1],[4,3,2,1]]);
}


module pcb_hulled(h=pcbthickness,r=0)
{ // PCB shape for case
	if(useredge)outline(h,r);
	else hull()outline(h,r);
}

module solid_case(d=0)
{ // The case wall
	hull()
        {
                translate([0,0,-casebottom])pcb_hulled(height,casewall-edge);
                translate([0,0,edge-casebottom])pcb_hulled(height-edge*2,casewall);
        }
}

module preview()
{
	pcb();
	color("#0f0")parts_top(part=true);
	color("#0f0")parts_bottom(part=true);
	color("#f00")parts_top(hole=true);
	color("#f00")parts_bottom(hole=true);
	color("#00f8")parts_top(block=true);
	color("#00f8")parts_bottom(block=true);
}

module top_half(step=false)
{
	difference()
	{
		translate([-casebottom-100,-casewall-100,pcbthickness-lip/2+0.01]) cube([pcbwidth+casewall*2+200,pcblength+casewall*2+200,height]);
		if(step)translate([0,0,pcbthickness-lip/2-0.01])pcb_hulled(lip,casewall/2+fit);
	}
}

module bottom_half(step=false)
{
	translate([-casebottom-100,-casewall-100,pcbthickness+lip/2-height-0.01]) cube([pcbwidth+casewall*2+200,pcblength+casewall*2+200,height]);
	if(step)translate([0,0,pcbthickness-lip/2])pcb_hulled(lip,casewall/2-fit);
}

module case_wall()
{
	difference()
	{
		solid_case();
		translate([0,0,-height])pcb_hulled(height*2);
	}
}

module top_side_hole()
{
	intersection()
	{
		parts_top(hole=true);
		case_wall();
	}
}

module bottom_side_hole()
{
	intersection()
	{
		parts_bottom(hole=true);
		case_wall();
	}
}

module parts_space()
{
	minkowski()
	{
		union()
		{
			parts_top(part=true,hole=true);
			parts_bottom(part=true,hole=true);
		}
		sphere(r=margin,$fn=6);
	}
}

module top_cut()
{
	difference()
	{
		top_half(true);
		if(parts_top)difference()
		{
			minkowski()
			{ // Penetrating side holes
				top_side_hole();
				rotate([180,0,0])
				pyramid();
			}
			minkowski()
			{
				top_side_hole();
				rotate([0,0,45])cylinder(r=margin,h=height,$fn=4);
			}
		}
	}
	if(parts_bottom)difference()
	{
		minkowski()
		{ // Penetrating side holes
			bottom_side_hole();
			pyramid();
		}
			minkowski()
			{
				bottom_side_hole();
				rotate([0,0,45])translate([0,0,-height])cylinder(r=margin,h=height,$fn=4);
			}
	}
}

module bottom_cut()
{
	difference()
	{
		 translate([-casebottom-50,-casewall-50,-height]) cube([pcbwidth+casewall*2+100,pcblength+casewall*2+100,height*2]);
		 top_cut();
	}
}

module top_body()
{
	difference()
	{
		intersection()
		{
			solid_case();
			pcb_hulled(height);
			top_half();
		}
		if(parts_top)minkowski()
		{
			if(nohull)parts_top(part=true);
			else hull()parts_top(part=true);
			translate([0,0,margin-height])cylinder(r=margin,h=height,$fn=8);
		}
	}
	intersection()
	{
		solid_case();
		parts_top(block=true);
	}
}

module top_edge()
{
	intersection()
	{
		case_wall();
		top_cut();
	}
}

module top()
{
	translate([casewall,casewall+pcblength,pcbthickness+casetop])rotate([180,0,0])difference()
	{
		union()
		{
			top_body();
			top_edge();
		}
		parts_space();
		translate([0,0,pcbthickness-height])pcb(height,r=margin);
	}
}

module bottom_body()
{
	difference()
	{
		intersection()
		{
			solid_case();
			translate([0,0,-height])pcb_hulled(height);
			bottom_half();
		}
		if(parts_bottom)minkowski()
		{
			if(nohull)parts_bottom(part=true);
			else hull()parts_bottom(part=true);
			translate([0,0,-margin])cylinder(r=margin,h=height,$fn=8);
		}
	}
	intersection()
	{
		solid_case();
		parts_bottom(block=true);
	}
}

module bottom_edge()
{
	intersection()
	{
		case_wall();
		bottom_cut();
	}
}

module bottom()
{
	translate([casewall,casewall,casebottom])difference()
	{
		union()
		{
        		bottom_body();
        		bottom_edge();
		}
		parts_space();
		pcb(height,r=margin);
	}
}
bottom(); translate([spacing,0,0])top();
