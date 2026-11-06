// FixFunctions.java - correct three carve errors found by the
// cross-check in notes 026:
//  1. FUN_00106b58 is not a function entry (no prologue) - it
//     is the tail of FUN_00105d80. Remove it and extend
//     FUN_00105d80's body to 0x109E58.
//  2. FUN_000f3db0 is truncated; extend its body to 0xF4838.
//  3. FUN_000fc1a0 (scene submit, called by game_loop) is
//     uncarved; create it.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class FixFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        // 1. remove the false 106b58 function
        Address a106 = toAddr(0x106B58L);
        Function f106 = getFunctionAt(a106);
        if (f106 != null) {
            currentProgram.getFunctionManager().removeFunction(a106);
            println("FixFunctions: removed false function at 106b58");
        }
        // 2. extend 105d80
        Address a105 = toAddr(0x105D80L);
        Function f105 = getFunctionAt(a105);
        if (f105 != null) {
            f105.setBody(new AddressSet(a105, toAddr(0x109E57L)));
            println("FixFunctions: 105d80 body extended, size=" +
                    f105.getBody().getNumAddresses());
        } else {
            println("FixFunctions: 105d80 NOT FOUND");
        }
        // 3. extend f3db0
        Address aF3 = toAddr(0xF3DB0L);
        Function fF3 = getFunctionAt(aF3);
        if (fF3 != null) {
            fF3.setBody(new AddressSet(aF3, toAddr(0xF4837L)));
            println("FixFunctions: f3db0 body extended, size=" +
                    fF3.getBody().getNumAddresses());
        } else {
            println("FixFunctions: f3db0 NOT FOUND");
        }
        // 4. create fc1a0
        Address aFc = toAddr(0xFC1A0L);
        if (getFunctionAt(aFc) == null) {
            Function f = currentProgram.getFunctionManager().createFunction(
                    "FUN_000fc1a0", aFc, null, SourceType.USER_DEFINED);
            println("FixFunctions: fc1a0 created, body=" +
                    (f == null ? "null" : "" + f.getBody().getNumAddresses()));
        }
    }
}
