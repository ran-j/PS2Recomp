// Exports PS2Recomp TOML config (+ optional CSV) from Ghidra
// @category PS2Recomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Pattern;

public class ExportPS2Functions extends GhidraScript {

    private static final Set<String> SYSTEM_FUNCTION_NAMES = new HashSet<>(Arrays.asList(
        "entry", "_start", "_init", "_fini",
        "abort", "exit", "_exit",
        "_profiler_start", "_profiler_stop",
        "__main", "__do_global_ctors", "__do_global_dtors",
        "_GLOBAL__sub_I_", "_GLOBAL__sub_D_",
        "__ctor_list", "__dtor_list", "_edata", "_end",
        "etext", "__exidx_start", "__exidx_end",
        "_ftext", "__bss_start", "__bss_start__",
        "__bss_end__", "__end__", "_stack", "_dso_handle"
    ));

    private static final Set<String> DO_NOT_SKIP_OR_STUB = new HashSet<>(Arrays.asList(
        "entry",
        "_start",
        "_init",
        "topThread",
        "cmd_sem_init"
    ));

    private static final Set<String> PS2_API_PREFIXES = new HashSet<>(Arrays.asList(
        "sce", "sif", "pad", "gs", "dma", "iop", "vif", "spu", "mc", "libc"
    ));

    private static final Set<String> KNOWN_STDLIB_NAMES = new HashSet<>(Arrays.asList(
        "printf", "sprintf", "snprintf", "fprintf", "vprintf", "vfprintf", "vsprintf", "vsnprintf",
        "puts", "putchar", "getchar", "gets", "fgets", "fputs", "scanf", "fscanf", "sscanf",
        "sprint", "sbprintf",
        "malloc", "free", "calloc", "realloc", "aligned_alloc", "posix_memalign",
        "memcpy", "memset", "memmove", "memcmp", "memcpy2", "memchr", "bcopy", "bzero",
        "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp", "strlen", "strstr",
        "strchr", "strrchr", "strdup", "strtok", "strtok_r", "strerror",
        "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "rewind", "fflush",
        "fgetc", "feof", "ferror", "clearerr", "fileno", "tmpfile", "remove", "rename",
        "open", "close", "read", "write", "lseek", "stat", "fstat",
        "atoi", "atol", "atoll", "atof", "strtol", "strtoul", "strtoll", "strtoull", "strtod", "strtof",
        "rand", "srand", "random", "srandom", "drand48", "sqrt", "pow", "exp", "log", "log10",
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
        "floor", "ceil", "fabs", "fmod", "frexp", "ldexp", "modf",
        "time", "ctime", "clock", "difftime", "mktime", "localtime", "gmtime", "asctime", "strftime",
        "gettimeofday", "nanosleep", "usleep",
        "atexit", "system", "getpid", "fork", "waitpid",
        "qsort", "bsearch", "abs", "div", "labs", "ldiv", "llabs", "lldiv",
        "isalnum", "isalpha", "isdigit", "islower", "isupper", "isspace", "tolower", "toupper",
        "setjmp", "longjmp", "getenv", "setenv", "unsetenv",
        "perror", "fputc", "getc", "ungetc", "freopen", "setvbuf", "setbuf",
        "strnlen", "strspn", "strcspn", "strcasecmp", "strncasecmp"
    ));

    private static final Pattern C_LIB_PATTERN = Pattern.compile(
        "^_*(mem|str|time|f?printf|f?scanf|malloc|free|calloc|realloc|atoi|itoa|rand|srand|abort|exit|atexit|getenv|system|bsearch|qsort|abs|labs|div|ldiv|mblen|mbtowc|wctomb|mbstowcs|wcstombs).*"
    );

