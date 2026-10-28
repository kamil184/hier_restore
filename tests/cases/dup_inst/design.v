// Two instances of the same module with identical inputs. Synthesis keeps one
// copy of the logic, which then belongs to both instances.
module inv_reg (input clk, input [3:0] d, output reg [3:0] q);
  always @(posedge clk) q <= ~d;
endmodule

module top (input clk, input [3:0] a, output [3:0] x, y);
  inv_reg u_p (.clk(clk), .d(a), .q(x));
  inv_reg u_q (.clk(clk), .d(a), .q(y));
endmodule
