/* prog_pc - control app for CH55x based 27CF840 programmer
 *
 * Copyright (C) 2020 Ole
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Build with:
 *
 *      gcc -o prog_pc prog_pc.c -lusb-1.0 
 *
 * USB lib API reference:
 *     http://libusb.sourceforge.net/api-1.0
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#ifdef MINGW
#include <libusbx-1.0/libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif


#define VENDOR_ID 0x16c0 
#define PRODUCT_ID 0x05dc
#define VENDOR_NAME "github.com/ole00"
#define PRODUCT_NAME "27cf840_prog"

//see usb1.1 page 183: value bitmap: Host->Device, Vendor request, Recipient is interface
#define TYPE_OUT_ITF		0x41

//see usb1.1 page 183: value bitmap: Device->Host, Vendor request, Sender is interface
#define TYPE_IN_ITF		(0x41 | (1 << 7))


#define COMMAND_SET_SHREG 0x10
#define COMMAND_SET_ADDR  0x20
#define COMMAND_SET_DATA  0x30
#define COMMAND_GET_DATA  0x40
#define COMMAND_WRITE     0x50
#define COMMAND_READ      0x60

#define COMMAND_JUMP_TO_BOOTLOADER 0xB0
#define COMMAND_SETUP  0xF0

#define SETUP_VERIFY_PROTECT 2
#define SETUP_ERASE 4
#define SETUP_SECTOR_ERASE 5
#define SETUP_READ 6
#define SETUP_WRITE 7
#define SETUP_READY 10
#define SETUP_IDENTIFY 20

#define ACTION_PRINT_HELP			1
#define ACTION_SET_VERBOSE			2

static uint8_t descriptor[256];

static uint8_t outBuf[64]; //output (command) buffer
static uint8_t resBuf[64]; //input (response) buffer

static const char *const strings[2] = { "info", "fatal" };

static char fname[1024];


char debug = 0;
char verbose = 0;
int action = 0;
unsigned char srData1 = 0;
unsigned char data = 0;
unsigned int addr = 0;
int totalRead = 0;
uint16_t setupAddr = 0;
uint16_t setupAddrBank = 0;
uint16_t slowWrite = 0;

static void infoAndFatal(const int s, char *f, ...) {
    va_list ap;
    va_start(ap,f);
    fprintf(stderr, "prog_pc: %s: ", strings[s]);
    vfprintf(stderr, f, ap);
    va_end(ap);
    if (s) exit(s);
}

#define info(...)   infoAndFatal(0, __VA_ARGS__)
#define fatal(...)  infoAndFatal(1, __VA_ARGS__)

static void usage(void) {
    info("\n"
    "*** [prog_pc] *********************************************************\n"
    "27CF840 programmer tool\n"
    "ver. 0.3 by olin\n"
    "***********************************************************************\n"
    "usage: [sudo] prog_pc command [parameter]\n"
    "commands:\n"
    "  -h     : prints this help \n"
    "  -v     : set verbose mode \n"
    "  -debug : print USB library debugging info \n"
    "  -boot  : reset the CH55x into bootloader mode \n"
    "  -i     : identify chip: read vendor and chip ID\n"
    "  -r  X  : read X number of 64 byte sectors\n"
    "  -w  F  : write a file F to flash. The chip must be erased\n"
    "           before writing.\n"
    "  -erase : erase the whole chip\n"
    "  -vsp A : verify sector protect at adddress A\n"
    "  -ers A : erase sector at address A (see IC datasheet)\n"
    "  -slow  : optional parameter used along with -w\n"
    "           It will ignore READY signal from the Flash chip\n"
    "           during write operation. READY pin can be disconnected.\n"
    "\n"
    "commands for testing / troubleshooting of the board and modules\n"
    "  -c X   : send a byte to a control register\n"
    "  -dr    : read data byte and status\n"
    "  -dw X  : write data byte\n"
    "  -a  X  : set 20 bit address \n"
    "\n"
    "Examples:\n"
    "   prog_pc -i \n"
    "   prog_pc -erase \n"
    "   prog_pc -w rom.bin \n"
    "   prog_pc -r 16384 > flash_data.bin \n"
    "   prog_pc -w rom.bin -slow\n"
    );
    exit(1);

}

static int dumpBuffer(uint8_t* buf, int size) {
    int i;
    for (i = 0; i < size; i++) {
        printf("%02X ", buf[i]);
        if (i % 16 == 15) {
            printf("\n");
        }
    }
    printf("\n");
    return 0;
} 

static int sendControlTransfer(libusb_device_handle *h, uint8_t command, uint16_t param1, uint16_t param2, uint8_t len) {
    int ret;

    ret = libusb_control_transfer(h, TYPE_OUT_ITF, command, param1, param2, outBuf, len, 50);
    if (verbose) {
        info("control transfer out:  result=%i \n", ret);
    }
    return ret;
}

static int recvControlTransfer(libusb_device_handle *h, uint8_t command, uint16_t param1, uint16_t param2) {
    int ret;
    memset(resBuf, 0, sizeof(resBuf));

    ret = libusb_control_transfer(h, TYPE_IN_ITF, command, param1, param2, resBuf, sizeof(resBuf), 50);
    if (verbose) {
        info("control transfer (0x%02x) incoming:  result=%i\n", command, ret);
        dumpBuffer(resBuf, sizeof(resBuf));
    }
    return ret;
}

//try to find the programmer usb device
static libusb_device_handle* getDeviceHandle(libusb_context* c) {
    int max;
    int ret;
    int device_index = -1;
    int i;
    libusb_device** dev_list = NULL;
    struct libusb_device_descriptor des;
    struct libusb_device_handle* handle;

    ret = libusb_get_device_list(c, &dev_list);
    if (verbose) {
        info("total USB devices found: %i \n", ret);
    }
    max = ret;
    //print all devices
    for (i = 0; i < max; i++) {
        char vendorName[32];
        char productName[32];
        ret = libusb_get_device_descriptor(dev_list[i],  & des);
        if (des.idVendor == VENDOR_ID && des.idProduct == PRODUCT_ID) {

            //get the device handle in order to get the vendor name and product name
            ret = libusb_open(dev_list[i], &handle);
            if (verbose) {
                info("open device result=%i\n", ret);
            }
            if (ret) {
                libusb_free_device_list(dev_list, 1);
                fatal("device open failed\n");
            }

            //retrieve the texts
            vendorName[0] = 0;
            libusb_get_string_descriptor_ascii(handle, des.iManufacturer, vendorName, sizeof(vendorName));
            vendorName[sizeof(vendorName) - 1] = 0;
            productName[0] = 0;
            libusb_get_string_descriptor_ascii(handle, des.iProduct, productName, sizeof(productName));
            productName[sizeof(productName) - 1] = 0;

            libusb_close(handle);

            if (verbose) {
                info("device %i  vendor=%04x, product=%04x bus:device=%i:%i %s/%s\n",
                        i, des.idVendor, des.idProduct,
                        libusb_get_bus_number(dev_list[i]),
                        libusb_get_device_address(dev_list[i]),
                        vendorName, productName
                );
            }
            
            //ensure the vendor name and product name matches
            if (
                device_index == -1 &&
                strcmp(VENDOR_NAME, vendorName) == 0 &&
                strcmp(PRODUCT_NAME, productName) == 0
            ) {
                device_index = i;
            }
        }
    }

    if (device_index < 0) {
        libusb_free_device_list(dev_list, 1);
        fatal("no device found\n");
    }

    if (verbose) {
        info("using device: %i \n", device_index);
    }

    ret = libusb_open(dev_list[device_index], &handle);
    if (verbose) {
        info("open device result=%i\n", ret);
    }
    if (ret) {
        libusb_free_device_list(dev_list, 1);
        fatal("device open failed\n");
    }

    libusb_free_device_list(dev_list, 1);

    //get config
    ret = libusb_get_descriptor(handle, LIBUSB_DT_DEVICE, 0, descriptor, 18);
    if (verbose) {
        info("get device descriptor 0 result=%i\n", ret);
    }
    ret = libusb_get_descriptor(handle, LIBUSB_DT_CONFIG, 0, descriptor, 255);
    if (verbose) {
        info("get device configuration 0 result=%i\n", ret);
    }
    usleep(20*1000);

    return handle;
}


static void checkArgumentValue(int i, int argc, char** argv, char* fatalText) {
    if (i >= argc || argv[i][0] == '-') {
        fatal(fatalText);
    }
}

static void checkArguments(int argc, char** argv) {
    int i;
    char* arg;

    action = 0;
    fname[0] = 0;

    if (argc <= 1) {
        return;
    }
    //skip argument 0 which is the program name
    //process all arguments
    for (i = 1; i < argc; i++) {
        arg = argv[i];
        //all arguments start with dash
        if (arg[0] == '-') {
            if (strcmp("-h", arg) == 0) {
                action = ACTION_PRINT_HELP;
            } else
            if (strcmp("-v", arg) == 0) {
                verbose = 1;
            } else
            if (strcmp("-debug", arg) == 0) {
                debug = 1;
            } else
            if (strcmp("-vsp", arg) == 0) {
                int a;
                checkArgumentValue(i + 1, argc, argv, "-vsp: missing sector address\n");
                action = COMMAND_SETUP;
                data = SETUP_VERIFY_PROTECT; // verify sector protect
                a = (unsigned int) (strtol(argv[++i], NULL, 0) & 0xFFFFF);
                setupAddr = a & 0xFFFF;
                setupAddrBank = (a >> 16) & 0xFF;
            } else
            if (strcmp("-erase", arg) == 0) {
                action = COMMAND_SETUP;
                data = 4; // erase the whole chip
            } else
            if (strcmp("-ers", arg) == 0) {
                int a;
                checkArgumentValue(i + 1, argc, argv, "-ers: missing sector address\n");
                action = COMMAND_SETUP;
                data = 5; // erase sector
                a = (unsigned int) (strtol(argv[++i], NULL, 0) & 0xFFFFF);
                setupAddr = a & 0xFFF;
                setupAddrBank = (a >> 12) & 0xFF;
            } else
            if (strcmp("-c", arg) == 0) {
                checkArgumentValue(i + 1, argc, argv, "-c: missing data value\n");
                action = COMMAND_SET_SHREG;
                srData1 = (unsigned char) (strtol(argv[++i], NULL, 0) & 0xFF);
            } else
            if (strcmp("-a", arg) == 0) {
                checkArgumentValue(i + 1, argc, argv, "-a: missing address value\n");
                action = COMMAND_SET_ADDR;
                addr = (unsigned int) (strtol(argv[++i], NULL, 0) & 0xFFFFF);
            } else
            if (strcmp("-dw", arg) == 0) {
                checkArgumentValue(i + 1, argc, argv, "-dw: missing data value\n");
                action = COMMAND_SET_DATA;
                data = (unsigned char) (strtol(argv[++i], NULL, 0) & 0xFF);
            } else
            if (strcmp("-boot", arg) == 0) {
                action = COMMAND_JUMP_TO_BOOTLOADER;
            } else
            if (strcmp("-dr", arg) == 0) {
                action = COMMAND_GET_DATA;
            } else
            if (strcmp("-w", arg) == 0) {
                checkArgumentValue(i + 1, argc, argv, "-w: missing file name\n");
                action = COMMAND_WRITE;
                strcpy(fname, argv[++i]);
            } else
            if (strcmp("-r", arg) == 0) {
                checkArgumentValue(i + 1, argc, argv, "-r: missing number of sectors parameter\n");
                action = COMMAND_READ;
                totalRead = (int) strtol(argv[++i], NULL, 0);
            } else
            if (strcmp("-i", arg) == 0) {
                action = COMMAND_SETUP;
                data = SETUP_IDENTIFY;
            } else
            if (strcmp("-slow", arg) == 0) {
                slowWrite = 0x100;
            }

            else {
                fatal("unknown parameter: %s\n" , arg);
            }
        } else {
            fatal("unknown parameter: %s\n" , arg);
        }
    }
}

static int flashIoFinished(libusb_device_handle* h)
{
    int ret = recvControlTransfer(h, COMMAND_GET_DATA, 0, 0);
    if (ret != 2) {
        info("Get data/status failed. result=%i\n", ret); 
    }
    //printf("buf 1 = 0x%02x\n", resBuf[1]);

    return resBuf[1]; 
}

static int waitForFlashIoFinish(libusb_device_handle* h, int initialDelay, int step, int errorState)
{

    usleep(initialDelay);
    while (1){
        int ret = flashIoFinished(h);
        if (ret == 0) {
            return 0;
        }
        if (errorState != 0 && ret == errorState) {
            return ret;
        }
        usleep(step);
    }
}

/**
 * Writes a file to the flash IC starting at address 0.
 */
