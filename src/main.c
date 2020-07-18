/***************************************************************
* CF840 flash IC programmer based on CH552T utilising 74HC595
* shift registers. Flashing is controlled via USB commands sent
* from the Host device.
*
* This code runs on CH552T MCU. 
* The PC Application code is in prog_pc.c file.
****************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ch554.h>
#include <ch554_usb.h>
#include <bootloader.h>
#include <debug.h>

#include "usb_desc.h"

// custom USB definitions, must be set before the "usb_intr.h" is included
#define EP0_BUFF_SIZE                       64
#define USB_CUST_VENDOR_ID                  0x16c0
#define USB_CUST_PRODUCT_ID                 0x05dc
#define USB_CUST_CONF_POWER                 120
#define USB_CUST_VENDOR_NAME_LEN            17
#define USB_CUST_VENDOR_NAME                { 'g','i', 't', 'h', 'u', 'b', '.','c', 'o', 'm', '/', 'o', 'l','e','0','0', 0}
#define USB_CUST_PRODUCT_NAME_LEN           13
#define USB_CUST_PRODUCT_NAME               { '2', '7', 'c', 'f', '8', '4', '0', '_', 'p', 'r', 'o', 'g', 0 }
#define USB_CUST_CONTROL_TRANSFER_HANDLER   handleVendorControlTransfer()
#define USB_CUST_CONTROL_DATA_HANDLER       handleVendorDataTransfer()

// function declaration for custom USB transfer handlers
static uint16_t handleVendorControlTransfer();
static void handleVendorDataTransfer();

// USB interrupt handlers
#include "usb_intr.h"

// GPIO PIN definition

#define PORT1 0x90
#define PORT3 0xb0

//CH552 ElectroDragon board LED - P1.4
#define PIN_LED 4
SBIT(LED, PORT1, PIN_LED);

//595 Store Clock - P3.5
#define PIN_ST_CLK 5
SBIT(ST_CLK, PORT3, PIN_ST_CLK);

//595 Shift clock 1  - P3.3
#define PIN_SH1_CLK 3
SBIT(SH1_CLK, PORT3, PIN_SH1_CLK);

//595 Shift clock 2 - P3.4
#define PIN_SH2_CLK 4
SBIT(SH2_CLK, PORT3, PIN_SH2_CLK);

//595 Serial data 1 - P3.1
#define PIN_SDATA1 1
SBIT(SDATA1, PORT3, PIN_SDATA1);

//READY signal from the flash chip - P3.0
#define PIN_FLREADY 0
SBIT(FLREADY, PORT3, PIN_FLREADY);

// CE# signal to the flash chip - P3.2
#define PIN_FLCE 2
SBIT(FLCE, PORT3, PIN_FLCE);


//U3 - data output controls

// Write enable: WE#
#define CTRL_WE   0x08

// Output enable: OE#
#define CTRL_OE   0x04

// Shift clock 1B control: SH1B - controls Shift clock on U2 (middle adrress bits A7 - A14)  
#define CTRL_SH1B   0x02

// LED1 control
#define CTRL_LED1 0x01




// USB commands received via Control endpoint EP0
#define CMD_SET_SHREG   0x10
#define CMD_SET_ADDR    0x20
#define CMD_SET_DATA    0x30
#define CMD_GET_DATA    0x40
#define CMD_WRITE       0x50
#define CMD_WRITE_SLOW  0x51
#define CMD_READ        0x60
#define CMD_BOOTLOADER  0xB0
#define CMD_SET_UP      0xF0

#define SETUP_MANUF_ID    0
#define SETUP_DEVICE_ID   1
#define SETUP_SECTOR_VERIFY  2
#define SETUP_ERASE       4
#define SETUP_ERASE_SECTOR 5
#define SETUP_READ        6
#define SETUP_WRITE       7
#define SETUP_READY      10

#define STATUS_INITIALISED    0x00
#define STATUS_ERASE          0x01
#define STATUS_ERASE_FAIL     0x02  
#define STATUS_PROGRAM        0x03
#define STATUS_PROGRAM_FAIL   0x04


// Controls the direction of the data port P1: eiter input (for reading) or output (for writing)
#define P1_DATA_OUT P1_DIR_PU = 0xFF
#define P1_DATA_IN  P1_DIR_PU = 0 

uint8_t rwBuffer[64];  //buffer for payload data transferred over USB

uint8_t command = 0;   //main command to execure: read / write /erase etc.
uint8_t addrBank = 0;  //top 4 bits of the 20bit address
uint8_t addrH = 0;     //middle 8 bits of the 20bit address
uint8_t addrL = 0;     //low 8 bits of the 20bit address
uint8_t ctrl = 0;      //control register - holds OE, WE, SH1B and LED bits
uint8_t data = 0;      //generic parameter set via usb interface
uint8_t status = 0;    //status of the read/write operation, sent back to the USB host 

static uint8_t writeData();
static void readData();
static void setShiftRegsCtrl();

/*******************************************************************************
* Jump to bootloader
*******************************************************************************/
static void jumpToBootloader()
{
    USB_INT_EN = 0;
    USB_CTRL = 0x6;
    EA = 0;
    mDelaymS(100);
    bootloader();
    while(1);
}



