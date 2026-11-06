// DiscoverPaths.java — Ghidra headless script (Comet Crash reconstruction).
//
// Creates the two functions Ghidra's analysis missed that build level
// file paths (found by disassembly: both load a "%slevel%u.map" /
// "%slevel%d.map" format string through a TOC slot), decompiles them,
// and decompiles the callers of the small path builder — the level
// loader is expected among them.
//
// Usage: -postScript DiscoverPaths.java <out.txt>

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.TreeSet;

public class DiscoverPaths extends GhidraScript {

    private void dump(BufferedWriter w, DecompInterface ifc, Function fn, String tag)
            throws Exception {
        w.write("/* ===== " + tag + ": " + fn.getName() + " @ " + fn.getEntryPoint()
                + " (" + fn.getBody().getNumAddresses() + " bytes) ===== */\n");
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
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long[] entries = {0xE9E7CL, 0x154828L};
        Function[] created = new Function[entries.length];
        for (int i = 0; i < entries.length; i++) {
            Address a = toAddr(entries[i]);
            Function fn = currentProgram.getFunctionManager().getFunctionContaining(a);
            if (fn == null) {
                fn = createFunction(a, null);
                println("DiscoverPaths: created function at " + Long.toHexString(entries[i])
                        + " -> " + (fn != null ? fn.getName() : "FAILED"));
            } else {
                println("DiscoverPaths: " + Long.toHexString(entries[i])
                        + " already inside " + fn.getName());
            }
            created[i] = fn;
        }

        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.setSimplificationStyle("decompile");
        if (!ifc.openProgram(currentProgram)) {
            println("DiscoverPaths: decompiler failed to open");
            return;
        }
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(args[0])))) {
            for (int i = 0; i < created.length; i++) {
                if (created[i] != null) {
                    dump(w, ifc, created[i], "path builder " + i);
                }
            }
            // Callers of the small "%slevel%u.map" builder: the loader.
            if (created[0] != null) {
                TreeSet<Long> callers = new TreeSet<>();
                ReferenceIterator rit = currentProgram.getReferenceManager()
                        .getReferencesTo(created[0].getEntryPoint());
                while (rit.hasNext()) {
                    Reference ref = rit.next();
                    Function cf = currentProgram.getFunctionManager()
                            .getFunctionContaining(ref.getFromAddress());
                    if (cf != null) {
                        callers.add(cf.getEntryPoint().getOffset());
                    }
                }
                w.write("callers of the level%u path builder: " + callers + "\n\n");
                for (long ca : callers) {
                    Function cf = currentProgram.getFunctionManager().getFunctionAt(toAddr(ca));
                    if (cf != null) {
                        dump(w, ifc, cf, "caller");
                    }
                }
            }
        } finally {
            ifc.dispose();
        }
        println("DiscoverPaths: wrote " + args[0]);
    }
}
