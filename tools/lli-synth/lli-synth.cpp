//===- lli-synth.cpp - LLVM Interpreter / Dynamic compiler ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This utility provides a simple wrapper around the LLVM Execution Engines,
// which allow the direct execution of LLVM programs through a Just-In-Time
// compiler, or through an interpreter if no JIT is available for this platform.
//
// This file was copied from lli.cpp and modified for DFENCE.
//
//===----------------------------------------------------------------------===//

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Type.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/IRReader.h" // added
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/System/Process.h"
#include "llvm/System/Signals.h"
#include "llvm/Target/TargetSelect.h"
#include <cerrno>
#include <time.h>

#include "../../lib/ExecutionEngine/Interpreter/Interpreter.h"
#include "../../lib/ExecutionEngine/Interpreter/Constraints.h"
#include "../../lib/ExecutionEngine/Interpreter/Params.h"

using namespace llvm;

namespace {
  // This was added so that we can re-run the program to get multiple traces
  cl::opt<int> RetryTime("try",
               cl::desc("How many traces should be exercised in each round..."), 
               cl::init(TRACES_PER_ROUND));

  cl::opt<std::string>
  InputFile(cl::desc("<input bitcode>"), cl::Positional, cl::init("-"));

  cl::list<std::string>
  InputArgv(cl::ConsumeAfter, cl::desc("<program arguments>..."));

  cl::opt<bool> ForceInterpreter("force-interpreter",
                                 cl::desc("Force interpretation: disable JIT"),
                                 cl::init(false));

  // Determine optimization level.
  cl::opt<char>
  OptLevel("O",
           cl::desc("Optimization level. [-O0, -O1, -O2, or -O3] "
                    "(default = '-O2')"),
           cl::Prefix,
           cl::ZeroOrMore,
           cl::init(' '));

  cl::opt<std::string>
  TargetTriple("mtriple", cl::desc("Override target triple for module"));

  cl::opt<std::string>
  MArch("march",
        cl::desc("Architecture to generate assembly for (see --version)"));

  cl::opt<std::string>
  MCPU("mcpu",
       cl::desc("Target a specific cpu type (-mcpu=help for details)"),
       cl::value_desc("cpu-name"),
       cl::init(""));

  cl::list<std::string>
  MAttrs("mattr",
         cl::CommaSeparated,
         cl::desc("Target specific attributes (-mattr=help for details)"),
         cl::value_desc("a1,+a2,-a3,..."));

  cl::opt<std::string>
  EntryFunc("entry-function",
            cl::desc("Specify the entry function (default = 'main') "
                     "of the executable"),
            cl::value_desc("function"),
            cl::init("main"));
  
  cl::opt<std::string>
  FakeArgv0("fake-argv0",
            cl::desc("Override the 'argv[0]' value passed into the executing"
                     " program"), cl::value_desc("executable"));
  
  cl::opt<bool>
  DisableCoreFiles("disable-core-files", cl::Hidden,
                   cl::desc("Disable emission of core files if possible"));

  cl::opt<bool>
  NoLazyCompilation("disable-lazy-compilation",
                  cl::desc("Disable JIT lazy compilation"),
                  cl::init(false));
}

static ExecutionEngine *EE = 0;

static void do_shutdown() {
  delete EE;
  llvm_shutdown();
}

std::string ErrorMsg;

extern Constraints constraintsHandler;
unsigned total_traces = 0;
unsigned buggy_traces = 0;
clock_t timeofInterp, timeofSolving, timeofVerify;
extern clock_t timeofChecking;