/*******************************************************************************
* Handler of the vendor Control transfer requests sent from the Host to 
* MCU via Endpoint 0
*
* Returns : the length of the response that is stored in Ep0Buffer  
*******************************************************************************/

static uint16_t handleVendorControlTransfer()
{
    // * up to 16 commands (4 bits) encoded in top nibble of UsbIntrSetupReq
    // * up to 36 data bits
    //    - 4 data bits in bottom nibble of UsbIntrSetupReq
    //    - 2 * 8 bits in UsbSetupBuf->wValueL and UsbSetupBuf->wValueH
    //    - 2 * 8 bits in UsbSetupBuf->wIndexL and UsbSetupBuf->wIndexH

	switch (UsbIntrSetupReq & 0xF0) {
    //set Shift register data
    case CMD_SET_SHREG : {
        ctrl = UsbSetupBuf->wValueL;
        command = CMD_SET_SHREG;
    } break;
    case CMD_SET_ADDR : {
        addrH = UsbSetupBuf->wValueH;
        addrL = UsbSetupBuf->wValueL;
        addrBank = UsbSetupBuf->wIndexL << 4;
        command = CMD_SET_ADDR;
    } break;
    case CMD_SET_DATA: {
        data = UsbSetupBuf->wValueL;
        command = CMD_SET_DATA;
    } break;
    case CMD_GET_DATA: {
        uint8_t* dst = (uint8_t*) Ep0Buffer;
        *dst = data;
        dst++;
        *dst = status;
        return 2; // transfer 2 bytes back to the host: data & status
    } break;
    //jump to bootloader
    case CMD_BOOTLOADER : {
        jumpToBootloader();
    } break;
    case CMD_SET_UP: {
        //lastAddr = 0xFFFF;
        addrH = UsbSetupBuf->wValueH;
        addrL = UsbSetupBuf->wValueL;
        addrBank = UsbSetupBuf->wIndexL << 4;
        data = UsbSetupBuf->wIndexH;
        command = CMD_SET_UP;
    } break;

    case CMD_WRITE: {
        addrH = UsbSetupBuf->wValueH;
        addrL = UsbSetupBuf->wValueL;
        addrBank = UsbSetupBuf->wIndexL << 4;
        data = UsbSetupBuf->wIndexH;
        // just wait for the data and confirm the transfer
    } break;
    case CMD_READ: {
        if ((UsbIntrSetupReq & 0xF) == 0) {
            addrH = UsbSetupBuf->wValueH;
            addrL = UsbSetupBuf->wValueL;
            addrBank = UsbSetupBuf->wIndexL << 4;
            //last addr is erased in SETUP_READ
            command = CMD_READ;
            return 0; 
        } else {
            memcpy(Ep0Buffer, rwBuffer, 64);
            return 64;
        }
    } break;
	default:
		return 0xFF; // Command not supported
		break;
	}
    return 0; // no data transfer back to the host
}

