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

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "Interpreter.h"
#include "Action.h"

using namespace llvm;

class Scheduler {
	public:
	 Action selectAction(const Interpreter*) const;
	 Action selectAction1(const Interpreter*) const;
};

#endif
