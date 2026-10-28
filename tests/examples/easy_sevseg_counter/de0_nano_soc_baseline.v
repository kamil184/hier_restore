 //--------------------------------------------------------------------------//
// Title:        de0_nano_soc_baseline.v                                       //
// Rev:          Rev 0.1                                                    //
// Last Revised: 09/14/2015                                                 //
//--------------------------------------------------------------------------//
// Description: Baseline design file contains DE0 Nano SoC    				 //
//              Board pins and I/O Standards.                               //
//--------------------------------------------------------------------------//
//Copyright 2015 Altera Corporation. All rights reserved.  Altera products
//are protected under numerous U.S. and foreign patents, maskwork rights,
//copyrights and other intellectual property laws.
//                 
//This reference design file, and your use thereof, is subject to and
//governed by the terms and conditions of the applicable Altera Reference
//Design License Agreement.  By using this reference design file, you
//indicate your acceptance of such terms and conditions between you and
//Altera Corporation.  In the event that you do not agree with such terms and
//conditions, you may not use the reference design file. Please promptly                         
//destroy any copies you have made.
//
//This reference design file being provided on an "as-is" basis and as an
//accommodation and therefore all warranties, representations or guarantees
//of any kind (whether express, implied or statutory) including, without
//limitation, warranties of merchantability, non-infringement, or fitness for
//a particular purpose, are specifically disclaimed.  By making this
//reference design file available, Altera expressly does not recommend,
//suggest or require that this reference design file be used in combination 
//with any other product not provided by Altera
//----------------------------------------------------------------------------

//Group Enable Definitions
//This lists every pinout group
//Users can enable any group by uncommenting the corresponding line below:
//`define enable_ADC
//`define enable_ARDUINO
`define enable_GPIO0
//`define enable_GPIO1
//`define enable_HPS


module de0_nano_soc_baseline(


	//////////// CLOCK //////////
	input 		          		CLOCK_50,
	input 		          		FPGA_CLK2_50,
	input 		          		FPGA_CLK3_50,

`ifdef enable_ADC
	//////////// ADC //////////
	/* 3.3-V LVTTL */
	output		          		ADC_CONVST,
	output		          		ADC_SCLK,
	output		          		ADC_SDI,
	input 		          		ADC_SDO,
`endif
	
`ifdef enable_ARDUINO
	//////////// ARDUINO ////////////
	/* 3.3-V LVTTL */
	inout					[15:0]	ARDUINO_IO,
	inout								ARDUINO_RESET_N,
`endif
	
`ifdef enable_GPIO0
	//////////// GPIO 0 ////////////
	/* 3.3-V LVTTL */
	inout				[35:0]		GPIO_0,
`endif

`ifdef enable_GPIO1	
	//////////// GPIO 1 ////////////
	/* 3.3-V LVTTL */
	inout				[35:0]		GPIO_1,
`endif

`ifdef enable_HPS
	//////////// HPS //////////
	/* 3.3-V LVTTL */
	inout 		          		HPS_CONV_USB_N,
	
	/* SSTL-15 Class I */
	output		    [14:0]		HPS_DDR3_ADDR,
	output		     [2:0]		HPS_DDR3_BA,
	output		          		HPS_DDR3_CAS_N,
	output		          		HPS_DDR3_CKE,
	output		          		HPS_DDR3_CS_N,
	output		     [3:0]		HPS_DDR3_DM,
	inout 		    [31:0]		HPS_DDR3_DQ,
	output		          		HPS_DDR3_ODT,
	output		          		HPS_DDR3_RAS_N,
	output		          		HPS_DDR3_RESET_N,
	input 		          		HPS_DDR3_RZQ,
	output		          		HPS_DDR3_WE_N,
	/* DIFFERENTIAL 1.5-V SSTL CLASS I */
	output		          		HPS_DDR3_CK_N,
	output		          		HPS_DDR3_CK_P,
	inout 		     [3:0]		HPS_DDR3_DQS_N,
	inout 		     [3:0]		HPS_DDR3_DQS_P,
	
	/* 3.3-V LVTTL */
	output		          		HPS_ENET_GTX_CLK,
	inout 		          		HPS_ENET_INT_N,
	output		          		HPS_ENET_MDC,
	inout 		          		HPS_ENET_MDIO,
	input 		          		HPS_ENET_RX_CLK,
	input 		     [3:0]		HPS_ENET_RX_DATA,
	input 		          		HPS_ENET_RX_DV,
	output		     [3:0]		HPS_ENET_TX_DATA,
	output		          		HPS_ENET_TX_EN,
	inout 		          		HPS_GSENSOR_INT,
	inout 		          		HPS_I2C0_SCLK,
	inout 		          		HPS_I2C0_SDAT,
	inout 		          		HPS_I2C1_SCLK,
	inout 		          		HPS_I2C1_SDAT,
	inout 		          		HPS_KEY,
	inout 		          		HPS_LED,
	inout 		          		HPS_LTC_GPIO,
	output		          		HPS_SD_CLK,
	inout 		          		HPS_SD_CMD,
	inout 		     [3:0]		HPS_SD_DATA,
	output		          		HPS_SPIM_CLK,
	input 		          		HPS_SPIM_MISO,
	output		          		HPS_SPIM_MOSI,
	inout 		          		HPS_SPIM_SS,
	input 		          		HPS_UART_RX,
	output		          		HPS_UART_TX,
	input 		          		HPS_USB_CLKOUT,
	inout 		     [7:0]		HPS_USB_DATA,
	input 		          		HPS_USB_DIR,
	input 		          		HPS_USB_NXT,
	output		          		HPS_USB_STP,
`endif
	
	//////////// KEY ////////////
	/* 3.3-V LVTTL */
	input				[1:0]			KEY,
	
	//////////// LED ////////////
	/* 3.3-V LVTTL */
	output			[7:0]			LED,
	
	//////////// SW ////////////
	/* 3.3-V LVTTL */
	input				[3:0]			SW

);