static void handleVendorDataTransfer()
{
    // Ah! The data to write just arrived.
    if (CMD_WRITE == UsbIntrSetupReq) {
        memcpy(rwBuffer, Ep0Buffer, 64);
        command = data ? CMD_WRITE_SLOW: CMD_WRITE;
    }
}


static void setupGPIO()
{

    P1_MOD_OC = 0;
    P1_DATA_OUT;

    P3_DIR_PU = 0x0;

    // set Serial data1 pin as GPIO Output
    P3_MOD_OC &= ~(1 << PIN_SDATA1);
    P3_DIR_PU |= (1 << PIN_SDATA1);

    // set store clock as GPIO Out 
    P3_MOD_OC &=  ~(1 << PIN_ST_CLK);
    P3_DIR_PU |= (1 << PIN_ST_CLK);

    // set shift clock1 as GPIO Out 
    P3_MOD_OC &=  ~(1 << PIN_SH1_CLK);
    P3_DIR_PU |= (1 << PIN_SH1_CLK);

    // set shift clock2 as GPIO Out 
    P3_MOD_OC &=  ~(1 << PIN_SH2_CLK);
    P3_DIR_PU |= (1 << PIN_SH2_CLK);

    // Flash CE pin - output
    P3_MOD_OC &= ~(1 << PIN_FLCE);
    P3_DIR_PU |= (1 << PIN_FLCE);

    // Flash READY pin - input
    P3_MOD_OC |= (1 << PIN_FLREADY);
    P3_DIR_PU |= (1 << PIN_FLREADY);

    SDATA1 = 0; 
    ST_CLK = 0;
    P1 = 0;
    SH1_CLK = 1;
    SH2_CLK = 1;
    FLCE = 1; //CE high (flash chip not enabled)
}

#if 0
// set 595 shift registers related to control (U3)
// only for illustartion of the asm version bellow
static void setShiftRegsSlow()
{
    ST_CLK = 0; //latch STC_CLK pin low

    SH2_CLK = 0; SDATA1 = ctrl & 0x1; SH2_CLK = 1; 
    SH2_CLK = 0; SDATA1 = ctrl & 0x2; SH2_CLK = 1; 
    SH2_CLK = 0; SDATA1 = ctrl & 0x4; SH22_CLK = 1; 
    SH2_CLK = 0; SDATA1 = ctrl & 0x8; SH2_CLK = 1; 
    SH2_CLK = 0; SDATA1 = ctrl & 0x10; SH2_CLK = 1;
    SH2_CLK = 0; SDATA1 = ctrl & 0x20; SH2_CLK = 1;
    SH2_CLK = 0; SDATA1 = ctrl & 0x40; SH2_CLK = 1;
    SH2_CLK = 0; SDATA1 = ctrl & 0x80; SH2_CLK = 1;

    ST_CLK = 1;	//latch ST_CLK pin high
}
#endif

// Set data on U3 - the Control 595 shift register.
// Shifting is done by toggling SH2 clock
static void setShiftRegsCtrl()
{

__asm
    //set ST_CLK low
    clr _ST_CLK

    // copy ctrl to MCU register B
    mov	b,_ctrl

    //pulse Shift register clock and set bit 0
    mov c, b[0]
    clr	_SH2_CLK
	mov	_SDATA1, c
    setb _SH2_CLK

    //pulse Shift register clock and set bit 1
    mov c, b[1]
    clr	_SH2_CLK
	mov	_SDATA1, c
    setb _SH2_CLK

    //pulse Shift register clock and set bit 2
	mov c, b[2]
    clr	_SH2_CLK
	mov	_SDATA1, c
    setb _SH2_CLK

    //pulse Shift register clock and set bit 3
    mov c, b[3]
    clr	_SH2_CLK
	mov	_SDATA1, c
    setb _SH2_CLK

    //pulse Shift register clock and set bit 4
    mov c, b[4]
    clr	_SH2_CLK
	mov	_SDATA1, c
    setb _SH2_CLK

    //pulse Shift register clock and set bit 5
    mov c, b[5]
    clr	_SH2_CLK
	mov	_SDATA1, c
    setb _SH2_CLK

    //pulse Shift register clock and set bit 6
    mov c, b[6]
    clr	_SH2_CLK
	mov	_SDATA1, c
    setb _SH2_CLK

    //pulse Shift register clock and set bit 7
    mov c, b[7]
    clr	_SH2_CLK
	mov	_SDATA1, c
    setb _SH2_CLK

    //set ST_CLK hihgh
    setb _ST_CLK
__endasm;
}

