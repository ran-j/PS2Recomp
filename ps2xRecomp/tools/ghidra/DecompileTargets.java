// Decompiles a fixed list of functions (by address) to C-like pseudocode
// and dumps them to a text file, plus lists all writes to a specific
// watch address found anywhere in the program (via decompiler high-level
// PcodeOp scanning is too complex here; instead we just decompile targets
// and let a human/Claude read the STORE expressions).
// @category PS2Recomp

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.File;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.List;

public class DecompileTargets extends GhidraScript {

    @Override
    public void run() throws Exception {
        List<String> targets = Arrays.asList(
            "001d0660", // thread 2 entry point
            "001d2da8", // sub_001D2DA8 (poll caller)
            "001d2b30", // sub_001D2B30
            "001d29f8", // sub_001D29F8
            "001cfd20", // sub_001CFD20
            "001d2bb0", // FUN_001d2bb0
            "001d2bf8", // inside gap_001d2bf8 range
            "001d2648", // sub_001D2648 (right after our merged gap)
            "001d2668", // inside gap_001d2668 range
            "001d2930"  // called 4x before the kick
        );

        File outFile = askFile("Choose output text file", "Save");
        if (outFile == null) {
            return;
        }

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        DecompileOptions opts = new DecompileOptions();
        decomp.setOptions(opts);

        try (PrintWriter writer = new PrintWriter(outFile)) {
            for (String addrStr : targets) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrStr);
                Function func = currentProgram.getFunctionManager().getFunctionContaining(addr);

                writer.println("========================================");
                writer.println("Target address: 0x" + addrStr);

                if (func == null) {
                    writer.println("No Ghidra function contains this address (it's in an unmapped/gap region).");
                    writer.println("Attempting raw disassembly listing instead:");
                    Address cur = addr;
                    for (int i = 0; i < 60 && cur != null; i++) {
                        ghidra.program.model.listing.Instruction instr = currentProgram.getListing().getInstructionAt(cur);
                        if (instr == null) {
                            break;
                        }
                        writer.println(String.format("  0x%08x: %s", cur.getOffset(), instr.toString()));
                        cur = instr.getFallThrough();
                        if (instr.getFlowType().isTerminal() && !instr.getFlowType().isFallthrough()) {
                            break;
                        }
                    }
                    writer.println();
                    continue;
                }

                writer.println("Function: " + func.getName() + "  Entry: " + func.getEntryPoint());
                DecompileResults res = decomp.decompileFunction(func, 60, new ConsoleTaskMonitor());
                if (res != null && res.decompileCompleted()) {
                    writer.println(res.getDecompiledFunction().getC());
                } else {
                    writer.println("Decompilation failed: " + (res != null ? res.getErrorMessage() : "null result"));
                }
                writer.println();
            }
        }

        println("Wrote decompiled targets to " + outFile.getAbsolutePath());
    }
}
