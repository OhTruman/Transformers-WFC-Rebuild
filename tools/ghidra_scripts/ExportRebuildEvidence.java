// Export evidence from a saved analysis snapshot. Does not change the program.
//@category WFCRebuild
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
public class ExportRebuildEvidence extends GhidraScript {
    private String clean(Object value) { return String.valueOf(value).replace('\t',' ').replace('\r',' ').replace('\n',' '); }
    private PrintWriter writer(File directory, String name) throws Exception {
        return new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(directory,name)), StandardCharsets.UTF_8));
    }
    @Override public void run() throws Exception {
        File out = new File(getScriptArgs()[0]); out.mkdirs();
        int functions=0, named=0, symbols=0, strings=0;
        List<Function> candidates=new ArrayList<>();
        try(PrintWriter f=writer(out,"functions.tsv")) {
            f.println("address\tname\tsignature\tbody_bytes");
            FunctionIterator it=currentProgram.getFunctionManager().getFunctions(true);
            while(it.hasNext() && !monitor.isCancelled()) {
                Function fn=it.next(); ++functions;
                String name=fn.getName(true);
                if(!fn.getName().matches("(?i)(FUN|Function)_[0-9a-f]+")) ++named;
                f.println(fn.getEntryPoint()+"\t"+clean(name)+"\t"+clean(fn.getSignature())+"\t"+fn.getBody().getNumAddresses());
                if(name.matches("(?i).*(Transform|Pawn|Player|Character|Health|Damage|Weapon|Vehicle|Camera).*") && !fn.isExternal() && fn.getBody().getNumAddresses()>4) candidates.add(fn);
            }
        }
        try(PrintWriter f=writer(out,"symbols.tsv")) {
            f.println("address\ttype\tname");
            SymbolIterator it=currentProgram.getSymbolTable().getAllSymbols(true);
            while(it.hasNext() && !monitor.isCancelled()) {
                Symbol s=it.next(); ++symbols;
                f.println(s.getAddress()+"\t"+s.getSymbolType()+"\t"+clean(s.getName(true)));
            }
        }
        try(PrintWriter x=writer(out,"gameplay-string-xrefs.tsv"); PrintWriter f=writer(out,"strings.tsv")) {
            x.println("string_address\tstring\tref_from\tref_type\tfunction");
            f.println("address\tvalue");
            DataIterator it=currentProgram.getListing().getDefinedData(true);
            while(it.hasNext() && !monitor.isCancelled()) {
                Data d=it.next(); Object v=d.getValue();
                if(v instanceof String) {
                    ++strings; f.println(d.getAddress()+"\t"+clean(v));
                    String value=(String)v;
                    if(value.matches("(?i).*(TnPawn|TnPlayer|TakeDamage|TransformTo|Transformation|RobotMode|VehicleMode|PlayerCamera|Health).*")) {
                        for(Reference ref:currentProgram.getReferenceManager().getReferencesTo(d.getAddress())) {
                            Function fn=currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                            x.println(d.getAddress()+"\t"+clean(value)+"\t"+ref.getFromAddress()+"\t"+ref.getReferenceType()+"\t"+(fn==null?"":fn.getName(true)));
                            if(fn!=null && !fn.isExternal() && fn.getBody().getNumAddresses()>4 && !candidates.contains(fn)) candidates.add(fn);
                        }
                    }
                }
            }
        }
        try(PrintWriter f=writer(out,"summary.txt")) {
            f.println("Program="+currentProgram.getName());
            f.println("Language="+currentProgram.getLanguageID());
            f.println("Compiler="+currentProgram.getCompilerSpec().getCompilerSpecID());
            f.println("ImageBase="+currentProgram.getImageBase());
            f.println("Functions="+functions); f.println("NonGenericFunctionNames="+named);
            f.println("Symbols="+symbols); f.println("DefinedStrings="+strings);
            f.println("GameplayNamedCandidates="+candidates.size());
        }
        DecompInterface decompiler=new DecompInterface();
        try {
            if(!decompiler.openProgram(currentProgram)) throw new IOException(decompiler.getLastMessage());
            int exported=0;
            if(candidates.isEmpty()) {
                FunctionIterator fallback=currentProgram.getFunctionManager().getFunctions(true);
                while(fallback.hasNext() && candidates.size()<12) {
                    Function fn=fallback.next();
                    if(!fn.isExternal() && fn.getBody().getNumAddresses()>32) candidates.add(fn);
                }
            }
            for(Function fn:candidates) {
                if(exported>=12 || monitor.isCancelled()) break;
                DecompileResults result=decompiler.decompileFunction(fn,20,monitor);
                if(!result.decompileCompleted()) continue;
                try(PrintWriter f=writer(out,fn.getEntryPoint()+".c")) {
                    f.println("// Evidence export only: Ghidra pseudocode, not recovered original source.");
                    f.println("// "+fn.getName(true)+" @ "+fn.getEntryPoint());
                    f.println(result.getDecompiledFunction().getC());
                }
                ++exported;
            }
            println("Exported "+functions+" functions, "+symbols+" symbols, "+strings+" strings, "+exported+" decompilations.");
        } finally { decompiler.dispose(); }
    }
}

