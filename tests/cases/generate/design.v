// Instances created by a generate loop get names like "gen[0].u_stage".
module stage (input clk, input [3:0] d, output reg [3:0] q);
  always @(posedge clk) q <= d + 4'd3;
endmodule

module top (input clk, input [3:0] a, output [3:0] y);
  wire [3:0] chain [0:3];
  assign chain[0] = a;
  genvar i;
  generate
    for (i = 0; i < 3; i = i + 1) begin : gen
      stage u_stage (.clk(clk), .d(chain[i]), .q(chain[i + 1]));
    end
  endgenerate
  assign y = chain[3];
endmodule
