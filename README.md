# 27cf840_prog

![Board image](https://github.com/ole00/27cf840_prog/raw/master/img/prog.jpg "27cd840 programmer")

This is a 27c800 and 27c400 EPROM replacement module. The replacement module 
uses a flash IC (29f800 or 29f400) that is electrically compatible with the
originals and allows the flash content to be quickly reprogrammed by a 
cheap custom USB programmer. 

While the original 27C800 and 27c400 IC chips are still available to buy, it's not
very convenient to reprogram them. They need to be erased in a UV Eraser before
being programmed which generally takes 15 - 20 minutes. They also need specific programmer
that supports high number of pins. There are some EPROM adapters that allow to program
the IC in more common programmers, but they are not particularly cheap.

I personally don't have one of those high-pin count EPPROM programmers and also did
not want to buy an adapter, therefore I decided to build my own 27C800 replacement
modules with a programmer. The goal was to make them as cheap as possible and
design something that's easy to build even for a hobbyist with intermediate skills.

Q: Why would anybody want to build it?

A: Well, if you want to develop a software for retro systems that use those IC and
you don't have an EPROM programmer nor UV Eraser, this programmer might save
you some bucks. Also, reprogramming will take significantly less time than
reprogramming an EPROM chip.

Q: I'm in, what should I do and how should I start?

A: read on!

## How it works

You'll need to build one or more IC replacement modules (I call them 27cf800 and 27cf400,
because they use a flash part) and one programmer. The modules themselves are more-or-less a
break-out board for 29F800 and 29F400 IC that's electrically compatible with the originals.

The programmer is based on CH552T MCU that handles communication with a PC via
USB and also does the grunt work of programming the flash module itself. For simplicity
and for the sake of lower build complexity the programmer uses an existing MCU mini-board
(Electrodragon CH552 mini dev board) - that's the red bit on right hand side edge of the
programmer (see img/proj.jpg). The MCU mini board comes already built, it's cheap (~$2
as of now: 2020) has 5V IO pin voltage, and is simple to use. It is soldered to the programmer via
through hole pins. 

So, in order to setup and opertate the programmer, you will need to:

