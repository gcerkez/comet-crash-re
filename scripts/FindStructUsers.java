// FindStructUsers.java — who references the asset-registry struct
// region, the arenaGraphics strings, and the ObjectGroup error
// string? Uses Ghidra's own reference manager (its analyzer does
// constant propagation our Capstone scans cannot).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressFactory;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class FindStructUsers extends GhidraScript {
    @Override
    public void run() throws Exception {
        ReferenceManager rm = currentProgram.getReferenceManager();
        AddressFactory af = currentProgram.getAddressFactory();
        long[][] ranges = {
            {0x1F5E00, 0x1F5F00},   // arenaGraphics string slots
            {0x1F6000, 0x1F6500},   // the static struct / registry
            {0x17CA80, 0x17CB20},   // arenaGraphics.cpp strings
            {0x17E0B0, 0x17E0B4},   // ObjectGroup.cpp error string
            {0x1F6800, 0x1F6804},   // ObjectGroup error slot
        };
        int total = 0;
        for (long[] rg : ranges) {
            for (long va = rg[0]; va < rg[1]; va += 4) {
                Address a = af.getDefaultAddressSpace().getAddress(va);
                ReferenceIterator it = rm.getReferencesTo(a);
                while (it.hasNext()) {
                    Reference ref = it.next();
                    Address from = ref.getFromAddress();
                    Function fn = currentProgram.getFunctionManager()
                            .getFunctionContaining(from);
                    println("REF to " + a + " from " + from + " fn="
                            + (fn == null ? "?"
                                          : fn.getName() + "@" + fn.getEntryPoint())
                            + " type=" + ref.getReferenceType());
                    total++;
                }
            }
        }
        println("TOTAL REFS: " + total);
    }
}