static int writeFlash(libusb_device_handle* h)
{
    FILE* f;
    int size = 1;
    int result = 0;

    uint32_t pos = 0;
    uint16_t addr = 0;
    uint16_t bank = 0;

    uint16_t index = SETUP_WRITE << 8;

    f = fopen(fname, "r");
    if (f) {
        // setup for Write -> set WE low
        int ret = sendControlTransfer(h, COMMAND_SETUP, 0, index, 0);
        if (verbose) {
            info("Setup write cmd result=%i\n", ret);
        }
        usleep(500);

        while (size > 0) {
            size = fread(outBuf, 1, sizeof(outBuf), f);

            if (size > 0) {
                //dumpBuffer(outBuf, size);
                int ret = sendControlTransfer(h, COMMAND_WRITE, addr, bank | slowWrite , 64);
                info("Write chunk result=%i (%s) %i addr=%04x bank=%02x \r", ret, ret == 64 ? "OK" : "Failed", pos, addr, bank);

                //usleep(1300 * 1000);
                if (0 != waitForFlashIoFinish(h, 1000, 100, 1)) {
                    info("\nError writing to flash at address=0x%06x \n", pos);
                    result = -1;
                    break;
                }
                pos += size;
                addr = pos & 0xFFFF; //16 bit base address
                bank = (pos >> 16) & 0xFF; // 4 bit top address bank
            }
        }
        printf("\n");
        index = SETUP_READY << 8;
        // setup for Ready - set WE high
        ret = sendControlTransfer(h, COMMAND_SETUP, 0, index, 0);
        if (verbose) {
            info("Init cmd result=%i\n", ret);
        }
        fclose(f);

    } else {
        printf("Error: failed to open file: %s\n", fname);
        return -1;
    }
    return result;    
}

