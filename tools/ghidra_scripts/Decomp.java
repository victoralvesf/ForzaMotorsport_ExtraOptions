// Decompile the functions containing each address given in the args; writes to file arg[0]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class Decomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(args[0]));
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();
        int timeout = 180;
        for (int i = 1; i < args.length; i++) {
            String a = args[i];
            if (a.startsWith("t:")) { timeout = Integer.parseInt(a.substring(2)); continue; } // decompile timeout in seconds
            if (a.startsWith("x:")) { // xrefs to address
                Address ad = toAddr(a.substring(2));
                out.println("==== XREFS to " + ad);
                for (Reference r : getReferencesTo(ad)) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    out.println("  " + r.getFromAddress() + " " + r.getReferenceType() + " in " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
                }
                continue;
            }
            Address ad = toAddr(a);
            Function f = fm.getFunctionContaining(ad);
            if (f == null) {
                disassemble(ad);
                f = createFunction(ad, null);
            }
            if (f == null) { out.println("==== no function at " + ad); continue; }
            out.println("==== FUNCTION " + f.getName() + " @ " + f.getEntryPoint() + " (query " + ad + ")");
            DecompileResults r = di.decompileFunction(f, timeout, monitor);
            if (r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
            else out.println("decompile failed: " + r.getErrorMessage());
        }
        out.close();
    }
}
