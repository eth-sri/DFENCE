Low Level Virtual Machine (LLVM)
================================

This directory and its subdirectories contain source code for the Low Level
Virtual Machine, a toolkit for the construction of highly optimized compilers,
optimizers, and runtime environments.

LLVM is open source software. You may freely distribute it under the terms of
the license agreement found in LICENSE.txt.

Please see the HTML documentation provided in docs/index.html for further
assistance with LLVM.

If you're writing a package for LLVM, see docs/Packaging.html for our
suggestions.


LLVM Extensions for DFENCE
==========================

The interpreter of this LLVM version has been extended according to the following PLDI'12 paper:

"Dynamic Synthesis for Relaxed Memory Models", PLDI'12
URL: http://www.srl.inf.ethz.ch/papers/pldi12-fender.pdf

The following files have been modified in the DFENCE version vs. the LLVM-2.7:

/include/llvm/ADT/DenseMap.h 
/include/llvm/ADT/ilist.h 
/include/llvm/ADT/SmallVector.h 
/include/llvm/ExecutionEngine/ExecutionEngine.h 
/include/llvm/ExecutionEngine/GenericValue.h 
/include/llvm/ExecutionEngine/Thread.h 
/include/llvm/ExecutionEngine/ThreadKey.h 
/include/llvm/Instruction.h 
/include/llvm/Use.h 
/lib/ExecutionEngine/ExecutionEngine.cpp 
/lib/Target/ARM/ARMConstantPoolValue.h 
/tools/lli/lli.cpp 
/tools/Makefile
The directory /tools/lli-synth/ is new.

Some of the changes are small and unrelated to DFENCE (like adding an #include for the latest library),
while others are changes related to DFENCE.

The files in the directory:

                    /lib/ExecutionEngine/Interpreter/ 
                    
represent the major changes to LLVM:

  New files:

    Action.h
    CheckTrace.cpp and CheckTrace.h are used to check Linearizability and Sequential Consistency.
    conf.txt is a configuration file
    Constraints.cpp and Contraints.h are used to capture constraints to be sent to the SAT solver.
    History.cpp and History.h capture the history of a trace.
    Params.cpp and Params.h are used to control the various parameters to the framework.
    RWHistory.cpp and RWHistory.h capture a trace of shared reads and writes.
    Scheduler.cpp and Scheduler.h capture the various schedulers used to trigger buggy executions.
    All the Spec* files are basically executable specifications for various data structures.a
    wsq.h is a helper file to process work stealing queues.
    There are also various .txt files which capture the names of the methods for the data structures under test.
    \SatSolver directory is an existing SAT solver (can be replaced with another solver if needed).
  
   
  Modified files:
    All other files are modified. The main changes are in Execution.cpp and in Interpreter.cpp where
    load and store instructions have been augmented to work with the buffered memory models TSO and PSO.
    There are also added variants of instructions to capture thread creation and joining.
    See the function visitCallSite in Execution.cpp to see what new functions have been added.  