// Set data to both 595 shift registers (U1 & U2) by sending 16 bits to the
// U1. Because the U2 is daisy chained to U1, the data from U1 will be passed
// to U2. We don't reset U1 nor U2 - we just clock-in full 16 bits.
// Note CTRL_SH1B must be set Low, so that the U2 receives the Shift-Clock
// pulses through the OR gate.   
static void setShiftRegsAddr()
{
__asm
    //set ST_CLK low
    clr _ST_CLK

    // copy top addr to MCU register B
    mov	b,_addrH;

    //pulse Shift register clock and set bit 0
    mov c, b[7]
    clr	_SH1_CLK
	mov	_SDATA1, c 
    setb _SH1_CLK

    //pulse Shift register clock and set bit 1
    mov c, b[6]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 2
	mov c, b[5]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 3
    mov c, b[4]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 4
    mov c, b[3]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 5
    mov c, b[2]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 6
    mov c, b[1]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 7
    mov c, b[0]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //now the bottom address

    // copy top addr to MCU register B
    mov	b,_addrL;

    //pulse Shift register clock and set bit 0
    mov c, b[7]
    clr	_SH1_CLK
	mov	_SDATA1, c 
    setb _SH1_CLK

    //pulse Shift register clock and set bit 1
    mov c, b[6]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 2
	mov c, b[5]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 3
    mov c, b[4]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 4
    mov c, b[3]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 5
    mov c, b[2]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 6
    mov c, b[1]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 7
    mov c, b[0]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK


    //set ST_CLK high
    setb _ST_CLK
__endasm;
}

// Set data to 595 shift register U1 - low 8 bit of the address.
// The REG_SH1B must be set HIGH to disable SH clock on U2.
// This is a clock-count optimised version of the above in cases when
// the 'addrH' does not need to be changed.
static void setShiftRegsAddrLow()
{
__asm
    //set ST_CLK low
    clr _ST_CLK

     // copy top addr to MCU register B
    mov	b,_addrL;

    //pulse Shift register clock and set bit 0
    mov c, b[7]
    clr	_SH1_CLK
	mov	_SDATA1, c 
    setb _SH1_CLK

    //pulse Shift register clock and set bit 1
    mov c, b[6]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 2
	mov c, b[5]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 3
    mov c, b[4]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 4
    mov c, b[3]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 5
    mov c, b[2]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 6
    mov c, b[1]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK

    //pulse Shift register clock and set bit 7
    mov c, b[0]
    clr	_SH1_CLK
	mov	_SDATA1, c
    setb _SH1_CLK


    //set ST_CLK hihgh
    setb _ST_CLK
__endasm;
}


//Set low 16 bits of and address. Slow & convenient.
//Do not use it for anything time critical.
static void setAddr()
{
    //unset SHB1 -> U2 will be clocked for shifts
    ctrl &= ~CTRL_SH1B;

    ctrl &= 0x0F; // clear top address bits
    ctrl |= (addrBank); //set top-most address bits from the address bank


    setShiftRegsCtrl();  // this will set the top address bits and also apply the SHB1 bit
    setShiftRegsAddr();  // all lower 16 bits of the address are set because SH1B is set LOW
 }

