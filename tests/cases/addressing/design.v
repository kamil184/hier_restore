module addr_slave_Bbehv
  (input  clk_50mhz,
   input  async_reset,
   input  [2:0] adr,
   input  we,
   input  d,
   output [7:0] leds);
  wire [2:0] addr_b1;
  wire [2:0] addr_b2;
  wire d_b1;
  wire d_b2;
  wire we_b1;
  wire we_b2;
  wire [7:0] led_register;
  wire n127;
  wire n129;
  wire n131;
  wire n133;
  wire n135;
  wire n137;
  wire n139;
  wire n141;
  wire [7:0] n142;
  wire n143;
  reg n144;
  wire n145;
  reg n146;
  wire n147;
  reg n148;
  wire n149;
  reg n150;
  wire n151;
  reg n152;
  wire n153;
  reg n154;
  wire n155;
  reg n156;
  wire n157;
  reg n158;
  wire [7:0] n159;
  reg [2:0] n165;
  reg [2:0] n166;
  reg n167;
  reg n168;
  reg n169;
  reg n170;
  wire [7:0] n171;
  reg [7:0] n172;
  assign leds = led_register; //(module output)
  /*# rtl/addr_slave.vhd:15:10 */
  assign addr_b1 = n165; // (signal)
  /*# rtl/addr_slave.vhd:15:19 */
  assign addr_b2 = n166; // (signal)
  /*# rtl/addr_slave.vhd:16:10 */
  assign d_b1 = n167; // (signal)
  /*# rtl/addr_slave.vhd:16:16 */
  assign d_b2 = n168; // (signal)
  /*# rtl/addr_slave.vhd:16:22 */
  assign we_b1 = n169; // (signal)
  /*# rtl/addr_slave.vhd:16:29 */
  assign we_b2 = n170; // (signal)
  /*# rtl/addr_slave.vhd:18:10 */
  assign led_register = n172; // (signal)
  /*# rtl/addr_slave.vhd:45:11 */
  assign n127 = addr_b2 == 3'b000;
  /*# rtl/addr_slave.vhd:46:11 */
  assign n129 = addr_b2 == 3'b001;
  /*# rtl/addr_slave.vhd:47:11 */
  assign n131 = addr_b2 == 3'b010;
  /*# rtl/addr_slave.vhd:48:11 */
  assign n133 = addr_b2 == 3'b011;
  /*# rtl/addr_slave.vhd:49:11 */
  assign n135 = addr_b2 == 3'b100;
  /*# rtl/addr_slave.vhd:50:11 */
  assign n137 = addr_b2 == 3'b101;
  /*# rtl/addr_slave.vhd:51:11 */
  assign n139 = addr_b2 == 3'b110;
  /*# rtl/addr_slave.vhd:52:11 */
  assign n141 = addr_b2 == 3'b111;
  /*# rtl/addr_slave.vhd:44:9 */
  assign n142 = {n141, n139, n137, n135, n133, n131, n129, n127};
  /*# rtl/addr_slave.vhd:18:10 */
  assign n143 = led_register[0]; // extract
  /*# rtl/addr_slave.vhd:44:9 */
  always @*
    case (n142)
      8'b10000000: n144 = n143;
      8'b01000000: n144 = n143;
      8'b00100000: n144 = n143;
      8'b00010000: n144 = n143;
      8'b00001000: n144 = n143;
      8'b00000100: n144 = n143;
      8'b00000010: n144 = n143;
      8'b00000001: n144 = d_b2;
      default: n144 = n143;
    endcase
  /*# rtl/addr_slave.vhd:18:10 */
  assign n145 = led_register[1]; // extract
  /*# rtl/addr_slave.vhd:44:9 */
  always @*
    case (n142)
      8'b10000000: n146 = n145;
      8'b01000000: n146 = n145;
      8'b00100000: n146 = n145;
      8'b00010000: n146 = n145;
      8'b00001000: n146 = n145;
      8'b00000100: n146 = n145;
      8'b00000010: n146 = d_b2;
      8'b00000001: n146 = n145;
      default: n146 = n145;
    endcase
  /*# rtl/addr_slave.vhd:18:10 */
  assign n147 = led_register[2]; // extract
  /*# rtl/addr_slave.vhd:44:9 */
  always @*
    case (n142)
      8'b10000000: n148 = n147;
      8'b01000000: n148 = n147;
      8'b00100000: n148 = n147;
      8'b00010000: n148 = n147;
      8'b00001000: n148 = n147;
      8'b00000100: n148 = d_b2;
      8'b00000010: n148 = n147;
      8'b00000001: n148 = n147;
      default: n148 = n147;
    endcase
  /*# rtl/addr_slave.vhd:18:10 */
  assign n149 = led_register[3]; // extract
  /*# rtl/addr_slave.vhd:44:9 */
  always @*
    case (n142)
      8'b10000000: n150 = n149;
      8'b01000000: n150 = n149;
      8'b00100000: n150 = n149;
      8'b00010000: n150 = n149;
      8'b00001000: n150 = d_b2;
      8'b00000100: n150 = n149;
      8'b00000010: n150 = n149;
      8'b00000001: n150 = n149;
      default: n150 = n149;
    endcase
  /*# rtl/addr_slave.vhd:18:10 */
  assign n151 = led_register[4]; // extract
  /*# rtl/addr_slave.vhd:44:9 */
  always @*
    case (n142)
      8'b10000000: n152 = n151;
      8'b01000000: n152 = n151;
      8'b00100000: n152 = n151;
      8'b00010000: n152 = d_b2;
      8'b00001000: n152 = n151;
      8'b00000100: n152 = n151;
      8'b00000010: n152 = n151;
      8'b00000001: n152 = n151;
      default: n152 = n151;
    endcase
  /*# rtl/addr_slave.vhd:18:10 */
  assign n153 = led_register[5]; // extract
  /*# rtl/addr_slave.vhd:44:9 */
  always @*
    case (n142)
      8'b10000000: n154 = n153;
      8'b01000000: n154 = n153;
      8'b00100000: n154 = d_b2;
      8'b00010000: n154 = n153;
      8'b00001000: n154 = n153;
      8'b00000100: n154 = n153;
      8'b00000010: n154 = n153;
      8'b00000001: n154 = n153;
      default: n154 = n153;
    endcase
  /*# rtl/addr_slave.vhd:18:10 */
  assign n155 = led_register[6]; // extract
  /*# rtl/addr_slave.vhd:44:9 */
  always @*
    case (n142)
      8'b10000000: n156 = n155;
      8'b01000000: n156 = d_b2;
      8'b00100000: n156 = n155;
      8'b00010000: n156 = n155;
      8'b00001000: n156 = n155;
      8'b00000100: n156 = n155;
      8'b00000010: n156 = n155;
      8'b00000001: n156 = n155;
      default: n156 = n155;
    endcase
  /*# rtl/addr_slave.vhd:18:10 */
  assign n157 = led_register[7]; // extract
  /*# rtl/addr_slave.vhd:44:9 */
  always @*
    case (n142)
      8'b10000000: n158 = d_b2;
      8'b01000000: n158 = n157;
      8'b00100000: n158 = n157;
      8'b00010000: n158 = n157;
      8'b00001000: n158 = n157;
      8'b00000100: n158 = n157;
      8'b00000010: n158 = n157;
      8'b00000001: n158 = n157;
      default: n158 = n157;
    endcase
  /*# rtl/addr_slave.vhd:43:7 */
  assign n159 = {n158, n156, n154, n152, n150, n148, n146, n144};
  /*# rtl/addr_slave.vhd:26:11 */
  always @(posedge clk_50mhz)
    n165 <= adr;
  /*# rtl/addr_slave.vhd:26:11 */
  always @(posedge clk_50mhz)
    n166 <= addr_b1;
  /*# rtl/addr_slave.vhd:26:11 */
  always @(posedge clk_50mhz)
    n167 <= d;
  /*# rtl/addr_slave.vhd:26:11 */
  always @(posedge clk_50mhz)
    n168 <= d_b1;
  /*# rtl/addr_slave.vhd:26:11 */
  always @(posedge clk_50mhz)
    n169 <= we;
  /*# rtl/addr_slave.vhd:26:11 */
  always @(posedge clk_50mhz)
    n170 <= we_b1;
  /*# rtl/addr_slave.vhd:42:5 */
  assign n171 = we_b2 ? n159 : led_register;
  /*# rtl/addr_slave.vhd:42:5 */
  always @(posedge clk_50mhz or posedge async_reset)
    if (async_reset)
      n172 <= 8'b00000000;
    else
      n172 <= n171;
