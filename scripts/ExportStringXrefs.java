// ExportStringXrefs.java — Ghidra headless script (Comet Crash reconstruction).
//
// String references in this PPC64 binary go through pointer slots in the
// writable segment (TOC-style): code loads the string pointer from a
// slot, so instruction->string references don't exist directly — but
// references TO the slots do. This script takes a list of slot addresses
// (one hex VA per line, produced by scripts/find_string_slots.py) and
// exports which function references each slot.
//
// Usage: -postScript ExportStringXrefs.java <slots.txt> <out.tsv>
// Output rows: function_address <TAB> slot_address

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.TreeSet;

public class ExportStringXrefs extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("ExportStringXrefs: need <slots.txt> <out.tsv>");
            return;
        }
        ReferenceManager refMgr = currentProgram.getReferenceManager();
        int slots = 0;
        int rows = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(new File(args[0])));
             BufferedWriter w = new BufferedWriter(new FileWriter(new File(args[1])))) {
            w.write("function\tslot\n");
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty()) {
                    continue;
                }
                long va = Long.parseLong(line.replace("0x", ""), 16);
                Address slot = toAddr(va);
                slots++;
                TreeSet<String> funcs = new TreeSet<>();
                ReferenceIterator rit = refMgr.getReferencesTo(slot);
                while (rit.hasNext()) {
                    Reference ref = rit.next();
                    Function f = currentProgram.getFunctionManager()
                            .getFunctionContaining(ref.getFromAddress());
                    if (f != null) {
                        funcs.add(f.getEntryPoint().toString());
                    }
                }
                for (String fn : funcs) {
                    w.write(fn + "\t" + line + "\n");
                    rows++;
                }
            }
        }
        println("ExportStringXrefs: " + slots + " slots, " + rows + " function-slot rows");
    }
}
