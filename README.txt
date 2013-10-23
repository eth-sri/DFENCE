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

Modified files:
---------------
  
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

The directory /tools/lli-synth/ is new with the main file there being lli-synth.cpp. 

Some of the changes are small and unrelated to DFENCE (like adding an #include for the latest library),
while others are changes related to DFENCE.

The files in the directory:

                    /lib/ExecutionEngine/Interpreter/ 
                    
represent the major changes to LLVM:

New files:
----------

  Action.h:
      Describes the action that the scheduler can choose for the interpreter to do – switching thread, 
      flushing memory or no action.
    
  CheckTrace.cpp and CheckTrace.h: 
      Used to check Linearizability and Sequential Consistency of a given trace 
      (by a given object of class History). Basically,  it generates all valid permutations of the trace and 
      checks if it is sequentially consistent or linearizable.
    
  conf.txt:
      Configuration file used to specify all parameters to DFENCE (described later).
    
  Constraints.cpp and Contraints.h:
      Used to capture constraints to be sent to the SAT solver.
    
  History.cpp and History.h:
      Capture the history of a trace. Records all the invocations of the functions 
      mentioned in the files malloc.txt (for the lock-free malloc algorithm) or wsq.txt (for the work-stealing queues). 
      The trace is recorded in the form of “function A begins … function B ends”.
    
  Params.cpp and Params.h:
      Parses all the parameters that are given in the configuration file and configures the interpreter 
      according to them.
    
  RWHistory.cpp and RWHistory.h:
      Rcords all reads and writes to the memory, which are shared by more than one thread. 
      The trace is recorded in the form of “thread X modified location Y”.
    
  Scheduler.cpp and Scheduler.h:
      Performs the choice of flushing memory or switching threads. The only one method in the class 
      selects that action and returns an object of class Action. If the choice is made to switch the
      thread, then the chosen thread is recorded in the returned object. If flushing memory is chosen, 
      then the thread and the buffer is recorded in the returned object (for TSO and PSO) and the variable 
      that will be flushed (for PSO).

  Spec* files:
      These represent executable specifications for various data structures and a memory allocator.
    
  wsq.h:
      A helper file to process work stealing queues.
    
  Various .txt files:
      These capture the names of the methods for the data structures under test.
    
  \SatSolver directory is an existing SAT solver (can be replaced with another solver if needed).
    
Modified files:
---------------    

  All other files in this directory are modified. The main changes are in Execution.cpp and in Interpreter.cpp where
  load and store instructions have been augmented to work with the buffered memory models TSO and PSO.
  There are also added variants of instructions to capture thread creation and joining.
  See the function visitCallSite in Execution.cpp to see what new functions have been added.  


Building DFENCE
===============

There are 2 options to building DFENCE:

1. Simply download the entire DFENCE repository, then type: 
    ./configure    (optionally you can give a -prefix where to install the files at make install)
    make
    make install 
    

2. Download the original LLVM 2.7 from: http://llvm.org/releases/2.7/llvm-2.7.tgz
   Then, copy all of the modified and new files (listed above) from the DFENCE repository into the llvm-2.7 directory.
   Then, proceed as in option 1: ./configure, make and make install.
   
   
Running DFENCE: Analyzing Algorithms
====================================

 - DFENCE (running the interpreter) has primiraly been tested on a 32-bit system.

 - For the LLVM-GCC compiler needed to produce bitecode files, again, download the corresponding binary 
   llvm-gcc version from 2.7: http://llvm.org/releases/download.html#2.7

 - update your PATH to include paths to the llvm-gcc and to where the 'lli-synth' file is found once you built DFENCE.
 
 - To run the compiled bitcode files that you wish to analyze, setup the variable CONFDIR, which gives the path to 
   conf.txt. To set CONFDIR use the following command (if DFENCE is build in /home/user):
 
   export CONFDIR=/home/user/DFENCE/lib/ExecutionEngine/Interpreter/
 
 - In conf.txt is saved the information about what program we are checking and the parameters of the checking procedure. 
   The file has the following structure:

   PROGRAM = {WSQ_FIFO, WSQ_LIFO, WSQ_CHASE, WSQ_ANCHOR, WSQ_THE, LF_MALLOC, SKIP_LIST }
   PROPERTY = {SC, LIN}
   WMM = {NONE, TSO, PSO}
   FLUSHPROB = {real number between 0 and 1}
   SCHEDULER = {RANDOM}
   LOG = {true, false}
   
   A sample conf.txt looks like this (make sure to have = as shown; parameters can be in any order):

   PROGRAM = WSQ_LIFO
   PROPERTY = SC
   WMM = TSO
   FLUSHPROB = 0.5
   SCHEDULER = RANDOM
   LOG = true
   
   PROGRAM   sets the name of the checked algorithm.
   
   PROPERLY  sets the property we want to check (Sequential Consistency in this case). 
             If you do not want to check Linearizability or SC, then remove the PROPERTY line from the file. 
             The default is none.
             
   WMM sets  the memory model, TSO in the example file above.
   
   FLUSHPROB sets the probability of flushing the buffer under the simulated weak memory model.
   
   SCHEDULER sets the used scheduling algorithm. For now only the RANDOM option is available. 
             It chooses randomly what do to: to switch to a thread or to flush the buffer.
             After that, the system chooses randomly which thread to switch or what buffer to flush 
            (an empty buffer can also be chosen for flushing).
             
   LOG       sets the option to log the shared reads and writes of the program execution. 
             If you want to use this functionality, use value 'true', otherwise use 'false'.
             
 - Compiling files to analy:

  llvm-gcc -emit-llvm -c <algorithm.c>
  
 - Run DFENCE to analyze the program:

  lli -force-interpreter algorithm.o  (this will only analyze the algorithm but will not run synthesis).
  
 - Run DFENCE to synthesize fences:
  
  lli-synth -force-interpreter algorithm.o  (
  
  The output from the synthesizer are the synthesized fences between LLVM  bitecodes.
  
  One can control how many rounds the synthesizer runs (see the PLDI'12 paper) in the lli-synth.cpp file.
  
