// SetBodyDecompile.java — force a function's body to an explicit
// address range and decompile it. For the mission-setup hub: one
// giant function whose computed branches defeat flow carving.
// Usage: -postScript SetBodyDecompile.java <entry hex> <end hex> <out.txt>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;

public class SetBodyDecompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address start = toAddr(Long.parseLong(args[0], 16));
        Address end = toAddr(Long.parseLong(args[1], 16));
        // Remove every function whose entry lies in [start, end):
        // the range becomes one function.
        java.util.List<Address> toRemove = new java.util.ArrayList<>();
        ghidra.program.model.listing.FunctionIterator fit =
                currentProgram.getFunctionManager().getFunctions(start, true);
        while (fit.hasNext()) {
            Function f = fit.next();
            if (f.getEntryPoint().compareTo(end) >= 0) {
                break;
            }
            toRemove.add(f.getEntryPoint());
        }
        for (Address a : toRemove) {
            currentProgram.getFunctionManager().removeFunction(a);
        }
        disassemble(start);
        Function fn = createFunction(start, null);
        fn.setBody(new AddressSet(start, end));
        println("SetBodyDecompile: " + fn.getName() + " body now "
                + fn.getBody().getNumAddresses() + " bytes");
        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.setSimplificationStyle("decompile");
        ifc.openProgram(currentProgram);
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(args[2])))) {
            DecompileResults res = ifc.decompileFunction(fn, 120, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                w.write(res.getDecompiledFunction().getC());
            } else {
                w.write("/* decompile failed */\n");
            }
        } finally {
            ifc.dispose();
        }
        println("SetBodyDecompile: wrote " + args[2]);
    }
}