    private static final Pattern KERNEL_RUNTIME_NAME_PATTERN = Pattern.compile(
        "^(?:"
            + "(?:Create|Delete|Start|ExitDelete|Exit|Terminate|Suspend|Resume|Sleep|Wakeup|CancelWakeup|Change|Rotate|Release|Setup|Register|Query|Get|Set|Refer|Poll|Wait|Signal|Enable|Disable|Flush|Reset|Add|Init)"
            + "(?:Thread|Sema|EventFlag|Alarm|Intc|IntcHandler2|Dmac|DmacHandler2|OsdConfigParam|MemorySize|VSyncFlag|Heap|TLS|Status|Cache|Syscall|TLB|TLBEntry|GsCrt)"
            + "|EndOfHeap"
            + "|GsGetIMR|GsPutIMR"
            + "|Deci2Call"
            + "|Sif[A-Za-z0-9_]+"
            + "|i(?:SignalSema|PollSema|ReferSemaStatus|SetEventFlag|ClearEventFlag|PollEventFlag|ReferEventFlagStatus|WakeupThread|CancelWakeupThread|ReleaseWaitThread|SetAlarm|CancelAlarm|FlushCache|sceSifSetDma|sceSifSetDChain)"
        + ")$"
    );

    private static final class FunctionRecord {
        String name;
        long start;
        long endExclusive;
        long size;
    }

    private enum ClassificationKind {
        STUB,
        SKIP,
        NONE
    }

    private static final class ClassificationResult {
        final ClassificationKind kind;
        final String name;

        ClassificationResult(ClassificationKind kind, String name) {
            this.kind = kind;
            this.name = name;
        }
    }

    private static String hex(long value) {
        return String.format("0x%08X", value & 0xFFFFFFFFL);
    }

