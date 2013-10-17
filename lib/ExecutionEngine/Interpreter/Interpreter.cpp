//===- Interpreter.cpp - Top-Level LLVM Interpreter Implementation --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the top-level functionality for the LLVM interpreter.
// This interpreter is designed to be a very simple, portable, inefficient
// interpreter.
//
//===----------------------------------------------------------------------===//

#include "Interpreter.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/ExecutionEngine/Thread.h"
#include "llvm/Instruction.h"
#include "llvm/User.h"
#include "llvm/Value.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/FormattedStream.h"
#include "Params.h"
#include "History.h"
#include <cstring>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <fstream>
#include <map>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <sys/time.h>
using namespace llvm;

namespace {

	static struct RegisterInterp {
		RegisterInterp() { Interpreter::Register(); }
	} InterpRegistrator;

}

extern "C" void LLVMLinkInInterpreter() { }

/// create - Create a new interpreter object.  This can never fail.
///
ExecutionEngine *Interpreter::create(Module *M, std::string* ErrStr) {
	// Tell this Module to materialize everything and release the GVMaterializer.
	if (M->MaterializeAllPermanently(ErrStr))
		// We got an error, just return 0
		return 0;

	return new Interpreter(M);
}


//===----------------------------------------------------------------------===//
// Interpreter ctor - Initialize stuff
//
Interpreter::Interpreter(Module *M)
	: ExecutionEngine(M), TD(M) {

		memset(&ExitValue.Untyped, 0, sizeof(ExitValue.Untyped));
		setTargetData(&TD);
		// Initialize the "backend"
		initializeExecutionEngine();
		initializeExternalFunctions();
		emitGlobals();

		Params::processInputFile();
		history = new History();
		rw_history = new RWHistory();
		currThread = Thread::getThreadByNumber(1);
		nextThreadNum = 2;
		ECStack = &threadStacks[currThread];
		counter = 0;
		int seed = time(0);
		timeval tv;
		gettimeofday(&tv,0);
		seed = tv.tv_usec;
		srand(seed);
#if defined(VIRTUALMEMORY)
		virtualizeGlobalVariables();
#else 
		physicalizeGlobalVariables();
#endif
		Mod = M;
		IL = new IntrinsicLowering(TD);

		/* initialized last instr info */
		instr_info.isBlocked = false;
}

Interpreter::~Interpreter() {
	delete IL;
	delete history;
	delete rw_history;
}

void Interpreter::runAtExitHandlers () {
	while (!AtExitHandlers.empty()) {
		callFunction(AtExitHandlers.back(), std::vector<GenericValue>());
		AtExitHandlers.pop_back();
		run();
	}
}

void Interpreter::getInvokeHistoryData(ExecutionContext& SF) {
	// initialize all used fields of history
	history->paramTypes.clear();
	history->intVals.clear();
	// traverse all parameters
	CallSite::arg_iterator it = SF.Caller.arg_begin();

	while (it != SF.Caller.arg_end()) {
		if ((*it)->getType()->isPointerTy()) {
				history->paramTypes.push_back(const_cast<Type*>((*it)->getType()));
				history->intVals.push_back((size_t)getOperandValue(*it, SF).PointerVal);
		}
		else if ((*it)->getType()->isIntegerTy()) {
				history->paramTypes.push_back(const_cast<Type*>((*it)->getType()));
				history->intVals.push_back(getOperandValue(*it, SF).IntVal.getLimitedValue());
		}
		++it;
	}
}

/// run - Start execution with the specified function and arguments.
///
GenericValue
Interpreter::runFunction(Function *F,
		const std::vector<GenericValue> &ArgValues) {
	ASSERT(F, "Function *F was null at entry to run()");

	// Try extra hard not to pass extra args to a function that isn't
	// expecting them.  C programmers frequently bend the rules and
	// declare main() with fewer parameters than it actually gets
	// passed, and the interpreter barfs if you pass a function more
	// parameters than it is declared to take. This does not attempt to
	// take into account gratuitous differences in declared types,
	// though.
	std::vector<GenericValue> ActualArgs;
	const unsigned ArgCount = F->getFunctionType()->getNumParams();
	for (unsigned i = 0; i < ArgCount; ++i) {
		ActualArgs.push_back(ArgValues[i]);
  }

	// Set up the function call.
	callFunction(F, ActualArgs);

	// Start executing the function.
	run();

	return ExitValue;
}

// createThread creates a new thread in the map and loads the initial function
void Interpreter::createThread(GenericValue functionToCall) {
	// first create the thread itself
	ECStack = &threadStacks[Thread::getThreadByNumber(nextThreadNum)];
	++nextThreadNum;

	// now iterate through function to see which is the desired one
	Module::iterator it;
	bool found = false;
	for(it=Mod->begin();it!=Mod->end();++it)
	{
		if(functionToCall.PointerVal == getPointerToFunction(it))
		{
			found = true;
			Function *fn = it;
			std::vector<GenericValue> v;
			//runFunction(fn,v); // run the function. That will generate the 
 			//initial exection context for the thread. The same as when 
		        //the initial thread calls main
			callFunction(fn,v);
			break;
		}
	}
	ASSERT(found, "function to be forked not found");
}

