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

#include "linkset.h"

using namespace llvm;

int LKS::seq_linkset_add(int task) {
	q.insert(task);
	return true;
}

int LKS::seq_linkset_contains(int task) {
	if (q.find(task) != q.end())
		return true;
	else 
		return false;
}

int LKS::seq_linkset_remove(int task) {
	if (q.find(task) != q.end()) {
		q.erase(task);
		return true;
	}
	else 
		return false;
}
