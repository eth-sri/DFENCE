//===-- Interpreter.h ------------------------------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header file defines the interpreter structure
//
//===----------------------------------------------------------------------===//

#ifndef LLI_INTERPRETER_H
#define LLI_INTERPRETER_H

#include "llvm/Function.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Support/CallSite.h"
#include "llvm/System/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/InstVisitor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ExecutionEngine/Thread.h"
#include "llvm/ExecutionEngine/ThreadKey.h"
#include "History.h"
#include "RWHistory.h"
#include "Action.h"
#include <string>
#include <vector>
#include <fstream>
#include <list>
#include <iostream>
#include <map>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <set>
#include <algorithm>
#include <deque>

#define DEBUG_PRINT

#define CAS32 0
#define CASIO 1

namespace llvm {

	class IntrinsicLowering;
	struct FunctionInfo;
	template<typename T> class generic_gep_type_iterator;
	class ConstantExpr;
	typedef generic_gep_type_iterator<User::const_op_iterator> gep_type_iterator;


	// AllocaHolder - Object to track all of the blocks of memory allocated by
	// alloca.  When the function returns, this object is popped off the execution
	// stack, which causes the dtor to be run, which frees all the alloca'd memory.
	//
	class AllocaHolder {
		friend class AllocaHolderHandle;
		std::vector<void*> Allocations;
		unsigned RefCnt;
		public:
		AllocaHolder() : RefCnt(0) {}
		void add(void *mem) {
			Allocations.push_back(mem);
		}
		// Change the destructor not to free memory
		// there is no way that the AllocaHolder could know what the native addresses are
		// so it relies that the memory is already freed by somebody else
		// in the case it is popStackAndReturn
		~AllocaHolder() {
			for (unsigned i = 0; i < Allocations.size(); ++i) {
				//free(Allocations[i]);
			}
		}
		//  map to store how many bytes are allocated for every address on the stack
		std::map<void *, int> bytesAllocated;
		//  method to insert not only the address but and the number of bytes allocated
		void addWithSize(void *mem, int numBytes) {
			Allocations.push_back(mem);
			bytesAllocated[mem] = numBytes;
		}
	};

	// AllocaHolderHandle gives AllocaHolder value semantics so we can stick it into
	// a vector...
	//
	class AllocaHolderHandle {
		AllocaHolder *H;
		public:
		AllocaHolderHandle() : H(new AllocaHolder()) { H->RefCnt++; }
		AllocaHolderHandle(const AllocaHolderHandle &AH) : H(AH.H) { H->RefCnt++; }
		~AllocaHolderHandle() { if (--H->RefCnt == 0) delete H; }

		void add(void *mem) { H->add(mem); }
		//  method to get element by index
		void * operator[] (int i) {
			ASSERT( i>=0 && i < (int)H->Allocations.size(), "index for allocas is out of bounds\n");
			return H->Allocations[i];
		}
		//  method to get the size of the vector
		int size() {
			return H->Allocations.size();
		}
		//  method to insert not only the address but and the number of bytes allocated
		void addWithSize(void *mem, int numBytes) {
			H -> addWithSize(mem,numBytes);
		}
		int getBytesAt(void *mem) {
			return H -> bytesAllocated[mem];
		}
		void *getAddress(int i) {
			ASSERT(i>=0 && i<(int)H->Allocations.size(), "trying to get allocation address with index out-of-bounds");
			return H -> Allocations[i];
		}
	};

	typedef std::vector<GenericValue> ValuePlaneTy;

	// ExecutionContext struct - This struct represents one stack frame currently
	// executing.
	//
	struct ExecutionContext {
		Function             *CurFunction;// The currently executing function
		BasicBlock           *CurBB;      // The currently executing BB
		BasicBlock::iterator  CurInst;    // The next instruction to execute
		std::map<Value *, GenericValue> Values; // LLVM values used in this invocation
		std::vector<GenericValue>  VarArgs; // Values passed through an ellipsis
		CallSite             Caller;     // Holds the call that called subframes.
		// NULL if main func or debugger invoked fn
		AllocaHolderHandle    Allocas;    // Track memory allocated by alloca
	};


	// Interpreter - This class represents the entirety of the interpreter.
	//
	class Interpreter : public ExecutionEngine, public InstVisitor<Interpreter> {
		Module *Mod;
		GenericValue ExitValue;          // The return value of the called function
		TargetData TD;
		IntrinsicLowering *IL;

		// The runtime stack of executing code.  The top of the stack is the current
		// function record.
		//  added the star to make it pointer to the real stack, 
		// for the current thread
		// that way less LLVM code is changed
		std::vector<ExecutionContext> *ECStack;

		// *********************************************************************
		// support for threads
		//  map for stacks for every thread. ECStack point to one of these vectors
		//  to the one that is executing the next isntruction
		std::map< Thread, std::vector<ExecutionContext> > threadStacks;
		//  keys of every thread
		std::map<std::pair<Thread, char*>, ThreadKey> threadKeys;
		//  the number of the next thread that will eventually be created
	  int nextThreadNum;
		//  the current thread, i.e. the thread that will execute the next operation
		Thread currThread;