//  this is a debug function (use sparingly)
void Interpreter::dumpState(Instruction& I) {
	freopen("/tmp/log.txt","a t", stderr);

	errs() << "/////////////////////////////////////////////////////////\n";
	errs() << "S T A T E - i n f o r m a t i o n\n";

	errs() << "G L O B A L  V A R I A B L E S\n";
	errs() << "------------------------------\n";
	Module::global_iterator it;
	for(it=Mod->global_begin();it!=Mod->global_end();++it) {
		errs() << "Name: " << it->getName().str() << " and the Value is: " 
           << *(int*)getPointerToGlobal(it) << "\n";
	}

	errs() << "\n";
	errs() << "L I V I N G  T H R E A D S\n";
	errs() << "--------------------------\n";
	std::map<Thread, std::vector<ExecutionContext> >::iterator mit;
	for(mit=threadStacks.begin();mit!=threadStacks.end();++mit) {
		if(!mit->second.empty()) errs() << mit->first.tid() << " ";
		errs() << "\n";
	}
	errs() << "The thread to execute next is: " << currThread.tid() << "\n";
	errs() << "The next instruction to interpret is: " << I << "\n";

	for(mit=threadStacks.begin();mit!=threadStacks.end();++mit) { 
    if(!mit->second.empty()) {
			errs() << "Stack frame for thread: " << mit->first.tid() 
             << " at depth " << mit->second.size() << "\n";
		  errs() << "------------------------" << "-------" << "----------" 
             << "---------------" << "\n";

		  std::map<Value *,GenericValue>::iterator lit;
		  ExecutionContext &SF = mit->second.back();
		  GenericValue GV;
		  for(lit=SF.Values.begin();lit!=SF.Values.end();++lit) {
			  GV = getOperandValue(lit->first,SF);
			  if(lit->first->getType()->isPointerTy()) {
				  errs() << "Name: " << lit->first->getName().str() << " Type: " 
                 << lit->first->getType()->getDescription() << " Value: " 
                 << GV.PointerVal << "\n";
			  } else {
				  errs() << "Name: " << lit->first->getName().str() << " Type: " 
                 << lit->first->getType()->getDescription() << " Value: " 
                 << GV.IntVal.getLimitedValue() << "\n";
				}
		  }
		  errs() << "\n\n";
		}
		errs() << "\n\n\n";
	}
}

bool Interpreter::isAddressOnStack(void* mem, ExecutionContext& SF) {
	int i,j;
	size_t A,B,C;
	C = (size_t)mem;
	for(j=0;j<threadStacks[currThread].size();++j) {
		for(i=0;i<threadStacks[currThread][j].Allocas.size();++i) {
			A = (size_t)threadStacks[currThread][j].Allocas[i];
			B = threadStacks[currThread][j].Allocas.getBytesAt((void*)A);
			B = A+B;
			if(A <= C && C < B) return true;
		}
	}
	return false;
}

void Interpreter::freeAllocas(ExecutionContext& SF) {
	int i;
	for(i=0;i<SF.Allocas.size();++i) {
#if defined(VIRTUALMEMORY)
		void *virAddr = SF.Allocas.getAddress(i);
		void *natAddr = virtualToNative[virAddr];
		free(natAddr);
#else
		void *addr = SF.Allocas.getAddress(i);
		free(addr);
#endif
	}
}


bool Interpreter::isThreadBufferNonEmpty(Thread t) {

	if (Params::WMM == WMM_NONE)
		return false;
	else if (Params::WMM == WMM_TSO) {
		if (!thread_buffer_tso[t].empty()) return true;
			return false;
	}
	else if (Params::WMM == WMM_PSO) {
			std::map<GenericValue, std::list<GenericValue> >::iterator mit;
			for(mit = thread_buffer_pso[t].begin(); mit != thread_buffer_pso[t].end(); ++mit) {
				if(!mit->second.empty()) {
					return true;
				}
			}
			return false;
	}
	
	ASSERT(false, "Should be Unreachable");
}

bool Interpreter::isWorkingWithGlobalMemory(Instruction& I) const {
	return true;
}

#if defined(VIRTUALMEMORY)
void *Interpreter::getVirtualBaseAddressStack(void *addr, ExecutionContext& SF) {
	int i;
	size_t A,B,C;
	C = (size_t)addr;
	for(i=0;i<SF.Allocas.size();++i) {
		A = (size_t)SF.Allocas.getAddress(i);
		B = A + SF.Allocas.getBytesAt((void*)A);
		if(C>=A && C<B) return SF.Allocas.getAddress(i);
	}
	return NULL;
}

void *Interpreter::getNativeAddressFull(void *virAddr, ExecutionContext& SF) {
	if((size_t)virAddr==0)return (void*)0;
	void *virBase = getVirtualBaseAddressHeap(virAddr);
	if(virBase == NULL) {
		int i;
		for(i=0;i<threadStacks[currThread].size();++i) {
			if(virBase == NULL)virBase = getVirtualBaseAddressStack(virAddr,threadStacks[currThread][i]);
		}
	}
	if(virBase == NULL) {
		ASSERT(0, "getNativeAddressFull - memory corruption");
	}
	size_t offset = (size_t)virAddr - (size_t)virBase;
	void *natBase = virtualToNative[virBase];
	void *natAddr = (void*)( (size_t)natBase + offset );
	return natAddr;
}

void *Interpreter::getNativeAddressGlobal(void *virAddr) {
	if((size_t)virAddr==0)return (void*)0;
	void *virBase = getVirtualBaseAddressHeap(virAddr);
	if(virBase == NULL) {
		ASSERT(0, "getNativeAddressGlobal - memory corruption");
	}
	size_t offset = (size_t)virAddr - (size_t)virBase;
	void *natBase = virtualToNative[virBase];
	void *natAddr = (void*)( (size_t)natBase + offset );
	return natAddr;
}

void Interpreter::printMap() {
	++counter;
	std::map<void*,int>::iterator mit;
	printf("**************************************\n");
	for(mit=bytesAtVirtualAddress.begin(); mit!=bytesAtVirtualAddress.end(); ++mit) {
		printf("%d: virtual: %x native: %x bytes: %x\n", counter, (size_t)mit->first, (size_t)virtualToNative[mit->first], (size_t)mit->first+mit->second);
	}
}
#endif

