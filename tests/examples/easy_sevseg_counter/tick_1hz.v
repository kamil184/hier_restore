//to get 1hz clock from Xhz clock, make a wire OneHertz that pulses after every X pulses of clcok is counted
//its 50mhz so 50000000, that -1 = 49999999
//or just delay #1 sec to flip, but that works in simulation not actual hardware 

/*i decided not to make OneHertz a clock, and keep the clock as the actual hardware clock,
and make OneHertz a pulse - a way to enable the counter every 1 second
and combine it with the button enabler that can pause (disable) the counter */

// Generate a 1 Hz pulse from an input clock by counting clock cycles.
// For a 50 MHz clock, the pulse is asserted once every 50,000,000 cycles
// note: a delay-based approach such as #1 only works in simulation, not in real hardware.

/* Instead of creating a separate 1 Hz clock, this module keeps the original
hardware clock and generates a 1-cycle tick pulse once per second.
That tick is then used as an enable signal for the counter, and can be
combined with another enable signal to pause or resume counting. */

module tick_1hz #(parameter CLK_FREQ = 50000000) (
    input clk,
    input reset,
    output reg tick
);
	//Resize register if CLK_FREQ exceeds range (hardcoded now for 50mhz clock)
    reg [25:0] count;

    initial begin
        tick  = 1'b0;
        count = 26'd0;
    end

    always @(posedge clk) begin
        if (reset) begin
            count <= 26'd0;
            tick  <= 1'b0;
        end
        else if (count == (CLK_FREQ -26'd1)) begin // 1-cycle pulse every 1 second
            count <= 26'd0;
            tick  <= 1'b1;
        end
        else begin
            count <= count + 26'd1;
            tick  <= 1'b0;
        end
    end

endmodule