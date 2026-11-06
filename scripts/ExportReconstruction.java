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
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.util.DefinedDataIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;

public class ExportReconstruction extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("ExportReconstruction: missing <outDir> argument");
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
        println("ExportReconstruction: " + funcCount + " functions listed");

        // ---------- strings.tsv ----------
        int stringCount = 0;
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(outDir, "strings.tsv")))) {
            w.write("address\tlength\tvalue\n");
            for (Data d : DefinedDataIterator.definedStrings(currentProgram)) {
                Object v = d.getValue();
                if (v == null) {
                    continue;
                }
                stringCount++;
                w.write(String.format("%s\t%d\t%s\n",
                        d.getAddress().toString(),
                        d.getLength(),
                        v.toString().replace('\t', ' ').replace('\n', ' ').replace('\r', ' ')));
            }
        }
        println("ExportReconstruction: " + stringCount + " strings listed");

        // ---------- decompiled.c ----------
        int okCount = 0;
        int failCount = 0;
        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.toggleSyntaxTree(true);
        ifc.setSimplificationStyle("decompile");
        if (!ifc.openProgram(currentProgram)) {
            println("ExportReconstruction: decompiler failed to open program: " + ifc.getLastMessage());
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
        println("ExportReconstruction: decompiled ok=" + okCount + " failed=" + failCount);

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
        println("ExportReconstruction: done");
    }
}
