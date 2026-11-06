// RenameFunctions.java — Ghidra headless script (Comet Crash reconstruction).
//
// Applies evidence-based function names:
//   arg 0: a CSV of <hex address>,<name> direct renames (boot walk
//          findings: _entry chain, main, the input seam).
//   arg 1: analysis/imports.tsv — for every named import, the thunk
//          function that references its table slot is renamed to the
//          SDK function name, so decompiled call sites read
//          cellPadGetData(...) instead of FUN_00173da4(...).
//
// Usage: -postScript RenameFunctions.java <renames.csv> <imports.tsv>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.HashSet;
import java.util.Set;

public class RenameFunctions extends GhidraScript {

    private void rename(Function fn, String name) {
        try {
            fn.setName(name, SourceType.USER_DEFINED);
        } catch (Exception e) {
            println("RenameFunctions: failed " + fn.getEntryPoint() + " -> " + name
                    + ": " + e.getMessage());
        }
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        int direct = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(new File(args[0])))) {
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) {
                    continue;
                }
                String[] parts = line.split(",", 2);
                Function fn = currentProgram.getFunctionManager()
                        .getFunctionAt(toAddr(Long.parseLong(parts[0], 16)));
                if (fn != null) {
                    rename(fn, parts[1].trim());
                    direct++;
                } else {
                    println("RenameFunctions: no function at " + parts[0]);
                }
            }
        }
        println("RenameFunctions: " + direct + " direct renames");

        ReferenceManager refMgr = currentProgram.getReferenceManager();
        int thunks = 0;
        int skipped = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(new File(args[1])))) {
            String line;
            boolean header = true;
            while ((line = r.readLine()) != null) {
                if (header) {
                    header = false;
                    continue;
                }
                String[] parts = line.split("\t", -1);
                if (parts.length < 5 || parts[4].isEmpty()) {
                    continue;
                }
                Address slot = toAddr(Long.parseLong(parts[2], 16));
                Set<Long> addrs = new HashSet<>();
                ReferenceIterator rit = refMgr.getReferencesTo(slot);
                while (rit.hasNext()) {
                    Reference ref = rit.next();
                    Function tf = currentProgram.getFunctionManager()
                            .getFunctionContaining(ref.getFromAddress());
                    if (tf != null) {
                        addrs.add(tf.getEntryPoint().getOffset());
                    }
                }
                for (long a : addrs) {
                    Function tf = currentProgram.getFunctionManager().getFunctionAt(toAddr(a));
                    if (tf != null && tf.getName().startsWith("FUN_")) {
                        rename(tf, parts[4]);
                        thunks++;
                    } else {
                        skipped++;
                    }
                }
            }
        }
        println("RenameFunctions: " + thunks + " import thunks renamed, " + skipped + " skipped");
    }
}