/**
 * Reads a flash IC contents and outputs it on the standard output.
 */
static void readFlash (libusb_device_handle* h) 
{
    int i,j;
    uint16_t addr = 0;
    uint16_t bank = 0;
    uint16_t len = 64;
    uint32_t pos = 0;
    uint16_t index = SETUP_READ << 8;

    // setup for Read
    int ret = sendControlTransfer(h, COMMAND_SETUP, 0, index, 0);
    if (verbose) {
        info("Init cmd result=%i\n", ret);
    }
    usleep(50);

    while (i < totalRead) {
        i++;
        addr = pos & 0xFFFF; //16 bit base address
        bank = (pos >> 16) & 0xFF; //4 bit top address

        // initiates reading
        ret = sendControlTransfer(h, COMMAND_READ, addr, bank, 0);
        if (ret != 0) {
            info("Read set addr failed. result=%i\n", ret); 
        }
        info("Read chunk result=%i (%s) %i addr=%04x bank=%02x \r", ret, ret == 0 ? "OK" : "Failed", pos, addr, bank);

        //wait until the buffer is filled
        waitForFlashIoFinish(h, 50, 20, 0);
        //usleep(2000);

// reading of data from the flash chip to the MCU takes ~ 5.3 seconds
// set to 0 to test speeds (raw transfer of 1 MByte takes ~ 8 seconds, that is 128kb /s - speed is 1 MBit/s)
// USB 1.1 full speed is 12 MBits / sec. Try using BULK endpoints ?
#if 1
        // transfers the buffer with data
        ret = recvControlTransfer(h, COMMAND_READ | 1, addr, bank);
        if (ret != len) {
            info("Get data failed. result=%i\n", ret); 
        } else {
            //info("Data read: 0x%02x  status: 0x%02x\n", resBuf[0], resBuf[1]); 
        }
        //dumpBuffer(resBuf, sizeof(resBuf));
        for (j = 0; j < 64; j++) putc(resBuf[j], stdout);
#endif
        pos += 64;
    }
    info("\n");

    index = SETUP_READY << 8;

    // setup for Ready - set OE high
    ret = sendControlTransfer(h, COMMAND_SETUP, 0, index, 0);
    if (verbose) {
        info("Init cmd result=%i\n", ret);
    }
    usleep(50);
}

