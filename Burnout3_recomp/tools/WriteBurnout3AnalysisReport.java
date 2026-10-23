// Write a read-only audit of the imported program after Ghidra auto-analysis.
// @category PS2Recomp
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Bookmark;
import ghidra.program.model.mem.MemoryBlock;
import java.io.File;
import java.io.PrintWriter;
import java.util.Iterator;
import java.util.Map;
import java.util.TreeMap;

public class WriteBurnout3AnalysisReport extends GhidraScript {
    @Override
    public void run() throws Exception {
        File output = askFile("Analysis report", "Save");
        try (PrintWriter writer = new PrintWriter(output, "UTF-8")) {
            writer.println("Burnout 3 USA — Ghidra analysis report");
            writer.println("Program: " + currentProgram.getName());
            writer.println("Executable: " + currentProgram.getExecutablePath());
            writer.println("Format: " + currentProgram.getExecutableFormat());
            writer.println("Language: " + currentProgram.getLanguageID());
            writer.println("Compiler: " + currentProgram.getCompilerSpec().getCompilerSpecID());
            writer.println("Image base: " + currentProgram.getImageBase());
            writer.println("Functions: " + currentProgram.getFunctionManager().getFunctionCount());
            writer.println("Instructions: " + currentProgram.getListing().getNumInstructions());
            writer.println("\nMemory blocks (name, start, end, executable):");
            for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
                writer.printf("%s\t%s\t%s\t%s%n", block.getName(), block.getStart(),
                    block.getEnd(), block.isExecute());
            }
            writer.println("\nError and warning bookmarks (address, type, category, comment):");
            Map<String, Integer> counts = new TreeMap<>();
            Iterator<Bookmark> bookmarks = currentProgram.getBookmarkManager().getBookmarksIterator();
            while (bookmarks.hasNext()) {
                Bookmark bookmark = bookmarks.next();
                String type = bookmark.getTypeString();
                counts.merge(type, 1, Integer::sum);
                if ("Error".equals(type) || "Warning".equals(type)) {
                    writer.printf("%s\t%s\t%s\t%s%n", bookmark.getAddress(), type,
                        bookmark.getCategory(), bookmark.getComment().replace('\n', ' '));
                }
            }
            writer.println("\nBookmark counts: " + counts);
        }
        println("Analysis report saved: " + output.getAbsolutePath());
    }
}
