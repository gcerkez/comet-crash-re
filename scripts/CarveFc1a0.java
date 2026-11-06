import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class CarveFc1a0 extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address a = toAddr(0xFC1A0L);
        if (getFunctionAt(a) == null) {
            Function f = currentProgram.getFunctionManager().createFunction(
                "scene_submitDraw", a,
                new AddressSet(a, toAddr(0xFC6BFL)), SourceType.USER_DEFINED);
            println("CarveFc1a0: created size=" + f.getBody().getNumAddresses());
        } else {
            println("CarveFc1a0: already exists");
        }
    }
}