/**
 * Retrieves data and status bytes from the flash IC.
 */
static int commandGetData(libusb_device_handle* h, char printResult)
{
    int ret = recvControlTransfer(h, COMMAND_GET_DATA, 0, 0);
    if (ret != 2) {
        info("Get data failed. result=%i\n", ret); 
    } else {
        if (printResult) {
            info("Data read: 0x%02x  status: 0x%02x\n", resBuf[0], resBuf[1]);
        } else {
            ret = 0;
        }
    }
    return ret;
}

/**
 * Retrieves the vendor ID and product ID of the flash chip
 */
static int runIdentifyFlashChip(libusb_device_handle* h)
{
    int ret;
    uint16_t addr = 0;
    uint16_t index = 0;
    uint8_t vendorId = 0;
    uint8_t productId = 0;

    //read Vendor Id
    ret = sendControlTransfer(h, COMMAND_SETUP, addr, index, 0);
    if (ret != 0) {
        info("Control transfer failed. result=%i\n", ret);
        return ret; 
    }
    usleep(50 * 1000);
    //read back the value
    ret = commandGetData(h, 0);
    if (ret) {
        return ret;
    }
    vendorId = resBuf[0];

    //read Product Id
    index = 1 << 8;
    ret = sendControlTransfer(h, COMMAND_SETUP, addr, index, 0);
    if (ret != 0) {
        info("Control transfer failed. result=%i\n", ret);
        return ret; 
    }
    usleep(50 * 1000);
    //read back the value
    ret = commandGetData(h, 0);
    if (ret) {
        return ret;
    }
    productId = resBuf[0];

    info("VendorId: 0x%02x  ProductId: 0x%02x\n", vendorId, productId);
    return 0;
}

