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

#ifndef LLI_CHECKTRACE_H
#define LLI_CHECKTRACE_H

#include "History.h"

#include <vector>

using namespace std;

namespace llvm {

class CheckTrace {

public:
	int static checkHistory(History*, int);
private:
	static vector<int> thread_perm;
	static vector<int> curr_perm;
	static vector<pair<pair<int, int>, trace_elem > > init_perm;
	static void gen_init_sc_perm(History* history, int nextThreadNum);
  static bool gen_next_sc_perm(int nextThreadNum);
	static void free_init_sc_perm();
	static bool sc_check(History*, int);
	static bool lin_check(History*, int);
	static bool checkWSQ();
	static bool checkMalloc();
	static bool checkQueue();
	static bool checkDeque();
	static bool checkLinkSet();
	static bool checkPermutation();
	static bool isRealTimeOrderPreserved();
	static bool check(History*, int, bool);
	static void printPerm();
};
}
#endif