//Active-low reset from button 0
wire reset;
assign reset = ~KEY[0];

// KEY[1] acts as a run/pause control for the counter
wire key_enable;
assign key_enable = KEY[1];  //hold key (key=0) to pause/disable

// OneHertz is a one-clock-cycle enable pulse generated once per second
wire OneHertz;
wire [3:0] number;// holds the current 4-bit counter value


//generate a 1 Hz enable pulse from the 50 MHz board clock.
tick_1hz u_tick_1hz (
    .clk(CLOCK_50),
    .reset(reset),
    .tick(OneHertz)
);


// BCD decade counter counting upward from 0 to 9.
/*
counter_bcd u_counter_bcd (
    .clk(CLOCK_50),
    .reset(reset),
    .enable(OneHertz && key_enable),
    .number(number)
);
*/

// Hexadecimal countdown from F to 0.
///*
countdown_hex u_countdown_hex (
    .clk(CLOCK_50),
    .reset(reset),
    .enable(OneHertz && key_enable),
    .number(number)
);
//*/

//drive LEDs with the current counter value for quick debugging
assign LED[3:0] = number;
assign LED[7:4] = 4'b0000;

wire [7:0] segment;
wire [3:0] dig_en;
wire [15:0] display_value;
 
// Extend the 4-bit counter value to 16 bits for the display module.
assign display_value = {12'd0, number};// for now: just show 4-bit number

// Display mode selection: 0/1 to hex/decimal
wire mode;
assign mode = 1'b0; //currently fixed at hex

sevseg_display u_sevseg (
    .clk(CLOCK_50),
    .number(display_value),
    .mode(mode),
    .dig_en(dig_en),
    .segment(segment)
);
	
	
	
// Map segment control signals to the external seven-segment display

// /*
assign GPIO_0[0]  = segment[0];  // A
assign GPIO_0[1]  = segment[1];  // B
assign GPIO_0[2]  = segment[2];  // C
assign GPIO_0[3]  = segment[3];  // D
assign GPIO_0[4]  = segment[4];  // E
assign GPIO_0[5]  = segment[5];  // F
assign GPIO_0[6]  = segment[6];  // G
assign GPIO_0[7]  = segment[7];  // DP
// */

// Map digit-enable signals to the four display digits
///*
assign GPIO_0[8]  = dig_en[0]; // DIG4
assign GPIO_0[9]  = dig_en[1]; // DIG3
assign GPIO_0[10] = dig_en[2]; // DIG2
assign GPIO_0[11] = dig_en[3]; // DIG1
//*/

// fixed-output code for testing the display 
/*
assign GPIO_0[0]  = 1'b1; // A
assign GPIO_0[1]  = 1'b1; // B
assign GPIO_0[2]  = 1'b1; // C
assign GPIO_0[3]  = 1'b1; // D
assign GPIO_0[4]  = 1'b1; // E
assign GPIO_0[5]  = 1'b1; // F
assign GPIO_0[6]  = 1'b1; // G
assign GPIO_0[7]  = 1'b0; // DP off
*/ 
/*
assign GPIO_0[8]  = 1'b0; // enable only DIG4
assign GPIO_0[9]  = 1'b1;
assign GPIO_0[10] = 1'b1;
assign GPIO_0[11] = 1'b1;
*/

// Unused GPIO pins are left in high-impedance state.
assign GPIO_0[35:12] = {24{1'bz}};


// Previous button-counter test connection kept here for reference.
//counter my_counter(.clk(CLOCK_50), .button(KEY[1:0]), .count(LED[7:0]) ); //button counter
endmodule