endmodule

module addr_master_Bbehv_3000000
  (input  clk_12mhz,
   input  async_reset,
   output [2:0] adr,
   output we,
   output d);
  wire [21:0] eventcnt;
  wire event;
  wire addr_inc;
  wire [1:0] state;
  wire [31:0] n29;
  wire n31;
  wire [31:0] n32;
  wire [31:0] n34;
  wire [21:0] n35;
  wire [21:0] n37;
  wire n40;
  reg [2:0] n48_addr_i;
  wire n53;
  wire [2:0] n55;
  wire [2:0] n57;
  wire n68;
  wire [1:0] n71;
  wire n73;
  wire n75;
  wire [2:0] n76;
  reg [1:0] n80;
  wire n87;
  wire n88;
  wire n92;
  wire n93;
  wire n96;
  wire n97;
  wire n101;
  wire n102;
  wire n105;
  wire n106;
  reg [21:0] n108;
  reg n109;
  reg [1:0] n110;
  wire [2:0] n111;
  reg [2:0] n112;
  assign adr = n112; //(module output)
  assign we = n93; //(module output)
  assign d = n102; //(module output)
  /*# rtl/addr_master.vhd:17:10 */
  assign eventcnt = n108; // (signal)
  /*# rtl/addr_master.vhd:18:10 */
  assign event = n109; // (signal)
  /*# rtl/addr_master.vhd:19:10 */
  assign addr_inc = n88; // (signal)
  /*# rtl/addr_master.vhd:24:10 */
  assign state = n110; // (signal)
  /*# rtl/addr_master.vhd:38:19 */
  assign n29 = {10'b0, eventcnt};  // uext
  /*# rtl/addr_master.vhd:38:19 */
  assign n31 = $signed(n29) < $signed(32'b00000000001011011100011011000000);
  /*# rtl/addr_master.vhd:39:30 */
  assign n32 = {10'b0, eventcnt};  // uext
  /*# rtl/addr_master.vhd:39:30 */
  assign n34 = n32 + 32'b00000000000000000000000000000001;
  /*# rtl/addr_master.vhd:39:21 */
  assign n35 = n34[21:0];  // trunc
  /*# rtl/addr_master.vhd:38:7 */
  assign n37 = n31 ? n35 : 22'b0000000000000000000000;
  /*# rtl/addr_master.vhd:38:7 */
  assign n40 = n31 ? 1'b0 : 1'b1;
  /*# rtl/addr_master.vhd:51:14 */
  always @*
    n48_addr_i = n112; // (isignal)
  initial
    n48_addr_i = 3'b000;
  /*# rtl/addr_master.vhd:57:19 */
  assign n53 = n48_addr_i == 3'b111;
  /*# rtl/addr_master.vhd:60:28 */
  assign n55 = n48_addr_i + 3'b001;
  /*# rtl/addr_master.vhd:57:9 */
  assign n57 = n53 ? 3'b000 : n55;
  /*# rtl/addr_master.vhd:74:9 */
  assign n68 = state == 2'b00;
  /*# rtl/addr_master.vhd:78:11 */
  assign n71 = event ? 2'b10 : 2'b01;
  /*# rtl/addr_master.vhd:77:9 */
  assign n73 = state == 2'b01;
  /*# rtl/addr_master.vhd:84:9 */
  assign n75 = state == 2'b10;
  /*# rtl/addr_master.vhd:73:7 */
  assign n76 = {n75, n73, n68};
  /*# rtl/addr_master.vhd:73:7 */
  always @*
    case (n76)
      3'b100: n80 = 2'b00;
      3'b010: n80 = n71;
      3'b001: n80 = 2'b01;
      default: n80 = 2'b00;
    endcase
  /*# rtl/addr_master.vhd:93:30 */
  assign n87 = state == 2'b10;
  /*# rtl/addr_master.vhd:93:19 */
  assign n88 = n87 ? 1'b1 : 1'b0;
  /*# rtl/addr_master.vhd:94:30 */
  assign n92 = state == 2'b00;
  /*# rtl/addr_master.vhd:94:19 */
  assign n93 = n92 ? 1'b1 : n97;
  /*# rtl/addr_master.vhd:95:24 */
  assign n96 = state == 2'b10;
  /*# rtl/addr_master.vhd:94:44 */
  assign n97 = n96 ? 1'b1 : 1'b0;
  /*# rtl/addr_master.vhd:98:23 */
  assign n101 = state == 2'b00;
  /*# rtl/addr_master.vhd:98:12 */
  assign n102 = n101 ? 1'b1 : n106;
  /*# rtl/addr_master.vhd:99:23 */
  assign n105 = state == 2'b10;
  /*# rtl/addr_master.vhd:98:37 */
  assign n106 = n105 ? 1'b0 : 1'b0;
  /*# rtl/addr_master.vhd:37:5 */
  always @(posedge clk_12mhz or posedge async_reset)
    if (async_reset)
      n108 <= 22'b0000000000000000000000;
    else
      n108 <= n37;
  /*# rtl/addr_master.vhd:37:5 */
  always @(posedge clk_12mhz or posedge async_reset)
    if (async_reset)
      n109 <= 1'b0;
    else
      n109 <= n40;
  /*# rtl/addr_master.vhd:72:5 */
  always @(posedge clk_12mhz or posedge async_reset)
    if (async_reset)
      n110 <= 2'b00;
    else
      n110 <= n80;
  /*# rtl/addr_master.vhd:55:5 */
  assign n111 = addr_inc ? n57 : n48_addr_i;
  /*# rtl/addr_master.vhd:55:5 */
  always @(posedge clk_12mhz or posedge async_reset)
    if (async_reset)
      n112 <= 3'b000;
    else
      n112 <= n111;
endmodule

module addressing
  (input  clk_12MHz,
   input  clk_50MHz,
   output [7:0] leds);
  wire async_reset;
  wire [2:0] adr_s;
  wire we_s;
  wire d_s;
  wire [7:0] \addr_slave_1.leds ;
  reg [7:0] n5_cnt;
  wire n10;
  wire [7:0] n12;
  wire n15;
  reg n20;
  wire [7:0] n21;
  reg [7:0] n22;
  assign leds = \addr_slave_1.leds ; //(module output)
  /*# rtl/addressing.vhd:20:10 */
  assign async_reset = n20; // (signal)
  /*# rtl/addressing.vhd:27:3 */
  addr_master_Bbehv_3000000 addr_master_1 (
    .clk_12mhz(clk_12MHz),
    .async_reset(async_reset),
    .adr(adr_s),
    .we(we_s),
    .d(d_s));
  /*# rtl/addressing.vhd:38:3 */
  addr_slave_Bbehv addr_slave_1 (
    .clk_50mhz(clk_50MHz),
    .async_reset(async_reset),
    .adr(adr_s),
    .we(we_s),
    .d(d_s),
    .leds(\addr_slave_1.leds ));
  /*# rtl/addressing.vhd:50:14 */
  always @*
    n5_cnt = n22; // (isignal)
  initial
    n5_cnt = 8'b00000000;
  /*# rtl/addressing.vhd:53:14 */
  assign n10 = $unsigned(n5_cnt) < $unsigned(8'b00000101);
  /*# rtl/addressing.vhd:54:28 */
  assign n12 = n5_cnt + 8'b00000001;
  /*# rtl/addressing.vhd:53:7 */
  assign n15 = n10 ? 1'b1 : 1'b0;
  /*# rtl/addressing.vhd:52:5 */
  always @(posedge clk_12MHz)
    n20 <= n15;
  /*# rtl/addressing.vhd:52:5 */
  assign n21 = n10 ? n12 : n5_cnt;
  /*# rtl/addressing.vhd:52:5 */
  always @(posedge clk_12MHz)
    n22 <= n21;
  initial
    n22 = 8'b00000000;
endmodule

