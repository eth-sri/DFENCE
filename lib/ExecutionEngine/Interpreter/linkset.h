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

#ifndef LINK_SET_H
#define LINK_SET_H

#include <set>

namespace llvm {

class LKS {
       private:
	std::set<int> q;
       public:
	int seq_linkset_add(int);
	int seq_linkset_contains(int);
	int seq_linkset_remove(int);
};
}
#endif
