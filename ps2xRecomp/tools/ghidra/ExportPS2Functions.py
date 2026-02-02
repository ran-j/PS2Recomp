# Exports function addresses and names to CSV for PS2Recomp
# @category PS2Recomp

import csv
import os

from ghidra.program.model.symbol import SourceType

def run():
    f = askFile("Choose output CSV file", "Save")
    
    if f is None:
        return

    with open(f.getAbsolutePath(), 'w') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(['Name', 'Start', 'End', 'Size'])
        
        fm = currentProgram.getFunctionManager() 
        functions = fm.getFunctions(True) # True iterates forward wtf kkkkk
        
        count = 0
        for func in functions:
            name = func.getName()
            start = func.getEntryPoint().getOffset()
             
            body = func.getBody()
            max_addr = body.getMaxAddress().getOffset()
             
            size = body.getNumAddresses()
             
            writer.writerow([
                name, 
                "0x{:08X}".format(start), 
                "0x{:08X}".format(max_addr + 1), # End address is exclusive
                size
            ])
            count += 1
            
    print("Exported {} functions to {}".format(count, f.getAbsolutePath()))

if __name__ == "__main__":
    run()