* compile and upload the ch552T firmware (or download a prebuilt binary - not available right now).
Firmware upload needs to be done only once, typically after you build the programmer board.
That's done via USB itself, it's not very complicated once you have the right tools.
You can either install the whole SDK for the CH552 chip (https://github.com/Blinkinlabs/ch554_sdcc.git)
and compile the firmware yourself, or you can just upload the firmware binary via wchisptool
that is available here (https://github.com/rgwan/librech551.git). I'll precompile and release the
firmware for your convenience as well.

* compile the pc software (or download a prebuild binary - not available just now). 
To compile the pc tool the 'compile_pc.sh' shell script. It expects you have gcc and libusb-1.0
installed on you PC. This should work on Linux (including Raspberry Pi), MacOS and other OS'es.
I'll precompile and release Win64 binaries for your convenience as well. 

## Uploading firmware to programmer's MCU

Uploading firmware to MCU is done only once, typically after you build the programmer board or 
if there is a firmware update. The MCU mini-board has two pins next to MCU itself marked
as PROG, these need to be shorted/connected together (by a metal paper clip or by using fine tweezers) while
the module is plugged in to your PC. That will force the MCU to stay in bootloader,
which allows the firmware to be uploaded via USB. To verify the  MCU is in bootloader use
'lsusb' command and check the device 4348:55e0 is in the printed list. 
<pre>
Device 017: ID 4348:55e0 WinChipHead
</pre>

Then you need to upload the firmware binray like that:
<pre>
sudo ./wchisptool -f path/cf840prog.bin -g
</pre>

If the command succeeds you should see the following text on the console:
<pre>
Libre CH551 Flasher 2018
Detected device CH552
Device rom capacity 16384, data flash capacity 128
Binary length 4043
Device bootloader version: 1.1

Now performing erase...
Erase done
Writting Flash
Write done
Verifing Flash
Verify done
Let target run, keep it at Hong Kong reporter speed!

Excited! 0..0
</pre>

And if you check the output of 'lsusb' command you should see the following line:

<pre>
Device 018: ID 16c0:05dc Van Ooijen Technische Informatica shared ID for use with libusb
</pre>
That's the programmers identifier using a free vendor/product USB id for libusb. If you
can see the line, it means the MCU firmware installed correctly (of course, make sure
no other USB device using the same vendor/product id is plugged-in to your PC).

## Reading and writing the flash chip contents

To operate the programmer you'll need to use the 'prog_pc' tool. You can either compile it
yourself or download it precompiled (to be done..). If you run the tool
without parameters it prints a help text descriibing the commands and parameters.

The first command you should use (after you've built the programmer and installed
the MCU firmware) is '-i' command to identify the chip inserted in the 
programmer's socket. You can run it even if there is no chip inserted, just to verify
the tool communicates with the programmer.

If the programmer is not properly installed/set-up it will print the following line:
<pre>
./prog_pc -i
prog_pc: fatal: no device found
</pre>

If the programmer is recognised it will print an information about the chip inserted
in the socket.
<pre>
prog_pc: info: VendorId: 0xc2  ProductId: 0xab
</pre>

If there is no chip in the socket it will print a grabage number. That is a confirmation
the programmer hardware communicates with the programmer tool.
<pre>
prog_pc: info: VendorId: 0x88  ProductId: 0x88
</pre>

Using the rest of the commands is pretty straightforward.

* To read the chip you need to specify the number of blocks to read. The programmer uses sector
size of 64 bytes. Therefore for 1 Mbyte module use 16384 blocks, for 512 Kbyte module use
8192 number like that:
  <pre>
  ./prog_pc -r 16384 > data.bin
  </pre> 
  The '-r 16384' means to read 16384 blocks and '> data.bin' means to store the output
into data.bin file.

* To write a file into the flash chip module you need to erase it first. Use the following
command:
  <pre>
  ./prog_pc -erase
  </pre>
  The erasing takes about 6 - 8 seconds to finish and the on board LED flashes.
  If the rease fails the tool prints the following line:
  <pre>
  Erasing full chip ...
  failed
  </pre>
  See troubleshooting for possible reasons of failure.
  You can verify the chip is erased by reading the flash contents (-r command) and checking
  all the bytes are set to 0xFF.
  
  Now, after erase  you can write your data into the flash chip module by using '-w' commmand:
  <pre>
  ./prog_pc -w rom.bin -slow
  </pre>
  It will attempt to write the full contents of the file into the flash chip starting at offset 0.
  During writing a progress statistic is printed on the console. Writing does not check the written
  content so after the writing is finished you should read the contents back and compare it via
  'cmp' command. The '-slow' parameter is a compatibility option, it lets you to use flash 
  chip modules without the Ready/Busy pin being connected.
  
  See 'Building flash modules' for more information about Ready/Busy signal.

## Building flash modules

Flash modules use 29F800 (1 MByte) or 29F400 (512 kByte) SOP IC chip for storing the data. You should be 
able to use any brand, they seem to use the same programming protocol. Just make sure they are 5V 
compatible. So far I've used Macronix MX29F800 and MX29F400, Fujitsu MBM29F800 and Amd AM29F800
and AM29F400. There are other brands like ST (M29F800), Hynix HY29F800 and others I have not tested. 

When chosing the chip ensure its speedrating matches the speed rating of the chip you try to replace.
The speed rating is specified after dash of the IC part name. For example -55 means speed 
of 55ns (~18 MHz bus frequency), -75 is 75ns (~13 Mhz bus frequency)  90ns is 90ns (~11 MHz bus frequency).
If you don't know what speed is required then as a rule of thumb use the speed/frequency that
is higher or equal than the CPU frequency on your target system. So, for example, if your target
system (let's say an arcade board) runs at 12 MHz you can use 55ns or 75ns but don't use 90ns
or 120 ns chip.

Right now I've designed modules that can use the SOP package (1.27 mm pin pitch), to make the 
soldering easier for hobyists. Unfortunatley the SOP package does not fit entirely on the DIP module,
therefore the module PCB has a small lip (or overhang) on one side to accomodate the big SOP
chip. There are 2 versions of the same module board - version a and b, where the lip is either
on the bottom or the top part of the board. Chose the version that fits better (in terms of space)
on your target board. The module construction requires a specific procedure to make everything fit.

See the following image:

![module construction](https://github.com/ole00/27cf840_prog/raw/master/img/module_construction.jpg "27CF400 module construction")

You can see on the image that the middle pins close to the lip need to be trimmed off and soldered
flush with the board before the Flash IC is installed. What I do is I trimm the pins before I insert
them to the module board and fill the pin cavity with hot solder to make a small bump. Then I level the
bump with a soldering wick. You can also do the continuity test of the trimmed pins before you proceed.
Then you can solder the Flash IC on the module board.

There are 2 extra pin holes marked as WE# and RDY. If you are building 29CF800 module you can ignore
both. If you are building 29CF400 module you will need to connect the WE# pin to the programmer 
socket for any operation except reading (required for identification, erasing, writing etc.). The 
RDY pin is optional, it can speed up writing by up to ~20% and also provides more reliable writing
operation.  If you chose not to use the RDY pin then wou have to use '-slow' parameter during writing.

In summary: 
* 29CF800 module does not require WE# nor RDY pin holes to be connected. But using RDY pin ensures
  more reliable writing.
* 29CF400 module requires WE# pin connected. RDY pin is optional.

To use the pins you connect the WE# pin hole by a wire to the top left (either of the first 3) pins
in the programmers socket. And for RDY pin you do the same, just use the bottom left (either of
the first 3) pin in the programmers socket. I solder a small wire loop to the pin holes and use
test clips to secure the connection during programming, but any reliable connection should do
the job. Once the programming is done, you can remove the wires from the WE# or RDY pins (if you 
use them at all). No extra wires need to be used when the module is in the target board.

Wiring during programming the 29CF400 module:

![module wiring](https://github.com/ole00/27cf840_prog/raw/master/img/module_wiring.jpg "27CF400 module wiring")

You can order the module PCBs in you favourite PCB fab service. The gerber files are located in
gerbers directory. Use either 27cf840a.zip (lip on the bottom) or 27cf840b.zip (lip on the top) or
use 27cf840_rep_0_3ab_fab.zip which contains both of them (2 x a + 2 x b) panelised. 
These are 2 layer boards that fit within 100 x 100 mm limit.

Note that the 27cf400 module needs to be 2 pins shorter than 27cf800 module. You can use 27cf400a.zip
or 27cf400b.zip gerbers, or you can use 27cf840*.zip gerbers/boards and then saw off the extra board 
area by using jewelers saw. There is a dash-line cut indication on the board if you want to do that.


## Building the programmer

Buidling the programmer should be straightforward. The CH552T MCU module can be bought here:
https://www.electrodragon.com/product/ch552-ch554-mini-dev-board-ch55x-series/
Make sure to select 552 option. The 554 option might work as well, but it is untested.
Soldering of the module is done via 0.1" pin headers inserted in between the MCU module
and the programmer board.
There's few SOIC chip and several SMT passives on the board. All of them are relatively
big, so they are easy to solder. The recommendation is to solder them in the order from smallest to
biggest size. Note that the on-board LED ground pin is a bit harder to solder
(sepcially when using SMT LED) due to the ground plane. For the socket you can use either a 
48 pin (2 x 24) ZIF socket or you can use 2 pieces of wide 2 x 12 DIP sockets which are cheaper, but
the module insertion and removal is riskier (bent pins) and less comfortable. DIP sockets
are probably OK if you only ever need to programm a few ICs.

The gerbers for the programmer board can be found in the gerbers directory, use 
27cf840_prog_0_4_1_fab.zip file and upload it to the PCB fab service of your choice.
It's a 2 layer board and it fits into 100 x 100 mm area, so it should be cheap to produce.

## Design and schematics

The design files and schematic can be found in the 'design' directory. Schematic was produced in kicad's 
eeschema (free) tool, the PCB files were designed in gEDA-PCB (free) desgin tool. Feel free to use
and improve the design to fit your purpose.


## Troubleshooting

Q: I've built the module and it does not seem to work

A: Ensure the pins on the Flash IC  are well soldered and there is 
a continuity between them and the the output pins. Use multi meter to verify that -
you should be able to hear a beep on all of the output pins.

A: Ensure there are not solder bridges. Use multi meter to verify that.

A: Ensure the IC chip orientation is correct. The pin 1 is marked with a small 
dot on the module PCB.

----

Q: My module still does not work and I've verified the pin soldering
and continuity.

A: Ensure the module is correctly seated in the socket. Check the images
above for reference, or check the sikscreen diagram.

A: Ensure the chip is erased before writing.

----

Q: My module fails when erasing the IC

A: If you are using 27CF400 module, ensure that the WE# pin is wired
to socket. See above for wiring picture.

---

Q: Some bytes are incorrectly written to the IC Chip

A: Ensure the Flash chip is erased before writing.

A: If you are not using the RDY pin wire, then you have to
use '-slow' parameter during write.

A: Optionally use the RDY pin wire. Erase the IC and 
then write the content without '-slow' parameter






