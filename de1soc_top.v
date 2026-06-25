module de1soc_top(
	// =======================================================
    // FPGA Clock and Reset
    // =======================================================
    input  wire        CLOCK_50,
    input  wire [3:0]  KEY,

    // =======================================================
    // HPS Memory (DDR3)
    // =======================================================
    output wire [14:0] HPS_DDR3_ADDR,
    output wire [2:0]  HPS_DDR3_BA,
    output wire        HPS_DDR3_CAS_N,
    output wire        HPS_DDR3_CKE,
    output wire        HPS_DDR3_CK_N,
    output wire        HPS_DDR3_CK_P,
    output wire        HPS_DDR3_CS_N,
    output wire [3:0]  HPS_DDR3_DM,
    inout  wire [31:0] HPS_DDR3_DQ,
    inout  wire [3:0]  HPS_DDR3_DQS_N,
    inout  wire [3:0]  HPS_DDR3_DQS_P,
    output wire        HPS_DDR3_ODT,
    output wire        HPS_DDR3_RAS_N,
    output wire        HPS_DDR3_RESET_N,
    input  wire        HPS_DDR3_RZQ,
    output wire        HPS_DDR3_WE_N,
	 
	 inout  wire        HPS_GPIO34,
    inout  wire        HPS_GPIO35,
    inout  wire        HPS_GPIO37,
    inout  wire        HPS_GPIO42,
    inout  wire        HPS_GPIO44,
    inout  wire        HPS_GPIO46,

    // =======================================================
    // HPS I/O (Ethernet, SD Card, UART)
    // =======================================================
    output wire        HPS_ENET_GTX_CLK,
    output wire        HPS_ENET_MDC,
    inout  wire        HPS_ENET_MDIO,
    input  wire        HPS_ENET_RX_CLK,
    input  wire [3:0]  HPS_ENET_RX_DATA,
    input  wire        HPS_ENET_RX_DV,
    output wire [3:0]  HPS_ENET_TX_DATA,
    output wire        HPS_ENET_TX_EN,
    output wire        HPS_SD_CLK,
    inout  wire        HPS_SD_CMD,
    inout  wire [3:0]  HPS_SD_DATA,
    input  wire        HPS_UART_RX,
    output wire        HPS_UART_TX,
    inout  wire        HPS_KEY,      // Usually GPIO 34 or 35
    inout  wire        HPS_LED       // Usually GPIO 34 or 35
    // Note: The specific GPIOs (34, 35, 37, 42, 44, 46) map to various 
    // HPS buttons/LEDs/switches on the DE1-SoC depending on your specific board revision.

);
	adder_prototype u0 (
		.clk_clk                         (CLOCK_50),                         //    clk.clk
		.reset_reset_n                   (KEY[0]),                   //  reset.reset_n
		.hps_io_hps_io_emac0_inst_TX_CLK (HPS_ENET_GTX_CLK),
	   .hps_io_hps_io_emac0_inst_TXD0   (HPS_ENET_TX_DATA[0]),
	   .hps_io_hps_io_emac0_inst_TXD1   (HPS_ENET_TX_DATA[1]),
	   .hps_io_hps_io_emac0_inst_TXD2   (HPS_ENET_TX_DATA[2]),
	   .hps_io_hps_io_emac0_inst_TXD3   (HPS_ENET_TX_DATA[3]),
	   .hps_io_hps_io_emac0_inst_RXD0   (HPS_ENET_RX_DATA[0]),
	   .hps_io_hps_io_emac0_inst_RXD1   (HPS_ENET_RX_DATA[1]),
	   .hps_io_hps_io_emac0_inst_RXD2   (HPS_ENET_RX_DATA[2]),
	   .hps_io_hps_io_emac0_inst_RXD3   (HPS_ENET_RX_DATA[3]),
	   .hps_io_hps_io_emac0_inst_MDIO   (HPS_ENET_MDIO),
	   .hps_io_hps_io_emac0_inst_MDC    (HPS_ENET_MDC),
	   .hps_io_hps_io_emac0_inst_RX_CTL (HPS_ENET_RX_DV),
	   .hps_io_hps_io_emac0_inst_TX_CTL (HPS_ENET_TX_EN),
	   .hps_io_hps_io_emac0_inst_RX_CLK (HPS_ENET_RX_CLK),
	   // SD Card
	   .hps_io_hps_io_sdio_inst_CMD     (HPS_SD_CMD),
	   .hps_io_hps_io_sdio_inst_D0      (HPS_SD_DATA[0]), // Note: Usually SD cards use D0-D3. 
																		  // You only exported D0, which means it will run in slower 1-bit mode.
	   .hps_io_hps_io_sdio_inst_CLK     (HPS_SD_CLK),

	   // UART
	   .hps_io_hps_io_uart0_inst_RX     (HPS_UART_RX),
	   .hps_io_hps_io_uart0_inst_TX     (HPS_UART_TX),
		.hps_io_hps_io_gpio_inst_GPIO34  (HPS_GPIO34),  //       .hps_io_gpio_inst_GPIO34
		.hps_io_hps_io_gpio_inst_GPIO35  (HPS_GPIO35),  //       .hps_io_gpio_inst_GPIO35
		.hps_io_hps_io_gpio_inst_GPIO37  (HPS_GPIO37),  //       .hps_io_gpio_inst_GPIO37
		.hps_io_hps_io_gpio_inst_GPIO42  (HPS_GPIO42),  //       .hps_io_gpio_inst_GPIO42
		.hps_io_hps_io_gpio_inst_GPIO44  (HPS_GPIO44),  //       .hps_io_gpio_inst_GPIO44
		.hps_io_hps_io_gpio_inst_GPIO46  (HPS_GPIO46),  //       .hps_io_gpio_inst_GPIO46
		// DDR3 Memory
	   .memory_mem_a                    (HPS_DDR3_ADDR),
	   .memory_mem_ba                   (HPS_DDR3_BA),
	   .memory_mem_ck                   (HPS_DDR3_CK_P),
	   .memory_mem_ck_n                 (HPS_DDR3_CK_N),
	   .memory_mem_cke                  (HPS_DDR3_CKE),
	   .memory_mem_cs_n                 (HPS_DDR3_CS_N),
	   .memory_mem_ras_n                (HPS_DDR3_RAS_N),
	   .memory_mem_cas_n                (HPS_DDR3_CAS_N),
	   .memory_mem_we_n                 (HPS_DDR3_WE_N),
	   .memory_mem_reset_n              (HPS_DDR3_RESET_N),
	   .memory_mem_dq                   (HPS_DDR3_DQ),
	   .memory_mem_dqs                  (HPS_DDR3_DQS_P),
	   .memory_mem_dqs_n                (HPS_DDR3_DQS_N),
	   .memory_mem_odt                  (HPS_DDR3_ODT),
	   .memory_mem_dm                   (HPS_DDR3_DM),
	   .memory_oct_rzqin                (HPS_DDR3_RZQ),
		
		.axi_signals_arcache					(4'b0111),             // axi_signals.arcache
		.axi_signals_arprot              (3'b000),//            .arprot
		.axi_signals_aruser              (5'b00001), //            .aruser
		.axi_signals_awcache             (4'b0111),//            .awcache
		.axi_signals_awprot              (3'b000),//            .awprot
		.axi_signals_awuser              (5'b00001)//            .awuser
	);

endmodule