		//  here, we added an object of type History
		History* history;
		RWHistory* rw_history;

		// information for the scheduler
		public:
		int ExitStatus;
		bool toFix;
		bool segmentFaultFlag;
		bool allonAssertExist;
		bool runMain;
		Thread getCurrThread() const {
			return currThread;
		}

		typedef struct {
			bool isBlocked;
			bool isWriteOrRead; // false-write; true-read
			bool isSharedAccessing;	
			size_t addr;
			int width;
		} last_instr_info;
		last_instr_info instr_info;

		private:
		typedef struct {
		  GenericValue pointer;
		  GenericValue value;
		  Type* type;
		} tso_buff_elem;

		std::map<GenericValue, Type*> pso_types;
		
		public: // public because of class Scheduler
		std::map<Thread, std::list<tso_buff_elem> > thread_buffer_tso;
		std::map<Thread, std::map<GenericValue, std::list<GenericValue> > > thread_buffer_pso;
		
		private:
		void flush_buffer_pso(Thread, GenericValue);
		void flush_buffer_tso(Thread);
		void membar_ss(Thread);
		void membar_sl(Thread);
		// List of functions to call when the program exits, 
		// registered with the atexit() library function.
		std::vector<Function*> AtExitHandlers;

		public:
		explicit Interpreter(Module *M);
		~Interpreter();

		/// runAtExitHandlers - Run any functions registered by the program's calls to
		/// atexit(3), which we intercept and store in AtExitHandlers.
		///
		void runAtExitHandlers();

		static void Register() {
			InterpCtor = create;
		}

		/// create - Create an interpreter ExecutionEngine. This can never fail.
		///
		static ExecutionEngine *create(Module *M, std::string *ErrorStr = 0);

		/// run - Start execution with the specified function and arguments.
		///
		virtual GenericValue runFunction(Function *F,
				const std::vector<GenericValue> &ArgValues);

		/// recompileAndRelinkFunction - For the interpreter, functions are always
		/// up-to-date.
		///
		virtual void *recompileAndRelinkFunction(Function *F) {
			return getPointerToFunction(F);
		}

		/// freeMachineCodeForFunction - The interpreter does not generate any code.
		///
		void freeMachineCodeForFunction(Function *F) { }

		// Methods used to execute code:
		// Place a call on the stack
		void callFunction(Function *F, const std::vector<GenericValue> &ArgVals);
		void run();                // Execute instructions until nothing left to do

		// Opcode Implementations
		void visitReturnInst(ReturnInst &I);
		void visitBranchInst(BranchInst &I);
		void visitSwitchInst(SwitchInst &I);
		void visitIndirectBrInst(IndirectBrInst &I);

		void visitBinaryOperator(BinaryOperator &I);
		void visitICmpInst(ICmpInst &I);
		void visitFCmpInst(FCmpInst &I);
		void visitAllocaInst(AllocaInst &I);

		void visitLoadInstNoWmm(LoadInst &I);
		void visitLoadInstTSO(LoadInst &I);
		void visitLoadInstPSO(LoadInst &I);
		void visitLoadInst(LoadInst &I);

		void visitStoreInstNoWmm(StoreInst &I);
		void visitStoreInstTSO(StoreInst &I);
		void visitStoreInstPSO(StoreInst &I);
		void visitStoreInst(StoreInst &I);

		void visitGetElementPtrInst(GetElementPtrInst &I);
		void visitPHINode(PHINode &PN) {
			llvm_unreachable("PHI nodes already handled!");
		}
		void visitTruncInst(TruncInst &I);
		void visitZExtInst(ZExtInst &I);
		void visitSExtInst(SExtInst &I);
		void visitFPTruncInst(FPTruncInst &I);
		void visitFPExtInst(FPExtInst &I);
		void visitUIToFPInst(UIToFPInst &I);
		void visitSIToFPInst(SIToFPInst &I);
		void visitFPToUIInst(FPToUIInst &I);
		void visitFPToSIInst(FPToSIInst &I);
		void visitPtrToIntInst(PtrToIntInst &I);
		void visitIntToPtrInst(IntToPtrInst &I);
		void visitBitCastInst(BitCastInst &I);
		void visitSelectInst(SelectInst &I);


		void visitCallSite(CallSite CS);
		void visitCallInst(CallInst &I) { visitCallSite (CallSite (&I)); }
		void visitInvokeInst(InvokeInst &I) { visitCallSite (CallSite (&I)); }
		void visitUnwindInst(UnwindInst &I);
		void visitUnreachableInst(UnreachableInst &I);

		void visitShl(BinaryOperator &I);
		void visitLShr(BinaryOperator &I);
		void visitAShr(BinaryOperator &I);

		void visitVAArgInst(VAArgInst &I);
		void visitInstruction(Instruction &I) {
			errs() << I;
			llvm_unreachable("Instruction not interpretable yet!");
		}