int InterpretRun(Module* Mod, int RetryTime, char** argv, char* const* envp,
									LLVMContext &Context, bool toSolver) { 
   double average_lits = 0.0;
   double accumul_lits = 0.0;

   while ((total_traces < RetryTime)) {// || (average_lits >= 5.0 * buggy_traces)) {
  	EngineBuilder builder(Mod);
  	builder.setMArch(MArch);
  	builder.setMCPU(MCPU);
  	builder.setMAttrs(MAttrs);
  	builder.setErrorStr(&ErrorMsg);
  	builder.setEngineKind(ForceInterpreter
                        ? EngineKind::Interpreter
                        : EngineKind::JIT);

  	// If we are supposed to override the target triple, do so now.
  	if (!TargetTriple.empty())
    	Mod->setTargetTriple(TargetTriple);

		CodeGenOpt::Level OLvl = CodeGenOpt::Default;
   	switch (OptLevel) {
	    default:
	    	errs() << argv[0] << ": invalid optimization level.\n";
    	    return 1;
     	    case ' ': break;
  	    case '0': OLvl = CodeGenOpt::None; break;
  	    case '1': OLvl = CodeGenOpt::Less; break;
  	    case '2': OLvl = CodeGenOpt::Default; break;
  	    case '3': OLvl = CodeGenOpt::Aggressive; break;
  	}

 	builder.setOptLevel(OLvl);

  	EE = builder.create();
  	if (!EE) {
    	  if (!ErrorMsg.empty())
      	     errs() << argv[0] << ": error creating EE: " << ErrorMsg << "\n";
      	  else
      	     errs() << argv[0] << ": unknown error creating EE!\n";
    	exit(1);
  	}

  	EE->RegisterJITEventListener(createOProfileJITEventListener());

  	EE->DisableLazyCompilation(NoLazyCompilation);

  	// If the user specifically requested an argv[0] to pass into the program,
  	// do it now.
  	if (!FakeArgv0.empty()) {
    	  InputFile = FakeArgv0;
  	} 
	else {
    	  // Otherwise, if there is a .bc suffix on the executable strip it off, it
    	  // might confuse the program.
    	  if (InputFile.rfind(".bc") == InputFile.length() - 3)
      	    InputFile.erase(InputFile.length() - 3);
  	}

  	// Add the module's name to the start of the vector of arguments to main().
  	InputArgv.insert(InputArgv.begin(), InputFile);

  	// Call the main function from M as if its signature were:
  	//   int main (int argc, char **argv, const char **envp)
  	// using the contents of Args to determine argc & argv, and the contents of
  	// EnvVars to determine envp.
  	//
  	Function *EntryFn = Mod->getFunction(EntryFunc);
  	if (!EntryFn) {
    	  errs() << '\'' << EntryFunc << "\' function not found in module.\n";
    	  return -1;
  	}

  	// If the program doesn't explicitly call exit, we will need the Exit 
  	// function later on to make an explicit call, so get the function now. 
  	Constant *Exit = Mod->getOrInsertFunction("exit", Type::getVoidTy(Context),
                                                    Type::getInt32Ty(Context),
                                                    NULL);
  
  	// Reset errno to zero on entry to main.
  	errno = 0;

	if (ForceInterpreter) {
		Interpreter* Intep = (Interpreter*)EE;
		Intep->runMain = false;
	}
 
  	// Run static constructors.
  	EE->runStaticConstructorsDestructors(false);

  	if (NoLazyCompilation) {
    		for (Module::iterator I = Mod->begin(), E = Mod->end(); I != E; ++I) {
      		  Function *Fn = &*I;
      		  if (Fn != EntryFn && !Fn->isDeclaration())
        	     EE->getPointerToFunction(Fn);
    	        }
  	}

  	// Run main.
 	if (ForceInterpreter) {
		Interpreter* Intep = (Interpreter*)EE;
		Intep->toFix = toSolver; // set it to lli-synth mode
		Intep->segmentFaultFlag = false;
		//Intep->allonAssertExist = false;
		Intep->runMain = true;
	}

 	int Result = EE->runFunctionAsMain(EntryFn, InputArgv, envp);

  	if (ForceInterpreter) {
		Interpreter* Intep = (Interpreter*)EE;
		Intep->runMain = false;
	}

  	// Run static destructors.
  	EE->runStaticConstructorsDestructors(true);

	if (ForceInterpreter) {
		Interpreter* Intep = (Interpreter*)EE;
		if (Intep->ExitStatus == 253) {
			/* add the constrains to the SAT solver */
			buggy_traces++;
			accumul_lits += 1.0 * constraintsHandler.GetLitSingleNumber();
			average_lits = accumul_lits / buggy_traces;
			//dbgs() << "- buggy trace " << buggy_traces << ": " 
		//	 << constraintsHandler.GetLitSingleNumber() << " Lits in this Clause\n";
			if (constraintsHandler.GetLitSingleNumber() == 0) {
				exit(254);
			}
			constraintsHandler.AddToSolver();
			//constraintsHandler.PrintConstraintInst(Mod);
		}
	}
	total_traces++;
    }
    return 0;
}

