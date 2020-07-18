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
because they use a flash part) and one programmer. The modules themselves are more-or less a
break-out board for 29F800 and 29F400 IC that's electrically compatible with the originals.

The programmer is based on CH552T MCU that handles communication with a PC via
USB and also does the grunt work of programming the flash module itself. For simplicity
and for the sake of lower build complexity the programmer uses an existing MCU mini-board
(Electrodragon CH552 mini dev board) - that's the red part on the right part of the
programmer. The MCU mini board comes already built, it's cheap (~$2 as of now: 2020)
has 5V IO pin voltage, and is simple to use. It is soldered to the programmer via
through hole pins.

So, in order to opertate the programmer, you will need to compile the pc software
(or download the prebuild binary  - * not available just now) and also be able to
upload the firmware into the MCU mini board. That's done via USB itself, it's not 
very complicated once you have the correct tools. You can either install the whole
SDK for the CH552 chip (https://github.com/Blinkinlabs/ch554_sdcc.git) and compile
the firmware yourself, or you can just upload the firmware binary via wchisptool
that is available here (https://github.com/rgwan/librech551.git). Firmware upload
needs to be done only once, typically after you build the programmer board.


WIP - still more infor to come...









