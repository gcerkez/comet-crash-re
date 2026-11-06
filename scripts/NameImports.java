// NameImports.java — Ghidra headless script (Comet Crash reconstruction).
//
// Reads analysis/imports.tsv (produced by scripts/match_fnids.py) and
// labels every identified import-table slot in the program with the SDK
// function name it resolves to at load time. After this runs, the
// decompiler shows calls through the table as e.g. (*cellPadGetData)(...)
// instead of (*(code *)*(uint *)DAT_001f2b0c)().
//
// Usage: -postScript NameImports.java <absolute path to imports.tsv>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

public class NameImports extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("NameImports: missing imports.tsv path argument");
            return;
        }
        SymbolTable symtab = currentProgram.getSymbolTable();
        int named = 0;
        int skipped = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(new File(args[0])))) {
            String line;
            boolean header = true;
            while ((line = r.readLine()) != null) {
                if (header) {
                    header = false;
                    continue;
                }
                String[] parts = line.split("\t", -1);
                if (parts.length < 5) {
                    continue;
                }
                String name = parts[4].trim();
                if (name.isEmpty()) {
                    skipped++;
                    continue;
                }
                Address addr = toAddr(Long.parseLong(parts[2], 16));
                // Primary label with the SDK name; keep it in the global
                // namespace so decompiled references print plainly.
                symtab.createLabel(addr, name, currentProgram.getGlobalNamespace(),
                        SourceType.USER_DEFINED);
                named++;
            }
        }
        println("NameImports: labeled " + named + " import slots, " + skipped + " left unnamed");
    }
}
