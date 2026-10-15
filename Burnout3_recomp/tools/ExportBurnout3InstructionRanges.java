// Export evidence for range repair without changing the Ghidra program.
// @category PS2Recomp
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import java.io.File;
import java.io.PrintWriter;

public class ExportBurnout3InstructionRanges extends GhidraScript {
    @Override
    public void run() throws Exception {
        File destination = askFile("Instruction ranges CSV", "Save");
        long start = -1, end = -1, count = 0;
        try (PrintWriter writer = new PrintWriter(destination, "UTF-8")) {
            writer.println("Start,End");
            InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
            while (instructions.hasNext()) {
                monitor.checkCancelled();
                Instruction instruction = instructions.next();
                MemoryBlock block = currentProgram.getMemory().getBlock(instruction.getAddress());
                if (block == null || !block.isExecute() || !block.isInitialized() ||
                    !instruction.getAddress().getAddressSpace().equals(
                        currentProgram.getAddressFactory().getDefaultAddressSpace())) {
                    continue;
                }
                if (instruction.getLength() != 4) {
                    throw new IllegalStateException("Non-word instruction: " + instruction.getAddress());
                }
                long address = instruction.getAddress().getOffset();
                if (start >= 0 && address != end) {
                    writer.printf("0x%08X,0x%08X%n", start, end);
                    start = -1;
                }
                if (start < 0) start = address;
                end = address + 4;
                count++;
            }
            if (start >= 0) writer.printf("0x%08X,0x%08X%n", start, end);
        }
        println("Exported contiguous ranges covering " + count + " instructions to " + destination);
    }
}
