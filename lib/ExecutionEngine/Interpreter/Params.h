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

#ifndef LLI_PARAMS_H
#define LLI_PARAMS_H

#include "llvm/Support/raw_ostream.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include <string>
#include <fstream>
#include <iostream>
#include <set>
#include <cstdlib>

using namespace std;

namespace llvm {

typedef enum {NO_PROGRAM, WSQ_CHASE, WSQ_LIFO, WSQ_FIFO, WSQ_THE, WSQ_ANCHOR, 
							LF_MALLOC, SKIP_LIST,
							QUEUE, DEQUE, LINKSET} program_type;
typedef enum {RANDOM, DBRR, PREDICTIVE} scheduler_type;

#define CONFDIR		"CONFDIR"
#define FLUSHPROB	"FLUSHPROB"
#define CONFILE		"conf.txt"
#define WSQFILE		"wsq.txt"
#define MALLOCFILE	"malloc.txt"
#define SKIPFILE	"skip.txt"
#define QUEUEFILE "queue.txt"
#define DEQUEFILE "deque.txt"
#define LINKSETFILE "linkset.txt"

#define PROP_NONE	0
#define PROP_SC		1
#define PROP_LIN	2

#define WMM_NONE	0
#define WMM_TSO		1
#define WMM_PSO		2

#define TRACES_PER_ROUND 20

class Params {

public:
	void static processInputFile();
	bool static recTrace() { if (Property == PROP_SC || Property == PROP_LIN) return true; else return false; };
	static double flushProb;
	static int Property;
	static int WMM;
	static int Scheduler;
	static std::set<std::string> funcs_rec;
	static program_type programToCheck;
	static bool logging;
};
}
#endif
