// RecreateFunction.java — removes a stub function (created earlier
// with a 1-byte body), disassembles at its entry, recreates it so the
// body computes from the real flow, and decompiles it to arg 1.
//
// Usage: -postScript RecreateFunction.java <hex address> <out.txt>

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;

public class RecreateFunction extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address a = toAddr(Long.parseLong(args[0], 16));
        currentProgram.getFunctionManager().removeFunction(a);
        disassemble(a);
        Function fn = createFunction(a, null);
        println("RecreateFunction: " + args[0] + " -> "
                + (fn != null ? fn.getName() + " (" + fn.getBody().getNumAddresses()
                        + " bytes)" : "FAILED"));

        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.setSimplificationStyle("decompile");
        ifc.openProgram(currentProgram);
        try (BufferedWriter w = new BufferedWriter(new FileWriter(new File(args[1])))) {
            if (fn != null) {
                DecompileResults res = ifc.decompileFunction(fn, 60, monitor);
                if (res != null && res.getDecompiledFunction() != null) {
                    w.write(res.getDecompiledFunction().getC());
                } else {
                    w.write("/* decompile failed */\n");
                }
            }
        } finally {
            ifc.dispose();
        }
        println("RecreateFunction: wrote " + args[1]);
    }
}
