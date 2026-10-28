// Logic in the top module itself, a registered child and a child that only
// forwards its input and leaves no cells after synthesis.
module passthru (input [3:0] a, output [3:0] y);
  assign y = a;
endmodule

module cnt (input clk, input en, output reg [3:0] q);
  always @(posedge clk) if (en) q <= q + 4'd1;
endmodule

module top (input clk, input en, input [3:0] a, output reg [3:0] y);
  wire [3:0] p, q;
  passthru u_pass (.a(a), .y(p));
  cnt u_cnt (.clk(clk), .en(en), .q(q));
  always @(posedge clk) y <= p ^ q;
endmodule
