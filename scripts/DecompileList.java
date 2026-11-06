// DecompileList.java - decompile the functions containing
// the addresses listed in args[0] (one hex address per line)
// and write each result to <args[1]>/<addr>.c, preceded by a
// header line with the decompiler's warning text (if any).
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class DecompileList extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        List<String> lines = Files.readAllLines(Paths.get(args[0]));
        File outDir = new File(args[1]);
        outDir.mkdirs();
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        for (String ln : lines) {
            ln = ln.trim();
            if (ln.isEmpty()) continue;
            Address a = toAddr(Long.parseLong(ln, 16));
            Function f = getFunctionContaining(a);
            if (f == null) {
                println("DecompileList: no function at " + ln);
                continue;
            }
            DecompileResults res = ifc.decompileFunction(f, 120, monitor);
            StringBuilder sb = new StringBuilder();
            sb.append("// function ").append(f.getName())
              .append(" @ ").append(f.getEntryPoint())
              .append(" size ").append(f.getBody().getNumAddresses()).append("\n");
            if (!res.decompileCompleted()) {
                sb.append("// DECOMPILE INCOMPLETE: ")
                  .append(res.getErrorMessage()).append("\n");
            }
            if (res.getDecompiledFunction() != null) {
                sb.append(res.getDecompiledFunction().getC());
            }
            Files.write(Paths.get(outDir.getPath(), ln + ".c"),
                    sb.toString().getBytes());
            println("DecompileList: wrote " + ln + ".c");
        }
        ifc.dispose();
    }
}
