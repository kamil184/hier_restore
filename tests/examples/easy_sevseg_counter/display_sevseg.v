//Seven-segment display (from now: sevseg) helper functions

//4 digit formatters - hex and decimal
module formatter_4dig(
  input [15:0] number, //for <16 bits, like 4 in our case, pad with 0s before inputting
  input mode, //0 for hex, 1 for decimal
  output reg [3:0] digit0,
  output reg [3:0] digit1,
  output reg [3:0] digit2,
  output reg [3:0] digit3
);
  
  reg [15:0] temp; // used in the alternative decimal-conversion version
  
  wire [15:0] bcd_4dig;

	bcd_converter #(.width(16), .digits(4)) dec_conv (
	  .binary_num(number),
	  .bcd(bcd_4dig)  // module instances must be declared outside procedural blocks
	);
  always @(*) begin
  if (mode == 0) begin
    //max value: FFFF; 4 bits = 1 digit
     digit0 = number[3:0];
     digit1 = number[7:4];
     digit2 = number[11:8];
     digit3 = number[15:12];
  end
  else begin
    //max value: 9999 (that sevseg can handle) 
	 //In decimal mode, the binary input is first converted to 4-digit BCD with bcd_converter
	  digit0 = bcd_4dig[3:0];
     digit1 = bcd_4dig[7:4];
     digit2 = bcd_4dig[11:8];
     digit3 = bcd_4dig[15:12];
	 
	 //division/modulus version: simpler but not optimal
	 /*
    temp = number;
    
    // 16 bits goes up to 65k so handle values over 9999 with 0s 
    if (temp > 9999) begin
      digit0 = 4'd0;
      digit1 = 4'd0;
      digit2 = 4'd0;
      digit3 = 4'd0;
    end
    else begin
      digit3 = temp/1000;
      temp = temp % 1000;
      
      digit2 = temp/100;
      temp = temp % 100;
      
      digit1 = temp/10;
      temp = temp % 10;
      
      digit0 = temp;
    end
    */
  end
    
  end
  
  
endmodule



module sevseg_display(
  input clk, //refreshed on clock
  input [15:0] number,
  input mode,
  output reg [3:0] dig_en, 
  output reg [7:0] segment
); 
  
 
  //Only one digit is driven at a time because the 4-digit display is multiplexed
  
  // The formatted digits to be shown on the display
  wire [3:0] digit0, digit1, digit2, digit3;
  formatter_4dig digits(number, mode, digit0, digit1, digit2, digit3);
  
  reg [15:0] refresh_counter;
 
  initial begin
     refresh_counter = 16'd0;
  end
  
	// Use the first 2 bits of the refresh counter to cycle through the 4 digits
	//use lower 2 for simpler simulation!
  wire [1:0] scan_select;
  assign scan_select = refresh_counter[15:14]; //[1:0] for simulation

  // Value of the currently active digit
  reg [3:0] current_digit; 
  
  always @(posedge clk) refresh_counter <= refresh_counter + 16'd1;
  
  always @(*) begin
    dig_en = 4'b0000; // digit enable lines
    current_digit = 4'd0;
    
    case (scan_select)
      2'b00: begin
        current_digit = digit0;
        dig_en = 4'b1110; //low-active
      end
      2'b01: begin
        current_digit = digit1;
        dig_en = 4'b1101;
      end
      2'b10: begin
        current_digit = digit2;
        dig_en = 4'b1011;
      end
      2'b11: begin
        current_digit = digit3;
        dig_en = 4'b0111;
      end
    
    endcase
 
  end
  
  always @(*) begin
    case (current_digit)
      4'h0: segment = 8'b0_0111111;
      4'h1: segment = 8'b0_0000110;
      4'h2: segment = 8'b0_1011011;
      4'h3: segment = 8'b0_1001111;
      4'h4: segment = 8'b0_1100110;
      4'h5: segment = 8'b0_1101101;
      4'h6: segment = 8'b0_1111101;
      4'h7: segment = 8'b0_0000111;
      4'h8: segment = 8'b0_1111111;
      4'h9: segment = 8'b0_1101111;
      4'hA: segment = 8'b0_1110111;
      4'hB: segment = 8'b0_1111100;
      4'hC: segment = 8'b0_0111001;
      4'hD: segment = 8'b0_1011110;
      4'hE: segment = 8'b0_1111001;
      4'hF: segment = 8'b0_1110001;
      default: segment = 8'b0_0000000;
    endcase
end
endmodule