/**
 * Sets up the Flash IC for reading , writing operations etc.
 * Also it erases the flash chip contents here.
 */
int runSetupCommand(libusb_device_handle* h)
{
    int ret;
    uint16_t addr = 0;
    uint16_t addrBank = 0;
    uint16_t index = data;

    if (data == SETUP_IDENTIFY) {
        return runIdentifyFlashChip(h);
    }

    // verify sector protect or sector erase
    if (data == SETUP_VERIFY_PROTECT  || data == SETUP_SECTOR_ERASE) {
        addr = setupAddr;
        addrBank = setupAddrBank; 
        printf("sector addr=%04x bank=%2x\n", addr, addrBank);
    }

    index <<= 8;
    index |= addrBank;
    ret = sendControlTransfer(h, COMMAND_SETUP, addr, index, 0);
    if (verbose) {
        info("Init cmd result=%i\n", ret);
    }
    if (ret == 0) {
        //wait for erase finished (takes ~ 5 secs for the full erase);
        if (data == SETUP_ERASE || data == SETUP_SECTOR_ERASE) {
            int result;
            printf("Erasing %s ...\n", data == SETUP_SECTOR_ERASE ? "sector": "full chip");
            result = waitForFlashIoFinish(h, 1 * 1000 * 1000, 500 * 000, 2);
            if (0 == result) {
                printf("done\n");
            } else {
                printf("failed\n");
                ret = 1;
            }
        } else {
            usleep(100 * 1000);
            //read back the value
            commandGetData(h, 1);
        }
    }
    return ret;
}

