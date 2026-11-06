// BootWalk.java — Ghidra headless script (Comet Crash reconstruction).
//
// Phase 1 of the boot walk:
//   * ensure a function exists at the ELF entry code address (the OPD
//     at the ELF header entry points to code 0x10230, which Ghidra's
//     initial analysis did not turn into a function),
//   * decompile the call chain from the entry, depth-limited,
//   * list the callers of the two input seam functions (the cellPadInit
//     caller FUN_000dae30 and the cellPadGetData caller FUN_00110738),
// and write it all to the output file given as arg 0.
//
// Usage: -postScript BootWalk.java <out.txt>

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.Set;
import java.util.TreeSet;

public class BootWalk extends GhidraScript {

    private static final long ENTRY_CODE = 0x10230L;
    private static final long PAD_INIT_CALLER = 0xDAE30L;
    private static final long PAD_SAMPLE_CALLER = 0x110738L;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args[0]);
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        // Ensure the entry function exists.
        Address entry = toAddr(ENTRY_CODE);
        Function entryFn = currentProgram.getFunctionManager().getFunctionContaining(entry);
        String entryNote;
        if (entryFn == null) {
            entryFn = createFunction(entry, "_entry");
            entryNote = "created function at entry code 0x10230";
        } else {
            entryNote = "entry code already inside " + entryFn.getName() + " @ "
                    + entryFn.getEntryPoint();
        }
        println("BootWalk: " + entryNote);

        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.setSimplificationStyle("decompile");
        if (!ifc.openProgram(currentProgram)) {
            println("BootWalk: decompiler failed to open");
            return;
        }

        try (BufferedWriter w = new BufferedWriter(new FileWriter(out))) {
            w.write("entry: " + entryNote + "\n\n");

            // Callers of the input seam functions.
            for (long target : new long[]{PAD_INIT_CALLER, PAD_SAMPLE_CALLER}) {
                w.write("callers of FUN_" + Long.toHexString(target) + ":\n");
                ReferenceIterator rit = refMgr.getReferencesTo(toAddr(target));
                Set<String> callers = new TreeSet<>();
                while (rit.hasNext()) {
                    Reference ref = rit.next();
                    Function cf = currentProgram.getFunctionManager()
                            .getFunctionContaining(ref.getFromAddress());
                    if (cf != null) {
                        callers.add(cf.getEntryPoint() + " (" + cf.getBody().getNumAddresses()
                                + " bytes, ref type " + ref.getReferenceType() + ")");
                    }
                }
                for (String c : callers) {
                    w.write("  " + c + "\n");
                }
                w.write("\n");
            }

            // Breadth-first decompile from the entry, depth <= 3.
            Set<Long> seen = new HashSet<>();
            ArrayDeque<long[]> queue = new ArrayDeque<>();
            queue.add(new long[]{entryFn.getEntryPoint().getOffset(), 0});
            seen.add(entryFn.getEntryPoint().getOffset());
            while (!queue.isEmpty()) {
                long[] item = queue.poll();
                long addr = item[0];
                int depth = (int) item[1];
                Function fn = currentProgram.getFunctionManager().getFunctionAt(toAddr(addr));
                if (fn == null) {
                    continue;
                }
                w.write("/* ===== depth " + depth + ": " + fn.getName() + " @ "
                        + fn.getEntryPoint() + " (" + fn.getBody().getNumAddresses()
                        + " bytes) ===== */\n");
                try {
                    DecompileResults res = ifc.decompileFunction(fn, 60, monitor);
                    if (res != null && res.getDecompiledFunction() != null) {
                        w.write(res.getDecompiledFunction().getC());
                    } else {
                        w.write("/* decompile failed */\n");
                    }
                } catch (Exception e) {
                    w.write("/* decompile exception: " + e.getMessage() + " */\n");
                }
                w.write("\n");
                if (depth >= 3) {
                    continue;
                }
                for (Function callee : fn.getCalledFunctions(monitor)) {
                    long ca = callee.getEntryPoint().getOffset();
                    if (seen.add(ca)) {
                        queue.add(new long[]{ca, depth + 1});
                    }
                }
            }
        } finally {
            ifc.dispose();
        }
        println("BootWalk: wrote " + out.getAbsolutePath());
    }
}
