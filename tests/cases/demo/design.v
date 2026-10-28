module adder #(parameter W = 8) (input [W-1:0] a, b, output [W:0] s);
  assign s = a + b;
endmodule

module alu (input [7:0] a, b, input sel, output [8:0] y);
  wire [8:0] sum;
  wire [7:0] logic_out;
  adder #(.W(8)) u_add (.a(a), .b(b), .s(sum));
  logic_unit u_logic (.a(a), .b(b), .y(logic_out));
  assign y = sel ? {1'b0, logic_out} : sum;
endmodule

module logic_unit (input [7:0] a, b, output [7:0] y);
  assign y = (a & b) ^ (a | b);
endmodule

module counter (input clk, rst, output reg [7:0] q);
  always @(posedge clk) q <= rst ? 8'd0 : q + 8'd1;
endmodule

module top (input clk, rst, sel, input [7:0] b, output [8:0] y);
  wire [7:0] cnt;
  counter u_cnt (.clk(clk), .rst(rst), .q(cnt));
  alu u_alu (.a(cnt), .b(b), .sel(sel), .y(y));
endmodule
