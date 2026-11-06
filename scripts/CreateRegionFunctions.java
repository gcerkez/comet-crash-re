// CreateRegionFunctions.java — Ghidra headless script (Comet Crash).
//
// Creates functions at the candidate entry points listed in arg 0
// (one hex address per line — produced by a Capstone scan for
// prologues and post-return addresses in a region Ghidra's analysis
// never discovered), disassembling first so bodies compute properly.
// Writes a size summary plus the full decompile of the function at
// the address in arg 2 to the file in arg 1.
//
// Usage: -postScript CreateRegionFunctions.java <entries.txt> <out.txt> <focus hex>

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

public class CreateRegionFunctions extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        List<Long> entries = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new FileReader(new File(args[0])))) {
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (!line.isEmpty()) {
                    entries.add(Long.parseLong(line, 16));
                }
            }
        }
        long focus = Long.parseLong(args[2], 16);
        int created = 0;
        int realBodies = 0;
        List<String> summary = new ArrayList<>();
        Function focusFn = null;
        for (long e : entries) {
            Address a = toAddr(e);
            Function fn = currentProgram.getFunctionManager().getFunctionContaining(a);
            if (fn == null) {
                disassemble(a);
                fn = createFunction(a, null);
                if (fn != null) {
                    created++;
                }
            }
            if (fn != null) {
                long size = fn.getBody().getNumAddresses();
                if (size > 4) {
                    realBodies++;
                }
                summary.add(String.format("%08x %s %d bytes", e, fn.getName(), size));
                if (e == focus) {
                    focusFn = fn;
                }
            }
        }
        println("CreateRegionFunctions: " + created + " created, "
                + realBodies + " with bodies > 4 bytes");

        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.setSimplificationStyle("decompile");
        ifc.openProgram(currentProgram);
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(args[1])))) {
            w.write("region functions (" + summary.size() + "):\n");
            for (String s : summary) {
                w.write("  " + s + "\n");
            }
            w.write("\n");
            if (focusFn != null) {
                w.write("/* ===== focus: " + focusFn.getName() + " @ "
                        + focusFn.getEntryPoint() + " ===== */\n");
                DecompileResults res = ifc.decompileFunction(focusFn, 60, monitor);
                if (res != null && res.getDecompiledFunction() != null) {
                    w.write(res.getDecompiledFunction().getC());
                } else {
                    w.write("/* decompile failed */\n");
                }
            } else {
                w.write("focus function not created\n");
            }
        } finally {
            ifc.dispose();
        }
        println("CreateRegionFunctions: wrote " + args[1]);
    }
}
