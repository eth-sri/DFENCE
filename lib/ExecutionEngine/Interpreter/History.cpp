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

#include "History.h"
#include "Params.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"

#include <vector>

using namespace llvm;

History::History()
{
	recur_calls.push_back(0);
	recur_calls.push_back(0);
}

void History::RecordFirstEvent()
{
	if (Params::recTrace()) {
		recur_calls.push_back(0);
	}
}

void History::RecordInvokeEvent(Function* currFunction, Thread currThread)
{
	if (Params::recTrace()) {
		if (Params::funcs_rec.find(currFunction->getName().str()) != Params::funcs_rec.end() && recur_calls[currThread.tid()] == 0) {
			trace_elem elem;
			elem.type = CALL_FUNC;
			elem.func = currFunction;
			elem.thread = currThread;
			for (unsigned int it = 0; it < paramTypes.size(); it++) {

				if (paramTypes[it]->isPointerTy() || paramTypes[it]->isIntegerTy()) {
					// here the code is made program independent.
					// if we have pointer then just care about its address but not what it points to.
					// in getInvokeHistoryData() we record the values of int parameters and casted to int values of pointers
					elem.arg_vals.push_back(intVals[it]);
				}
				else {
					std::cout << "WARNING: Argument with non-int and non-pointer type given to function!" << std::endl;
					elem.arg_vals.push_back(0);
				}
			}
			trace_rec.push_back(elem);
		}
		if (Params::funcs_rec.find(currFunction->getName().str()) != Params::funcs_rec.end()) {
			recur_calls[currThread.tid()]++;
		}
	}
}

void History::RecordReturnEvent(const Type*& RetTy, GenericValue& Result, Function*& currFunction, Thread& currThread)
{
  if (Params::recTrace()) {
	trace_elem elem;

	if (Params::funcs_rec.find(currFunction->getName().str()) != Params::funcs_rec.end() && recur_calls[currThread.tid()] == 1) {
		elem.type = RETURN_FUNC;
		if (RetTy->isPointerTy() && Result.PointerVal != NULL) {
			elem.ret_val = (size_t)Result.PointerVal;
		}
		else if (RetTy->isIntegerTy()) {
			elem.ret_val = Result.IntVal.getLimitedValue();
		}
		else {
			std::cout << "WARNING: Result with non-int and non-pointer type returned by function!" << std::endl;
			elem.ret_val = 0;
		}
		elem.func = currFunction;
		elem.thread = currThread;
		trace_rec.push_back(elem);
	}
	if (Params::funcs_rec.find(currFunction->getName().str()) != Params::funcs_rec.end()) {
		recur_calls[currThread.tid()]--;
	}
  }
}

void History::printRecordedTrace()
{
	if(Params::recTrace()) {
		std::cout << "RECORDED TRACE" << std::endl;
		for (std::vector<trace_elem>::const_iterator ci = trace_rec.begin(); 
			ci != trace_rec.end(); ++ci) {
			if ((*ci).type == CALL_FUNC) {
				std::cout << "call of " << (*ci).func->getName().str() 
					<< " on thread " << (*ci).thread.tid() << ": ";
        for (std::list<int>::const_iterator cit = ci->arg_vals.begin(); 
					cit != ci->arg_vals.end(); cit++) {
					if (Params::programToCheck == LF_MALLOC) {
						std::cout << (unsigned)*cit << " "; 
					} else {
						std::cout << *cit << " "; 
					}
				} 
				std::cout << std::endl;
			} else {
				std::cout << "return of " << (*ci).func->getName().str() 
					<< " on thread " << (*ci).thread.tid() << ": ";
					if (Params::programToCheck == LF_MALLOC) {
						std::cout << (unsigned)(*ci).ret_val << std::endl;
					} else {
						std::cout << (*ci).ret_val << std::endl;
					}
			}
		}
		std::cout << "END OF RECORDED TRACE" << std::endl;
	}
}

