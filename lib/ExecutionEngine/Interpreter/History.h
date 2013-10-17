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

#ifndef LLI_HISTORY_H
#define LLI_HISTORY_H 		

#include "llvm/Function.h"
#include "llvm/ExecutionEngine/Thread.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Type.h"
#include "wsq.h"

#include <vector>
#include <list>

using namespace std;

namespace llvm {

typedef enum {CALL_FUNC, RETURN_FUNC, NONE} inst_type;

struct trace_elem {
	inst_type type; // Save whether it represents returning function, called function or both
	std::list<int> arg_vals;
	int ret_val;
	Function* func; // LLVM object representing the called/returning function
	Thread thread; //  The thread that executes the function
	bool operator<(const trace_elem& te) const {
 	 	return this->thread.tid() < te.thread.tid();
	}
};

class History {

	public: 
	 std::vector<trace_elem> trace_rec;
	private:
	 std::vector<int> recur_calls;
	public:
	 // needed data for recording of invokation
	 std::vector<Type*> paramTypes; // types of the parameters of the invoked function
	 std::vector<int> intVals; // values of integer parameters and casted to int values of pointer parameters
	public:
	 History();
	 void RecordFirstEvent();
	 void RecordInvokeEvent(Function*, Thread);
	 void RecordReturnEvent(const Type*&, GenericValue&, Function*&, Thread&);
   void printRecordedTrace();
	 void freeRecordedTrace();
};
}
#endif
