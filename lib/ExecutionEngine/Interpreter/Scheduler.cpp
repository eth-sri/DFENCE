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

#include "Scheduler.h"
#include "Params.h"

typedef vector<Thread> Threads;
bool belongsTo(Threads thds, Thread thd) {
	Threads::iterator it;
	for (it = thds.begin(); it < thds.end(); it++) {
		if (*it == thd) {
			return true;
		}
	}
	return false;
}

Thread pickUpNextThreadRR(Threads thds, Thread thd) {
	Threads::iterator it;
	for (it = thds.begin(); it < thds.end(); it++) {
		if (*it > thd) {
			break;
		}
	}

	if (it != thds.end()) {
		return *it;
	} else {
		return *(thds.begin());
	}
}

unsigned CSCounter = 0; 	   // context switch counter
unsigned PreemptiveCSCounter = 0; // preemptive context switch counter

Action Scheduler::selectAction(const Interpreter* interpreter) const {
	vector<Thread> enabled;
	enabled = interpreter->getAllActiveThreads();
	Action action;

	if (interpreter->instr_info.isBlocked) {
		return selectAction1(interpreter);	
	}
	
	if (!interpreter->instr_info.isSharedAccessing) {
		// this is not a shared memory accessing	
		action.type = SWITCH_THREAD;
		Thread curr = interpreter->getCurrThread();
		if (belongsTo(enabled, curr)) {
			action.thread = curr;
			return action;
		} else {
			return selectAction1(interpreter);
		}
	} else {	
		return selectAction1(interpreter);
	}
}

Action Scheduler::selectAction1(const Interpreter* interpreter) const {

	if (Params::Scheduler == RANDOM) {
		vector<Thread> enabled;
		Action action;
		// find all active threads
		enabled = interpreter->getAllActiveThreads();
		// decide what to do: switch thread or flush memory
		if (Params::WMM == WMM_NONE || ((double) rand()/RAND_MAX) > Params::flushProb) {  
			// switch thread
			action.type = SWITCH_THREAD;
			action.thread = enabled[rand() % enabled.size()];
		} else {  																																				
			// flush memory
			if (Params::WMM == WMM_NONE) {
				action.type = NO_ACTION;
			} else if (Params::WMM == WMM_TSO) {
				action.type = FLUSH_BUFFER;
				action.thread = enabled[rand() % enabled.size()];
			} else if (Params::WMM == WMM_PSO) {
	  		action.thread = enabled[rand() % enabled.size()];
		      	std::map<GenericValue, std::list<GenericValue> >::const_iterator mits, mite, mit;
	  		std::vector<GenericValue> possibleAddress;
	  		Thread ta = action.thread;
	  		std::map<Thread, std::map<GenericValue, std::list<GenericValue> > > to = 
          interpreter->thread_buffer_pso;

	  		mits = to[ta].begin();
	  		mite = to[ta].end();

	  		for (mit = mits; mit != mite; ++mit) {
					if(!mit->second.empty()) {
						possibleAddress.push_back(mit->first);
					}
      	}

	  		if (possibleAddress.size() != 0) {
					action.type = FLUSH_BUFFER;
		    	action.pso_var = possibleAddress[rand() % possibleAddress.size() ];
		 		} else {
					action.type = NO_ACTION;
		 		}
			}
		}
		return action;
	}

	else if (Params::Scheduler == DBRR) {
		static Thread thdIndex(-1);
		vector<Thread> enabled = interpreter->getAllActiveThreads();
		Action action;

		// round robin scheduling to pick up an available thread 
		thdIndex = pickUpNextThreadRR(enabled, thdIndex);

		// decide whether flush buffer or execute instruction
		action.thread = thdIndex;
		if (((double)rand()/RAND_MAX) > Params::flushProb) {   
		        // execute instruction
			action.type = SWITCH_THREAD;
		} 
		else {                                               
		        // flush memory
			if (Params::WMM == WMM_NONE) {
				action.type = NO_ACTION;
			} else if (Params::WMM == WMM_TSO) {
				action.type = FLUSH_BUFFER;
			} else if (Params::WMM == WMM_PSO) {
				ASSERT(0, "Cannot Handle PSO Model for this Scheduler!");		
				// currently, we focus on TSO; the implementation of PSO is empty for this scheduler.
			}
		}
		return action;
	}

	else if (Params::Scheduler == PREDICTIVE) {
		Action action;

		return action;
	}

	else {
		ASSERT(0, "Unrecognized type of Scheduler!");		
		Action action;
		action.type = NO_ACTION;
		return action;	
	}
}
