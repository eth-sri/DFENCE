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

#ifndef LLI_LFMALLOC_H
#define LLI_LFMALLOC_H

#include <list>
#include <deque>

namespace llvm {

class LFMALLOC {
public:
	std::list<std::pair<int, size_t> > alloc_list;
};
}
#endif
