// BCD decade counter: counts from 0 to 9, then wraps to 0
module counter_bcd(
  input clk,
  input reset,
  input enable,
  output reg [3:0] number);
  
  always @(posedge clk) begin
    if (reset || (enable && (number == 4'd9)) )
      number <= 4'd0;
    else if (enable)
      number <= number + 4'd1;
        //if enable is off, number keeps old value
  end
  
  
endmodule

//Hexadecimal countdown counter: counts from F down to 0, then wraps to F
module countdown_hex(
  input clk,
  input reset,
  input enable,
  output reg [3:0] number);
  
   always @(posedge clk) begin
     if (reset || (enable && (number == 4'h0)) )
       number <= 4'hF;
     //note: no underflow check is actually needed since it'd wrap around to F anyway
     else if (enable)
       number <= number - 4'h1;
     
   end
  
endmodule

// Binary counter: counts 0 to 9999 decimal, then wraps to 0
module counter_bin_9999(
  input clk,
  input reset,
  input enable,
  output reg [13:0] number
);

always @(posedge clk) begin
  if (reset)
    number <= 14'd0;
  else if (enable) begin
    if (number == 14'd9999)
      number <= 14'd0;
    else
      number <= number + 14'd1;
  end
  // if enable is off, number keeps old value
end

endmodule


//BCD Converter - used "Double Dabble" algorithm for optimization (avoid division/modulo)
module bcd_converter #(
  parameter width = 14,
  parameter digits = 4
)(
  input  [width-1:0] binary_num,
  output reg [digits*4-1:0] bcd
);

  localparam bcd_width  = digits * 4;
  localparam temp_width = width + bcd_width;

  reg [temp_width-1:0] bcd_version; // combined (BCD and original number) register for shifting
  integer i, j;

  always @(*) begin
    // initialize, load input value
    bcd_version = {temp_width{1'b0}};
    bcd_version[width-1:0] = binary_num;

	 // for each input bit, check each BCD digit, if a digit is 5 or greater,
    // add 3 before shifting the combined register left by 1.
    for (i = 0; i < width; i = i + 1) begin
      for (j = 0; j < digits; j = j + 1) begin
        if (bcd_version[width + j*4 +: 4] >= 4'd5)
          bcd_version[width + j*4 +: 4] = bcd_version[width + j*4 +: 4] + 4'd3;
      end

      bcd_version = bcd_version << 1;
    end

	 //extract the final BCD digits
    bcd = bcd_version[temp_width-1:width];
  end

endmodule