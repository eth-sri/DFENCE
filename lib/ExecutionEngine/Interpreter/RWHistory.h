//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The class was added for DFENCE.
//
//===----------------------------------------------------------------------===//

#ifndef LLI_RWHISTORY_H
#define LLI_RWHISTORY_H

#include "llvm/ExecutionEngine/Thread.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Value.h"

#include <vector>

using namespace std;
using namespace llvm;

typedef enum {READ, WRITE, FLUSH_INSTR, FLUSH_FENCE, FLUSH_CAS_TSO, FLUSH_CAS_PSO, FLUSH_RANDOM_TSO, FLUSH_RANDOM_PSO, SPAWN, JOIN} RWType;

struct rwtrace_elem {
	Thread thr;     // the thread that executes the operation
	RWType type; 	// the type of the operation
	int value;  	// read/written value
	int* location; 	// location of the operation
	int label;
};

class RWHistory {
private:
	// recorded trace of all SHARED accesses
	//vector<rwtrace_elem> shared_rec;  	
public:
	// recorded trace of all SHARED accesses
	vector<rwtrace_elem> shared_rec;  	
	// recorded trace on all non-local variables
  vector<rwtrace_elem> rwtrace_rec;  	
	// records a load operation of non-local variable
  void RecordRWEvent(GenericValue, GenericValue, Thread, RWType, int);
	// records a flush operation of non-local variable
	// records a spawn or join instruction, even though it is not a RW instr
  void RecordRWEvent(Thread, RWType, int);
  void RecordRWEvent(GenericValue, Thread, RWType, int);
	// finds the trace of all SHARED accesses. 
	// Use it after you have recorded all stores and loads of non-local variables
	void FindSharedRW();  
	void PrintSharedRW();  
};
 
#endif

