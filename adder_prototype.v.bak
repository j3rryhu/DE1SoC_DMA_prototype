module avalon_adder (
    input  wire        clk,
    input  wire        reset,
    
    // Avalon-MM Slave Interface
    input  wire [1:0]  avs_address,
    input  wire        avs_read,
    input  wire        avs_write,
    input  wire [31:0] avs_writedata,
    output reg  [31:0] avs_readdata
);

    // Internal registers (the "buffer")
    reg [31:0] operand_a;
    reg [31:0] operand_b;
    
    // The actual adder logic
    wire [31:0] sum;
    assign sum = operand_a + operand_b;

    // Synchronous read/write logic
    always @(posedge clk or posedge reset) begin
        if (reset) begin
            operand_a    <= 32'b0;
            operand_b    <= 32'b0;
            avs_readdata <= 32'b0;
        end else begin
            
            // Handle Writes from HPS
            if (avs_write) begin
                case (avs_address)
                    2'b00: operand_a <= avs_writedata;
                    2'b01: operand_b <= avs_writedata;
                endcase
            end
            
            // Handle Reads to HPS
            if (avs_read) begin
                case (avs_address)
                    2'b00: avs_readdata <= operand_a;
                    2'b01: avs_readdata <= operand_b;
                    2'b10: avs_readdata <= sum;
                    default: avs_readdata <= 32'b0;
                endcase
            end
            
        end
    end
endmodule