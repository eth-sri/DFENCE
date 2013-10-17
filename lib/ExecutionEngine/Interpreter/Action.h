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

#ifndef ACTION_H
#define ACTION_H

#include "Interpreter.h"

enum ActionType {
	SWITCH_THREAD,
	FLUSH_BUFFER,
	NO_ACTION
};

class Action {
   public:
 	   enum ActionType type; 	// the type of action that have to be done
	   Thread thread; 		// the thread that will be scheduled or whose buffer will be flushed for TSO
	   GenericValue pso_var; 	// the variable of whose buffer will be flushed for the given thread under PSO
};

#endif