		void visitSpawnThread(ExecutionContext &SF);
		void visitAssert(ExecutionContext &SF);
		void visitAssertExist(ExecutionContext &SF);
		void visitJoinAll(ExecutionContext &SF);
		void visitCAS(ExecutionContext &SF, int inst);
		void visitCASPO(ExecutionContext &SF);
		void visitFASIO(ExecutionContext &SF);
		void visitFASPO(ExecutionContext &SF);
		void visitMalloc(ExecutionContext &SF);
		void visitFree(ExecutionContext &SF);
		void visitMemset(ExecutionContext &SF);
		void visitMemcpy(ExecutionContext &SF, CallSite CS);
		void visitNprintString(ExecutionContext &SF);
		void visitNprintInt(ExecutionContext &SF);
		void visitGetEnv(ExecutionContext &SF);
		void visitRand(ExecutionContext &SF);
		void visitSysConf(ExecutionContext &SF);
		void visitMmap(ExecutionContext &SF);
		void visitMunmap(ExecutionContext &SF);
		void visitPthreadSelf(ExecutionContext &SF);
		void visitKeyCreate(ExecutionContext &SF);
		void visitKeyGetSpecific(ExecutionContext &SF);
		void visitKeySetSpecific(ExecutionContext &SF);

		GenericValue callExternalFunction(Function *F,
				const std::vector<GenericValue> &ArgVals);
		void exitCalled(GenericValue GV);

		void addAtExitHandler(Function *F) {
			AtExitHandlers.push_back(F);
		}

		GenericValue *getFirstVarArg () {
			return &(ECStack->back ().VarArgs[0]);
		}

		public:
		GenericValue executeGEPOperation(Value *Ptr, gep_type_iterator I,
				gep_type_iterator E, ExecutionContext &SF);

		// invoke this function before getting history of a trace
		// getOperandValue() etc. are private for Interpreter 
		// so this function is here

		void getInvokeHistoryData(ExecutionContext&); 		
		// get a list of all threads that have something left to execute		
		vector<Thread> getAllActiveThreads() const;		  
		// flushes all buffers that have something to left to flush (execute at the end of program)
		void flushAll();

		private:  // Helper functions
		// SwitchToNewBasicBlock - Start execution in a new basic block and run any
		// PHI nodes in the top of the block.  This is used for intraprocedural
		// control flow.
		//
		void SwitchToNewBasicBlock(BasicBlock *Dest, ExecutionContext &SF);

		void *getPointerToFunction(Function *F) { return (void*)F; }
		void *getPointerToBasicBlock(BasicBlock *BB) { return (void*)BB; }

		void initializeExecutionEngine() { }
		void initializeExternalFunctions();
		GenericValue getConstantExprValue(ConstantExpr *CE, ExecutionContext &SF);
		GenericValue getOperandValue(Value *V, ExecutionContext &SF);
		GenericValue executeTruncInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeSExtInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeZExtInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeFPTruncInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeFPExtInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeFPToUIInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeFPToSIInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeUIToFPInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeSIToFPInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executePtrToIntInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeIntToPtrInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeBitCastInst(Value *SrcVal, const Type *DstTy,
				ExecutionContext &SF);
		GenericValue executeCastOperation(Instruction::CastOps opcode, Value *SrcVal,
				const Type *Ty, ExecutionContext &SF);
		void popStackAndReturnValueToCaller(const Type *RetTy, GenericValue Result);

		// functions added:
		//
		// create new thread for spawnthread, the GenericValue keeps the address of the function that the new thread will execute
		void createThread(GenericValue);
		// dump state dumps the current state. Not usable.
		void dumpState(Instruction&);
		// checks if a address is on the stack
		// works both with virtual memory and without
		// if virtual memory is used then the virtual address must be given
		bool isAddressOnStack(void *mem, ExecutionContext& SF);
		// frees the memory contained by a stack frame just before it is destroyed
		// it is implemented bacause Allocas can't know the native addresses
		void freeAllocas(ExecutionContext&);
		// isWorkingWithGlobalMemory decides if a function is working with the global memory; load or store
		bool isWorkingWithGlobalMemory(Instruction&) const;
		bool isThreadBufferNonEmpty(Thread);
		void processFile();
#if defined(VIRTUALMEMORY)
		// this function given a virtual pointer returns the virtual base address in the given stack frame
		// that points to the memory that the given pointer is pointing to.
		// returns NULL if it cannot be mapped with base address on the given stack frame
		void *getVirtualBaseAddressStack(void *,ExecutionContext&);
		// getNativeAddressFull - returns the native address for a given virtual address and ExecutionContext
		// it works for all addresses, not only for base addresses
		// returns NULL if it cannot map the address with address in the heap + Global Variables or in the given stack frame
		void *getNativeAddressFull(void *, ExecutionContext&);
		// getNativeAddressGlobal is the same as getNativeAddressFull but only for global variables without giving ExecutionContext
		void *getNativeAddressGlobal(void*);
		void printMap();
#endif
		/*
		// loadValue is like visitLoadInst but instead instruction in receives the address and returns the value
		GenericValue loadValueNoWmm(void*, const Type*);
		GenericValue loadValueTSO(void*, const Type*);
		GenericValue loadValuePSO(void*, const Type*);
		*/
	};

} // End llvm namespace

#endif
