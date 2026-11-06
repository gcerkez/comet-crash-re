import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;

public class ExtendRenderText extends GhidraScript {
    @Override
    public void run() throws Exception {
        Function f = getFunctionAt(toAddr(0x15ADF8L));
        if (f == null) { println("ExtendRenderText: not found"); return; }
        f.setBody(new AddressSet(toAddr(0x15ADF8L), toAddr(0x15CAC7L)));
        println("ExtendRenderText: gui_renderText body now " + f.getBody().getNumAddresses());
    }
}