//Write a byte to an address. Slow and convenient.
//Do not use it for anything time critical.
//Note: the addrBank is set to 0.
static void writeByte(uint16_t a, uint8_t d)
{
    //allows to pre-set the addr and bank value 
    if (a != 0) {
        addrH = a >> 8;
        addrL = a & 0xFF;
        //addrBank = (a >> 12) & 0xF;
        addrBank = 0;
    }

    // set the data to output port (all 8 bits at once)
    P1 = d;

    // set the address bits to address buss
    setAddr();

    //make a CE & WE pulse: bot low and then both high

    //Flash Chip enable low
    FLCE = 0;
    //set WE LOW    
    ctrl &= ~( CTRL_WE );
    // apply controls
    setShiftRegsCtrl();

    //set HI
    ctrl |= (CTRL_WE);  
    // apply controls
    setShiftRegsCtrl();
    //Flash Chip enable high
    FLCE = 1;
}

// Writes a buffer of 64 bytes to flash.
// This function does not use READY signal for checking whether
// the IC is ready to write another byte. Therefore we give enough
// time assuming the IC wrote the previous byte OK. If the flash 
// chip is very slow we might get errors.
static uint8_t writeDataSlow()
{
    uint8_t i = 0;
    //note: addr and addrBank must be already set
    uint8_t addrProgL = addrL;
    uint8_t addrProgH = addrH;
    uint8_t waitCnt;

    //ensure the direction of all pins of the data port is Out 
    P1_DATA_OUT;

    //unset SHB1 -> U2 will be clocked for shifts
    ctrl &= ~CTRL_SH1B;

    ctrl &= 0x0F; // clear top address bits
    ctrl |= (addrBank); //set top-most address bits from the address bank
    setShiftRegsCtrl();  // this will apply SHB1


    //WE# low - must be already set (via Setup command, before bulk write)

    while (i < 64)
    {
        //wait for ready high 
        //while (!READY){}

        //magic sequence: "write byte" 0xAAA:0xAA , 0x555:0x55, 0xAAA:0xA0
        // Target physical address is 0xAAA
        addrH = 0xA;
        addrL = 0xAA;
        setShiftRegsAddr();
 
        //set LOW   -> apply address 0xAAA   with data 0xAA 
        FLCE = 0;
        P1 = 0xAA;
        __asm nop
         nop __endasm;
        //set HI - latch data
        FLCE = 1;
        //mDelaymS(550); //for LED debug

        // addr = 0x555; we just shift previously set 0xAAA by one bit left and add 1 to get 0x555

        __asm
         clr _ST_CLK
         clr _SH1_CLK 
         setb _SDATA1
         nop
         setb _SH1_CLK
         setb _ST_CLK
         nop
        __endasm;
        
        //set LOW -> apply address 0x555   with data 0x55 
        FLCE = 0;
        P1 = 0x55;
        __asm nop
         nop __endasm;
        //set HI
        FLCE = 1;       
        //mDelaymS(550); //for LED debug


        // addr = 0xAAA; we just shift previously set 0x555 by one bit left and add 0 to get 0xAAA
        __asm 
         clr _ST_CLK
         clr _SH1_CLK 
         clr _SDATA1
         nop
         setb _SH1_CLK 
         setb _ST_CLK
        __endasm;

        //set LOW --> apply address 0xAAA    with data 0xA0
        FLCE = 0;
        P1 = 0xA0;
        __asm nop
         nop __endasm;
        //set HI  - latch data
        FLCE = 1;       
        //mDelaymS(550); //for LED debug
        
        //addr is now 0xAAA

        // Now write the actual byte to the flash memory
        addrL = addrProgL;
        addrH = addrProgH;
        setShiftRegsAddr();
        
        //set LOW - address is latched    
        FLCE = 0;
        // set the data bus
        P1 = rwBuffer[i];
        __asm
         nop __endasm;
        //set HI - data is latched
        FLCE = 1;

        //we need to wait while writing is in progress ~5us or longer
        waitCnt = 8;
        while (waitCnt--) {
            __asm nop
            nop
            nop
            nop
            nop
            nop
            nop
            nop
            nop
            nop
            nop
            nop
            nop
            nop
            nop
            nop __endasm;
        }

        //switch to the next address
        addrProgL++;
        i++;

        //mDelaymS(50); //for LED debug
    }

    //WE# high - only at the complete end of the bulk transfer
    return 0;
}

