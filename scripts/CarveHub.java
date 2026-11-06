// CarveHub.java — the mission-setup hub is ONE giant function
// (entry 0xF6990) whose computed branches defeat flow carving.
// Remove every function whose entry lies in [entry, end), create
// one function, force its body to the whole range, decompile.
// Usage: -postScript CarveHub.java <entry hex> <end hex> <out.txt>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

public class CarveHub extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address start = toAddr(Long.parseLong(args[0], 16));
        Address end = toAddr(Long.parseLong(args[1], 16));
        List<Address> toRemove = new ArrayList<>();
        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(start, true);
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
        println("CarveHub: removed " + toRemove.size() + " functions in range");
        disassemble(start);
        // Low-level create with an explicit body: the script-level
        // createFunction flow analysis claims the next function's
        // entry byte and throws OverlappingFunctionException.
        Function fn = currentProgram.getFunctionManager().createFunction(
                "mission_setup_hub", start,
                new AddressSet(start, end.previous()),
                ghidra.program.model.symbol.SourceType.USER_DEFINED);
        println("CarveHub: hub body = " + fn.getBody().getNumAddresses() + " bytes");
        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.setSimplificationStyle("decompile");
        ifc.openProgram(currentProgram);
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(args[2])))) {
            DecompileResults res = ifc.decompileFunction(fn, 180, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                w.write(res.getDecompiledFunction().getC());
            } else {
                w.write("/* decompile failed */\n");
            }
        } finally {
            ifc.dispose();
        }
        println("CarveHub: wrote " + args[2]);
    }
}