/**
 * Main entry point.
 */
int main(int argc, char** argv) {
    libusb_context* c = NULL;
    libusb_device_handle *h;
    int offset = 0, size = 0;
    int ret;
    int i;

    checkArguments(argc, argv);
    if (action == 0 || action == ACTION_PRINT_HELP) {
        usage();
    }

    //initialize libusb 
    if (libusb_init(&c)) {
        fatal("can not initialise libusb\n");
    }

    //set debugging state
    if (debug) {
        libusb_set_debug(c, 4);
    }

    //get the handle of the connected USB device
    h = getDeviceHandle(c);

    //try to detach existing kernel driver if kernel is already handling 
    //the device
    if (libusb_kernel_driver_active(h, 0) == 1) {
        if (verbose) {
            info("kernel driver active\n");
        }
        if (!libusb_detach_kernel_driver(h, 0)) {
            if (verbose) {
                info("driver detached\n");
            }
        }
    }


    //set the first configuration -> initialize USB device
    if (libusb_set_configuration (h, 1) != 0) {
        fatal("cannot set device configuration\n");
    }

    if (verbose) {
        info("device configuration set\n");
    }
    usleep(20 * 1000);

    //get the first interface of the USB configuration
    if (libusb_claim_interface(h, 0) < 0) {
         fatal("cannot claim interface\n");
    }

    if (verbose) {
        info("interface claimed\n");
    }

    if (libusb_set_interface_alt_setting(h, 0, 0) < 0) {
        fatal("alt setting failed\n");
    }

    switch(action) {
        case COMMAND_SET_SHREG : {
            int ret;
            ret = sendControlTransfer(h, COMMAND_SET_SHREG, srData1, 0, 0);
            info("Set control register data (0x%02x) result=%i\n", srData1, ret);
        } break;
        case COMMAND_SET_ADDR : {
            int ret;
            ret = sendControlTransfer(h, COMMAND_SET_ADDR, (uint16_t)(addr & 0xFFFF), (uint16_t)((addr >> 16) & 0xF), 0);
            info("Set addr (0x%05x) result=%i\n", addr, ret);
        } break;
        case COMMAND_SET_DATA : {
            int ret;
            ret = sendControlTransfer(h, COMMAND_SET_DATA, data, 0, 0);
            info("Set data (0x%02x) result=%i\n", data, ret);
        } break;
        case COMMAND_GET_DATA : {
            commandGetData(h, 1);
        } break;

        case COMMAND_WRITE : {
            writeFlash(h);
        } break;

        case COMMAND_READ : {
            readFlash(h);
        } break;

        case COMMAND_SETUP : {
            runSetupCommand(h);
        } break;
        case COMMAND_JUMP_TO_BOOTLOADER : {
            sendControlTransfer(h, COMMAND_JUMP_TO_BOOTLOADER, 0, 0, 0);
        } break;
    } //end of switch

    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(c);
    return 0;
}