// Writes a buffer of 64 bytes to flash.
// This function uses READY signal for checking whether
// the IC is ready to write another byte. 
static uint8_t writeData()
{
    uint8_t i = 0;
    uint8_t safetyCnt;
    //note: addr and addrBank must be already set
    uint8_t addrProgL = addrL;
    uint8_t addrProgH = addrH;

    //ensure the direction of all pins of the data port is Out 
    P1_DATA_OUT;

    //unset SHB1 -> U2 will be clocked and bits shifted
    ctrl &= ~CTRL_SH1B;

    ctrl &= 0x0F; // clear top address bits
    ctrl |= (addrBank); //set top-most address bits from the address bank
    setShiftRegsCtrl();  // this will apply SHB1

    //WE# low - must be already set (via Setup command, before bulk write)

    while (i < 64)
    {

        //magic sequence: "write byte" 0xAAA:0xAA , 0x555:0x55, 0xAAA:0xA0
        // Target physical address is 0xAAA
        addrH = 0xA;
        addrL = 0xAA;
        setShiftRegsAddr();

        //wait until the flash chip is ready
        safetyCnt = 0xFF;
        while (!FLREADY && safetyCnt) {
            safetyCnt--;
        }

        //safety counter is depleted (Ready pin is stuck low) -> Error
        if (!safetyCnt) {
            return 1;
        }

        //set LOW   -> apply address 0xAAA   with data 0xAA 
        FLCE = 0;
        P1 = 0xAA;
        __asm nop
         nop __endasm;
        //set HI - latch data
        FLCE = 1;
        //mDelaymS(550); //for LED debug

        // addr = 0x555; we just shift previously set 0xAAA by one bit left and add 1 to get 0x555
        __asm
         clr _ST_CLK
         clr _SH1_CLK 
         setb _SDATA1
         nop
         setb _SH1_CLK
         setb _ST_CLK
         nop
        __endasm;
        
        //set LOW -> apply address 0x555   with data 0x55 
        FLCE = 0;
        P1 = 0x55;
        __asm nop
         nop __endasm;
        //set HI
        FLCE = 1;       
        //mDelaymS(550); //for LED debug


        // addr = 0xAAA; we just shift previously set 0x555 by one bit left and add 0 to get 0xAAA
        __asm 
         clr _ST_CLK
         clr _SH1_CLK 
         clr _SDATA1
         nop
         setb _SH1_CLK 
         setb _ST_CLK
        __endasm;

        //set LOW --> apply address 0xAAA  with data 0xA0
        FLCE = 0;
        P1 = 0xA0;
        __asm nop
         nop __endasm;
        //set HI  - latch data
        FLCE = 1;       
        //mDelaymS(550); //for LED debug
        
        //addr is now 0xAAA

        // Now write the actual byte to the flash memory
        addrL = addrProgL;
        addrH = addrProgH;
        setShiftRegsAddr();
        
        //set LOW - address is latched    
        FLCE = 0;
        // set the data bus
        P1 = rwBuffer[i];
        __asm
         nop __endasm;
        //set HI - data is latched
        FLCE = 1;

        //switch to next address 
        addrProgL++;
        i++;

        //mDelaymS(1);
        //mDelaymS(50); //for LED debug
    }

    //wait until the flash chip is ready
    safetyCnt = 0xFF;
    while (!FLREADY && safetyCnt) {
        __asm nop __endasm;
        safetyCnt--;
    }

    if (!safetyCnt) {
        return 1;
    }

    //WE# high - only at the complete end of the bulk transfer
    return 0;
}

