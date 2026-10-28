// The same module three times with different inputs. Source locations alone
// cannot tell the instances apart, the connected nets can.
module acc (input clk, input [3:0] d, output reg [3:0] q);
  always @(posedge clk) q <= q + d;
endmodule

module top (input clk, input [3:0] a, b, c, output [3:0] x, y, z);
  acc u_a (.clk(clk), .d(a), .q(x));
  acc u_b (.clk(clk), .d(b), .q(y));
  acc u_c (.clk(clk), .d(c), .q(z));
endmodule
