// Four levels below the top. Every level registers its output, so synthesis
// cannot merge logic across instance boundaries.
module leaf (input clk, input [3:0] a, output reg [3:0] y);
  always @(posedge clk) y <= a ^ {a[2:0], a[3]};
endmodule

module level3 (input clk, input [3:0] a, output reg [3:0] y);
  wire [3:0] t;
  leaf u_leaf (.clk(clk), .a(a), .y(t));
  always @(posedge clk) y <= t + 4'd1;
endmodule

module level2 (input clk, input [3:0] a, output reg [3:0] y);
  wire [3:0] t;
  level3 u_l3 (.clk(clk), .a(a), .y(t));
  always @(posedge clk) y <= ~t;
endmodule

module level1 (input clk, input [3:0] a, output reg [3:0] y);
  wire [3:0] t;
  level2 u_l2 (.clk(clk), .a(a), .y(t));
  always @(posedge clk) y <= t & a;
endmodule

module top (input clk, input [3:0] a, output [3:0] y);
  level1 u_l1 (.clk(clk), .a(a), .y(y));
endmodule