// Reads a single byte from an address. This is not optimised for
// speed, so use it only for non time-critical stuff.
static void readByte(uint16_t a)
{
    //allows to preset the addr and the addrBank
    if (a) {
        addrH = a >> 8;
        addrL = a & 0xFF;
        addrBank = 0;
    }

    setAddr();

    FLCE = 0;
    //set LOW    
    ctrl &= ~CTRL_OE;
    // apply controls
    setShiftRegsCtrl();

    data = P1;

    //set HIGH    
    ctrl |= CTRL_OE;
    // apply controls
    setShiftRegsCtrl();
    FLCE = 1;
}

// Reads 64 bytes of data from the flash chip into the rwBuffer. 
static void readData()
{
    uint8_t i = 0;

    //note: addr and addrBank must be already set
    setAddr();

    //set SHB1 high -> U2 will NOT be clocked for shifts
    ctrl |= CTRL_SH1B;
    setShiftRegsCtrl();  // this will apply SHB1

    // CTRL_OE must be already low (via PC setup command)!
    // Note - CE low/high will also toggle OE low/high because of the OR gate

    while (i < 64) // len
    {
        //mDelaymS(50); //for LED debug
        //set Flash Chip Enable LOW    
        FLCE = 0;
        
        addrL++; // ~41 ns
        __asm nop __endasm; //~41 ns

        rwBuffer[i] = P1;
        i++;
 
        //Set Flash Chip enable HI.
        //The total time of the pulse is at least 160ns which is long enough for most of the flash chips
        //when the CH55X MCU runs at 24MHz
        FLCE = 1;

        //Set next address - set only Low 8 bits to be as quick as possible.
        //Note: we always read the initial address aligned on 64 byte boundaries
        //therefore the address change will never spill over to middle address bits 8..15 
        setShiftRegsAddrLow();
    }

    //CTRL_OE is set High at the end of the whole readinging 
}

// Waits till the erase procedure is finished. It also blinks a LED during erasing.
static void waitForErase()
{
    uint8_t cnt = 100; // wait up to 10 seconds

    mDelaymS(50);

    P1_DATA_IN;
    // erase does not take longer than few seconds
    // turn LED1 off
    ctrl &= ~CTRL_LED1;
    setShiftRegsCtrl();

    // read a byte - during programming we get an 'erase in progress' status
    // once we read 0xFF it means erasing finished and the vale 0xFF is
    // the actual value from the flash
    data = 0;
    while (data != 0xFF && cnt)
    {
        //blink LED. Note register is set during read byte
        if (cnt & 0x1) {
            ctrl |= CTRL_LED1;
        } else {
            ctrl &= ~CTRL_LED1;
        }
        readByte(1);
        mDelaymS(100);
        cnt--;
    }

    // turn LED1 on 
    ctrl |= CTRL_LED1;
    setShiftRegsCtrl();
    data = 0xFF;
    
    status = cnt ? STATUS_INITIALISED : STATUS_ERASE_FAIL;
}