//===----------------------------------------------------------------------===//
// main Driver function
//
int main(int argc, char **argv, char * const *envp) {
	timeofChecking = 0;
	timeofSolving = 0;
	clock_t start = clock();
 
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  
  LLVMContext &Context = getGlobalContext();
  atexit(do_shutdown);  // Call llvm_shutdown() on exit.

  // If we have a native target, initialize it to ensure it is linked in and
  // usable by the JIT.
  InitializeNativeTarget();

  cl::ParseCommandLineOptions(argc, argv,
                              "llvm interpreter & dynamic compiler\n");
 
  // If the user doesn't want core files, disable them.
  if (DisableCoreFiles)
    sys::Process::PreventCoreFiles();

  // Load the bitcode...
  Module *Mod = NULL;

	SMDiagnostic Err;
	Mod = ParseIRFile(InputFile, Err, Context);

/*
  if (MemoryBuffer *Buffer = MemoryBuffer::getFileOrSTDIN(InputFile,&ErrorMsg)){
    Mod = getLazyBitcodeModule(Buffer, Context, &ErrorMsg);
    if (!Mod) delete Buffer;
  }
*/
  
  if (!Mod) {
    errs() << argv[0] << ": error loading program '" << InputFile << "': "
           << ErrorMsg << "\n";
    exit(1);
  }

  // If not jitting lazily, load the whole bitcode file eagerly too.
  if (NoLazyCompilation) {
    if (Mod->MaterializeAllPermanently(&ErrorMsg)) {
      errs() << argv[0] << ": bitcode didn't read correctly.\n";
      errs() << "Reason: " << ErrorMsg << "\n";
      exit(1);
    }
  }

	/* print out the original IR */
	raw_ostream *Out = &outs();
	const std::string& IFN = InputFile;
	std::string OutputFilename;
	int Len = IFN.length();
	if (IFN[Len-2] == '.' && IFN[Len-1] == 'o') {
		OutputFilename = std::string(IFN.begin(), IFN.end()-2)+".ll";
	} else {
		OutputFilename = IFN+".ll";
	}

       if (OutputFilename != "-") {
         sys::RemoveFileOnSignal(sys::Path(OutputFilename));
         std::string ErrorInfo;
         Out = new raw_fd_ostream(OutputFilename.c_str(), ErrorInfo, raw_fd_ostream::F_Binary);
   	 if (!ErrorInfo.empty()) {
           errs() << ErrorInfo << '\n';
	   delete Out;
           return 1;
         }
       }
	
       *Out << *Mod;

	if (Out != &outs()) {
	    delete Out;
	}

	unsigned label = 0;

	/* added label to the instructions: we don't need an additional pass */
        for (Module::iterator F = Mod->begin(), FE = Mod->end(); F != FE; F++) {
          for (Function::iterator BB = F->begin(), BBE = F->end(); BB != BBE; BB++) {
            for (BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; ) {
		label++;
		I->label_instr = label;
        	I++;
      	    }
          }
        }

	dbgs() << "There are " << (label - 1) << " instructions in total!\n";
	// make it more easier using label to index instruction 
	constraintsHandler.SetupInstructionLabelMap(Mod);

	int round = 0;
	while (1) {
		round++;
		
		dbgs() << "/-----/ Round " << round << " /------/\n";

		clock_t start3 = clock();
		int ret = InterpretRun(Mod, RetryTime, argv, envp, Context, true);
		timeofInterp += clock() - start3;
		timeofVerify = clock() - start3;

		if (ret != 0) return ret;

		/* solve the constraints */
		dbgs() << "/-----/ Execution completes /----------------------------------/\n";
		dbgs() << "Try " << total_traces << " times," 
           << " find " << buggy_traces << " buggy traces\n";
		dbgs() << "Collect " << constraintsHandler.GetLitTotalNumber() << " lits and " 
										 		 << buggy_traces << " clauses to SAT solver...\n\n"; 
		if (buggy_traces == 0) {
			dbgs() << "/-----/ Converged! /-----------------------------------------/\n\n";
			break;
		}
		
		clock_t start1 = clock();
		dbgs() << "/-----/ Starting SAT solving /---------------------------------/\n";
		if (constraintsHandler.Solve()) {
			constraintsHandler.Merge();
			dbgs() << "/-----/ Showing instr-pairs need to enordered /----------------/\n";
			constraintsHandler.PrintOrderedInst();
		} else {
			dbgs() << "/-----/ Can't find out solutions /-----------------------------/\n\n";
			return 1;
		}
		
		dbgs() << "/-----/ Inserting fences to IR /-------------------------------/\n\n";
		constraintsHandler.InsertFences(Mod);
		
		timeofSolving += clock() - start1;

		dbgs() << "/-----/ Restart interpreter /----------------------------------/\n\n";
		// start to execute: start to put a loop here!
		total_traces = 0;
		buggy_traces = 0;
		constraintsHandler.Flush();
	}

	dbgs() << "/-----/ Printing out fixed IR /-------------------------------/\n\n";
	constraintsHandler.PrintFinalInst();

	Out = &outs();
	if (IFN[Len-2] == '.' && IFN[Len-1] == 'o') {
		OutputFilename = std::string(IFN.begin(), IFN.end()-2)+".fixed.ll";
	} else {
		OutputFilename = IFN+".fixed.ll";
	}

 	if (OutputFilename != "-") {
   	  sys::RemoveFileOnSignal(sys::Path(OutputFilename));
   	  std::string ErrorInfo;
   	  Out = new raw_fd_ostream(OutputFilename.c_str(), ErrorInfo, raw_fd_ostream::F_Binary);

 	  if (!ErrorInfo.empty()) {
        	errs() << ErrorInfo << '\n';
        	delete Out;
     	        return 1;
   	  }
 	}
	
	*Out << *Mod;
	
	if (Out != &outs()) {
		delete Out;
	}

	timeofInterp = timeofInterp - timeofChecking - timeofVerify;
	dbgs() << "time stat: \n";
	dbgs() << "Interp: " << (double)timeofInterp / CLOCKS_PER_SEC << "\n";
	dbgs() << "Checking: " << (double)timeofChecking / CLOCKS_PER_SEC << "\n";
	dbgs() << "Solving: " << (double)timeofSolving / CLOCKS_PER_SEC << "\n";
	dbgs() << "Verify: " << (double)timeofVerify / CLOCKS_PER_SEC << "\n";
 
	return 0;
}
