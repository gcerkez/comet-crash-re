// ExportReconstruction.java
// Ghidra headless post-script: exports a reconstruction corpus for the
// decrypted Comet Crash (PS3, NPUB30061) EBOOT.ELF:
//   <outDir>/functions.tsv  - every function: address, name, size, signature
//   <outDir>/strings.tsv    - every defined string with its address
//   <outDir>/decompiled.c   - decompiler C output for every function
//   <outDir>/summary.txt    - counts and program metadata
// Usage (headless): -postScript ExportReconstruction.java <outDir>

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.DecompiledFunction;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.util.DefinedDataIterator;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.File;
import java.io.FileWriter;

public class ExportCorpus2 extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("ExportCorpus2: missing <outDir> argument");
            return;
        }
        File outDir = new File(args[0]);
        outDir.mkdirs();

        // ---------- functions.tsv ----------
        FunctionManager fm = currentProgram.getFunctionManager();
        int funcCount = 0;
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(outDir, "functions.tsv")))) {
            w.write("address\tname\tsize_bytes\tcalling_convention\tis_thunk\tis_external\tsignature\n");
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                funcCount++;
                String sig;
                try {
                    sig = f.getSignature().toString().replace('\t', ' ');
                } catch (Exception e) {
                    sig = "";
                }
                w.write(String.format("%s\t%s\t%d\t%s\t%b\t%b\t%s\n",
                        f.getEntryPoint().toString(),
                        f.getName(),
                        f.getBody().getNumAddresses(),
                        String.valueOf(f.getCallingConventionName()),
                        f.isThunk(),
                        f.isExternal(),
                        sig));
            }
        }
        println("ExportCorpus2: " + funcCount + " functions listed");

        // ---------- strings.tsv ----------
        int stringCount = 0;
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(outDir, "strings.tsv")))) {
            w.write("address\tlength\tvalue\n");
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                Object v = d.getValue();
                if (!(v instanceof String)) {
                    continue;
                }
                stringCount++;
                w.write(String.format("%s\t%d\t%s\n",
                        d.getAddress().toString(),
                        d.getLength(),
                        v.toString().replace('\t', ' ').replace('\n', ' ').replace('\r', ' ')));
            }
        }
        println("ExportCorpus2: " + stringCount + " strings listed");

        // ---------- decompiled.c ----------
        int okCount = 0;
        int failCount = 0;
        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.toggleSyntaxTree(true);
        ifc.setSimplificationStyle("decompile");
        if (!ifc.openProgram(currentProgram)) {
            println("ExportCorpus2: decompiler failed to open program: " + ifc.getLastMessage());
            return;
        }
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(outDir, "decompiled.c")))) {
            w.write("/* Decompiled with Ghidra " + getGhidraVersion() + " from Comet Crash EBOOT.ELF (PS3 NPUB30061, PowerPC64 BE).\n");
            w.write("   Function names are Ghidra-assigned (FUN_<address>) unless the binary carried a symbol.\n");
            w.write("   Treat as reconstructed logic, not original source. */\n\n");
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                if (monitor.isCancelled()) {
                    break;
                }
                Function f = it.next();
                if (f.isExternal()) {
                    continue;
                }
                w.write("\n/* ============================================================\n");
                w.write("   " + f.getName() + " @ " + f.getEntryPoint().toString()
                        + "  (" + f.getBody().getNumAddresses() + " bytes)\n");
                w.write("   ============================================================ */\n");
                try {
                    DecompileResults res = ifc.decompileFunction(f, 60, monitor);
                    DecompiledFunction df = (res != null) ? res.getDecompiledFunction() : null;
                    if (df != null) {
                        w.write(df.getC());
                        okCount++;
                    } else {
                        w.write("/* decompile failed: " + (res != null ? res.getErrorMessage() : "no result") + " */\n");
                        failCount++;
                    }
                } catch (Exception e) {
                    w.write("/* decompile exception: " + e.getMessage() + " */\n");
                    failCount++;
                }
            }
        } finally {
            ifc.dispose();
        }
        println("ExportCorpus2: decompiled ok=" + okCount + " failed=" + failCount);


        // NOTE: no function_strings.tsv here on purpose. Ghidra's
        // database has no references for this binary's TOC-based string
        // traffic; that map is produced by scripts/toc_string_xrefs.py
        // (Capstone) instead. (A Ghidra-side export once overwrote the
        // good file with a header-only one — see RECONSTRUCTION-003.)

        if (args.length >= 2) {
            ReferenceManager refMgr = currentProgram.getReferenceManager();
            try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(outDir, "import_callers.tsv")));
                 BufferedReader r = new BufferedReader(new FileReader(new File(args[1])))) {
                w.write("library\tname\tfnid\tslot\tcaller_function\n");
                String line;
                boolean header = true;
                while ((line = r.readLine()) != null) {
                    if (header) { header = false; continue; }
                    String[] parts = line.split("\t", -1);
                    if (parts.length < 5) continue;
                    Address slot = toAddr(Long.parseLong(parts[2], 16));
                    String label = parts[4].isEmpty() ? ("fnid_" + parts[3]) : parts[4];
                    // Level 1: references to the slot live in thunk functions.
                    java.util.TreeSet<String> thunks = new java.util.TreeSet<>();
                    ReferenceIterator rit = refMgr.getReferencesTo(slot);
                    while (rit.hasNext()) {
                        Reference ref = rit.next();
                        Function tf = fm.getFunctionContaining(ref.getFromAddress());
                        if (tf != null) {
                            thunks.add(tf.getEntryPoint().toString());
                        }
                    }
                    // Level 2: who calls the thunks is the real caller set.
                    java.util.TreeSet<String> callers = new java.util.TreeSet<>();
                    for (String t : thunks) {
                        ReferenceIterator crit = refMgr.getReferencesTo(toAddr(Long.parseLong(t, 16)));
                        while (crit.hasNext()) {
                            Reference ref = crit.next();
                            Function cf = fm.getFunctionContaining(ref.getFromAddress());
                            if (cf != null && !cf.getEntryPoint().toString().equals(t)) {
                                callers.add(cf.getEntryPoint().toString());
                            }
                        }
                    }
                    if (callers.isEmpty()) {
                        callers = thunks;
                    }
                    for (String c : callers) {
                        w.write(parts[0] + "\t" + label + "\t" + parts[3] + "\t" + parts[2] + "\t" + c + "\n");
                    }
                }
            }
            println("ExportCorpus2: import_callers.tsv written");
        }

        // ---------- summary.txt ----------
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(outDir, "summary.txt")))) {
            w.write("program: " + currentProgram.getName() + "\n");
            w.write("language: " + currentProgram.getLanguageID().toString() + "\n");
            w.write("compiler_spec: " + currentProgram.getCompilerSpec().getCompilerSpecID().toString() + "\n");
            w.write("image_base: " + currentProgram.getImageBase().toString() + "\n");
            w.write("functions: " + funcCount + "\n");
            w.write("strings: " + stringCount + "\n");
            w.write("decompiled_ok: " + okCount + "\n");
            w.write("decompiled_failed: " + failCount + "\n");
            w.write("memory_blocks:\n");
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                w.write(String.format("  %s %s-%s size=%d r=%b w=%b x=%b initialized=%b\n",
                        b.getName(), b.getStart().toString(), b.getEnd().toString(), b.getSize(),
                        b.isRead(), b.isWrite(), b.isExecute(), b.isInitialized()));
            }
            w.write("entry_points:\n");
            currentProgram.getSymbolTable().getExternalEntryPointIterator().forEachRemaining(a ->
                    {
                        try {
                            w.write("  " + a.toString() + "\n");
                        } catch (Exception e) {
                            // ignore write races in lambda; summary is best-effort here
                        }
                    });
        }
        println("ExportCorpus2: done");
    }
}