// Set up the Flash chip for different operations based on the value in 'data' variable.
static void runSetUp() {

    uint8_t oldAddrH = addrH;
    uint8_t oldAddrL = addrL;
    uint8_t oldAddrBank = addrBank;

    //reset the device
    addrH = 0;
    addrL = 0;
    addrBank = 0;

    ctrl = CTRL_LED1 | CTRL_WE | CTRL_OE;
    setShiftRegsCtrl();

    //alternative reset - write 0xf0 byte to any address
    P1_DATA_OUT;
    writeByte(0x0f, 0xf0);

    addrH = 0;
    addrL = 0;
    addrBank = 0;

    //check ready
    if (data == SETUP_READY) {
        P1 = 0;
        return;
    }

    status = STATUS_INITIALISED;
    
    if (data == SETUP_READ) {
        //lastAddr = 0xFFFF;
        P1_DATA_IN;

        //set output enable LOW (active)    
        ctrl &= ~(CTRL_OE );
        // apply controls
        setShiftRegsCtrl();
        return;
    } else
    if (data == SETUP_WRITE) {
        //lastAddr = 0xFFFF;
        //set write enable LOW (active)    
        ctrl &= ~(CTRL_WE );
        // apply controls
        setShiftRegsCtrl();
        return;
    }

    //Set up for some more interesting operations...
    P1_DATA_OUT;
    //write a magic sequence:  so called "unlock cycles"
    writeByte(0xAAA, 0xAA);
    writeByte(0x1555, 0x55);
    
    //read manufacturer ID
    if (data == SETUP_MANUF_ID) {
        writeByte(0x2AAA, 0x90);
        P1_DATA_IN;
        readByte(0x100);
        status = 0xF0;
    }
    //read device ID 
    else if (data == SETUP_DEVICE_ID) {
        writeByte(0x2AAA, 0x90);
        P1_DATA_IN;
        readByte(0x102);
        status = 0xF1;
    }
    //read sector protection status
    else if (data == SETUP_SECTOR_VERIFY) {
        writeByte(0xAAA, 0x90);
        status = SETUP_SECTOR_VERIFY;
        addrBank = oldAddrBank;
        addrH = oldAddrH;
        addrL = oldAddrL | 4;
        P1_DATA_IN;
        readByte(0);
    }
    //start chip erase
    else if (data == SETUP_ERASE) {
        status = STATUS_ERASE;
        writeByte(0xAAA, 0x80);
        writeByte(0xAAA, 0xAA);
        writeByte(0x555, 0x55);
        writeByte(0xAAA, 0x10);

        waitForErase();
    }
    //start sector erase
    else if (data == SETUP_ERASE_SECTOR) {
        status = STATUS_ERASE;
        writeByte(0xAAA, 0x80);
        writeByte(0xAAA, 0xAA);
        writeByte(0x555, 0x55);
        //set the sector address
        addrBank = oldAddrBank;
        addrH = oldAddrH;
        addrL = oldAddrL;
        writeByte(0, 0x30);
        waitForErase();
    }
}

// The starting point of the program.
void main() {
    uint8_t tmp;

    CfgFsys();   // CH559 main frequency setup
    mDelaymS(5); // wait for the internal crystal to stabilize.

    setupGPIO();        
    USBDeviceCfg();

    //initialise the address
    addrH = 0;
    addrL = 0;
    addrBank = 0;

    FLCE = 1; // Flash chip enable HIGH (inactive)  

    // initial setup of controls 
    ctrl = CTRL_LED1 | CTRL_WE | CTRL_OE;
    setShiftRegsCtrl();

    //apply the address to the address bus
    setAddr();

    //apply the data 0 to the data bus
    P1 = 0x0;
 
    //poll for received USB commands and execute them
    while (1) {
        if (command == CMD_WRITE || command == CMD_WRITE_SLOW) {
            uint8_t slow = (CMD_WRITE_SLOW == command);
            command = 0;
            status = CMD_WRITE;

            //blink LED
            tmp = (addrH & 0x3F);
            if (tmp == 0) {
                ctrl &= ~(CTRL_LED1);
                setShiftRegsCtrl();
            }
            if (tmp == 0x20) {
                ctrl |= (CTRL_LED1);
                setShiftRegsCtrl();
            }
            status = (slow) ? writeDataSlow() : writeData();
        }
        else if (command == CMD_READ) {
            command = 0;
            status = CMD_READ;

            //blink LED
            tmp = (addrH & 0x3F);
            if ((addrH & 0x3F) == 0) {
                ctrl &= ~(CTRL_LED1);
                setShiftRegsCtrl();
            }
            if ((addrH & 0x3F) == 0x20) {
                ctrl |= (CTRL_LED1);
                setShiftRegsCtrl();
            }

            P1_DATA_IN;
            readData();
            status = 0;
        }
        else if (command == CMD_SET_UP) {
            command = 0;
            runSetUp();
        }
     
        // The rest of the commands is only for testing and debugging
	    else if (command == CMD_SET_SHREG) {
            command = 0;
            setShiftRegsCtrl();
	    }
        else if (command == CMD_SET_ADDR) {
            command = 0;
            setAddr();
        }
        else if (command == CMD_SET_DATA) {
            command = 0;
            P1_DATA_OUT;
            P1 = data;
        }
    }
}
