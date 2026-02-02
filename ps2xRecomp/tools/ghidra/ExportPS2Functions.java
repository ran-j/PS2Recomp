// Exports function addresses and names to CSV for PS2Recomp
// @category PS2Recomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

import java.io.File;
import java.io.PrintWriter;

public class ExportPS2Functions extends GhidraScript {

    @Override
    public void run() throws Exception {
        File file = askFile("Choose output CSV file", "Save");

        if (file == null) {
            return;
        }

        int count = 0;
        try (PrintWriter writer = new PrintWriter(file)) { 
            writer.println("Name,Start,End,Size");

            FunctionManager fm = currentProgram.getFunctionManager(); 
            FunctionIterator it = fm.getFunctions(true);

            while (it.hasNext() && !monitor.isCancelled()) {
                Function func = it.next();
                
                String name = func.getName();
                long start = func.getEntryPoint().getOffset();
                 
                AddressSetView body = func.getBody();
                long maxAddr = body.getMaxAddress().getOffset();
                 
                long size = body.getNumAddresses();
                  
                writer.printf("%s,0x%08X,0x%08X,%d%n", 
                    name, 
                    start, 
                    maxAddr + 1, // End address is exclusive
                    size
                );
                
                count++;
            }
        }

        println(String.format("Exported %d functions to %s", count, file.getAbsolutePath()));
    }
}