    private static String tomlString(String value) {
        if (value == null) {
            return "\"\"";
        }
        return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    private static String normalizeOptionalLeadingUnderscore(String value) {
        if (value == null || value.isEmpty()) {
            return "";
        }
        return value.startsWith("_") && value.length() > 1 ? value.substring(1) : value;
    }

    private static boolean hasReliableSymbolName(String name) {
        if (name == null || name.isEmpty()) {
            return false;
        }

        if (name.startsWith("sub_") || name.startsWith("FUN_") || name.startsWith("func_") ||
            name.startsWith("entry_") || name.startsWith("function_") || name.startsWith("LAB_")) {
            return false;
        }

        boolean hasAlpha = false;
        boolean allHexOrPrefix = true;
        for (int i = 0; i < name.length(); ++i) {
            char c = name.charAt(i);
            if (Character.isAlphabetic(c)) {
                hasAlpha = true;
            }
            if (!(Character.digit(c, 16) >= 0 || c == 'x' || c == 'X' || c == '_')) {
                allHexOrPrefix = false;
            }
        }

        if (!hasAlpha) {
            return false;
        }

        if ((name.startsWith("0x") || name.startsWith("0X")) && allHexOrPrefix) {
            return false;
        }

        return true;
    }

    private static boolean hasPs2ApiPrefix(String name) {
        if (name == null || name.isEmpty()) {
            return false;
        }

        String base = normalizeOptionalLeadingUnderscore(name).toLowerCase();
        for (String prefix : PS2_API_PREFIXES) {
            if (base.startsWith(prefix)) {
                return true;
            }
        }
        return false;
    }

    private static boolean isSystemSymbolNameForHeuristics(String name) {
        if (!hasReliableSymbolName(name)) {
            return false;
        }

        return SYSTEM_FUNCTION_NAMES.contains(name) || name.startsWith("__") || name.startsWith(".");
    }

    private static boolean matchesWithOptionalLeadingUnderscoreAlias(String candidate, Set<String> names) {
        if (candidate == null || candidate.isEmpty() || names == null || names.isEmpty()) {
            return false;
        }

        if (names.contains(candidate)) {
            return true;
        }

        String normalized = normalizeOptionalLeadingUnderscore(candidate);
        if (!normalized.equals(candidate) && names.contains(normalized)) {
            return true;
        }

        if (!candidate.startsWith("_") && names.contains("_" + candidate)) {
            return true;
        }

        return false;
    }

    private static boolean isLibraryFunctionName(String name) {
        if (name == null || name.isEmpty() || !hasReliableSymbolName(name)) {
            return false;
        }

        String normalized = normalizeOptionalLeadingUnderscore(name);
        if (KERNEL_RUNTIME_NAME_PATTERN.matcher(normalized).matches()) {
            return true;
        }

        if (matchesWithOptionalLeadingUnderscoreAlias(normalized, KNOWN_STDLIB_NAMES)) {
            return true;
        }

        if (hasPs2ApiPrefix(normalized)) {
            return true;
        }

        return C_LIB_PATTERN.matcher(normalized).matches();
    }

    private static ClassificationResult classifyFunction(Function function) {
        if (function == null) {
            return new ClassificationResult(ClassificationKind.NONE, "");
        }

        String name = function.getName();
        if (name == null || name.isEmpty() || DO_NOT_SKIP_OR_STUB.contains(name)) {
            return new ClassificationResult(ClassificationKind.NONE, name == null ? "" : name);
        }

        if (function.isThunk()) {
            if (isLibraryFunctionName(name)) {
                return new ClassificationResult(ClassificationKind.STUB, name);
            }

            Function target = function.getThunkedFunction(true);
            if (target != null) {
                String targetName = target.getName();
                if (isLibraryFunctionName(targetName)) {
                    return new ClassificationResult(ClassificationKind.STUB, targetName);
                }
            }

            if (isSystemSymbolNameForHeuristics(name)) {
                return new ClassificationResult(ClassificationKind.SKIP, name);
            }

            return new ClassificationResult(ClassificationKind.NONE, name);
        }

        if (isLibraryFunctionName(name)) {
            return new ClassificationResult(ClassificationKind.STUB, name);
        }

        if (isSystemSymbolNameForHeuristics(name)) {
            return new ClassificationResult(ClassificationKind.SKIP, name);
        }

        return new ClassificationResult(ClassificationKind.NONE, name);
    }

    private static String makeSelector(String name, long start, boolean includeAddress) {
        if (includeAddress) {
            return name + "@" + hex(start);
        }
        return name;
    }

    private static List<String> collectFunctionSelectors(
        Set<String> names,
        List<FunctionRecord> records,
        boolean includeAddress
    ) {
        List<FunctionRecord> ordered = new ArrayList<>(records);
        ordered.sort(Comparator.comparingLong(r -> r.start));

        List<String> selectors = new ArrayList<>();
        Set<String> seenSelectors = new LinkedHashSet<>();
        Set<String> coveredNames = new HashSet<>();

        for (FunctionRecord record : ordered) {
            if (record.name == null || !names.contains(record.name)) {
                continue;
            }

            coveredNames.add(record.name);
            String selector = makeSelector(record.name, record.start, includeAddress);
            if (seenSelectors.add(selector)) {
                selectors.add(selector);
            }
        }

        if (includeAddress) {
            List<String> unresolved = new ArrayList<>();
            for (String name : names) {
                if (!coveredNames.contains(name)) {
                    unresolved.add(name);
                }
            }
            Collections.sort(unresolved);
            for (String name : unresolved) {
                System.out.println("Warning: unresolved selector name without address, omitting from TOML: " + name);
            }
        } else {
            Collections.sort(selectors);
        }

        return selectors;
    }

    @Override
    public void run() throws Exception {
        File tomlFile = askFile("Choose output TOML config file", "Save");
        if (tomlFile == null) {
            return;
        }

        boolean exportCsv = askYesNo("Export CSV", "Also export compatibility CSV function map?");
        File csvFile = null;
        if (exportCsv) {
            csvFile = askFile("Choose output CSV file", "Save");
            if (csvFile == null) {
                exportCsv = false;
            }
        }

        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionIterator it = fm.getFunctions(true);

        List<FunctionRecord> functionRecords = new ArrayList<>();
        Set<String> stubNames = new LinkedHashSet<>();
        Set<String> skipNames = new LinkedHashSet<>();
        int uncategorizedCount = 0;

        while (it.hasNext() && !monitor.isCancelled()) {
            Function func = it.next();

            AddressSetView body = func.getBody();
            if (body == null || body.getNumAddresses() == 0) {
                continue;
            }

            FunctionRecord record = new FunctionRecord();
            record.name = func.getName();
            record.start = func.getEntryPoint().getOffset();
            record.endExclusive = body.getMaxAddress().getOffset() + 1L;
            record.size = body.getNumAddresses();
            functionRecords.add(record);

            ClassificationResult classification = classifyFunction(func);
            if (classification.kind == ClassificationKind.STUB) {
                stubNames.add(classification.name);
            } else if (classification.kind == ClassificationKind.SKIP) {
                skipNames.add(classification.name);
            } else {
                uncategorizedCount++;
            }
        }

        List<String> stubSelectors = collectFunctionSelectors(stubNames, functionRecords, true);
        List<String> skipSelectors = collectFunctionSelectors(skipNames, functionRecords, true);

        if (exportCsv && csvFile != null) {
            try (PrintWriter writer = new PrintWriter(csvFile)) {
                writer.println("Name,Start,End,Size");
                functionRecords.sort(Comparator.comparingLong(r -> r.start));
                for (FunctionRecord record : functionRecords) {
                    writer.printf("%s,0x%08X,0x%08X,%d%n",
                        record.name,
                        record.start,
                        record.endExclusive,
                        record.size
                    );
                }
            }
        }

        String programPath = currentProgram.getExecutablePath();
        if (programPath == null) {
            programPath = "";
        }

        File outputDir = tomlFile.getParentFile() == null ? new File("output") : new File(tomlFile.getParentFile(), "output");
        String ghidraCsvPath = (exportCsv && csvFile != null) ? csvFile.getAbsolutePath() : "";

        try (PrintWriter writer = new PrintWriter(tomlFile)) {
            writer.println("# Auto-generated by ExportPS2Functions.java");
            writer.println("#");
            writer.println("# Classification policy (aligned with analyzer intent):");
            writer.println("# - library/runtime names -> [general].stubs");
            writer.println("# - system names -> [general].skip");
            writer.println("# - others are left for recompilation");
            writer.println();

            writer.println("[general]");
            writer.println("input = " + tomlString(programPath));
            writer.println("output = " + tomlString(outputDir.getAbsolutePath()));
            writer.println("ghidra_output = " + tomlString(ghidraCsvPath));
            writer.println("single_file_output = false");
            writer.println("patch_syscalls = false");
            writer.println("patch_cop0 = true");
            writer.println("patch_cache = true");
            writer.println("stubs = [");
            for (String selector : stubSelectors) {
                writer.println("  " + tomlString(selector) + ",");
            }
            writer.println("]");
            writer.println("skip = [");
            for (String selector : skipSelectors) {
                writer.println("  " + tomlString(selector) + ",");
            }
            writer.println("]");
            writer.println();

            writer.println("[ghidra_export]");
            writer.println("function_count = " + functionRecords.size());
            writer.println("stub_count = " + stubSelectors.size());
            writer.println("skip_count = " + skipSelectors.size());
            writer.println("uncategorized_count = " + uncategorizedCount);
            writer.println("runtime_call_name_count = 0");
            writer.println("runtime_call_source = \"regex_only\"");
        }

        if (exportCsv && csvFile != null) {
            println(String.format("Exported %d functions to %s", functionRecords.size(), csvFile.getAbsolutePath()));
        }

        println("Using regex-only runtime/library classification (no ps2_call_list.h).");
        println(String.format("Exported TOML config to %s", tomlFile.getAbsolutePath()));
    }
}
