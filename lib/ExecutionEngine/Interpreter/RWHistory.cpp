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

#include "RWHistory.h"
#include "Params.h"
#include "llvm/ExecutionEngine/Thread.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Value.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <map>
#include <set>

using std::cout;
using namespace llvm;

// parameter type tells us whether we are recording read or write (true for read and false for write)
void RWHistory::RecordRWEvent(GenericValue ptr, GenericValue val,Thread thr, RWType type, int label) {
	assert((type == READ || type == WRITE) && "A wrong RECORD function is called!");
	rwtrace_elem elem;
	
	elem.location = (int*)ptr.PointerVal;
	elem.value = val.IntVal.getLimitedValue();
	elem.thr = thr;
	elem.type = type;
	elem.label = label;

	rwtrace_rec.push_back(elem);
}

void RWHistory::RecordRWEvent(Thread thr, RWType type, int label) {
	assert((type == FLUSH_FENCE || type == FLUSH_CAS_TSO || 
					type == FLUSH_INSTR || type == FLUSH_RANDOM_TSO ||
				 	type == SPAWN || type == JOIN) && 
				 "A wrong RECORED function is called!");

	rwtrace_elem elem;
	
	elem.thr = thr;
	elem.type = type;
	elem.label = label;

	rwtrace_rec.push_back(elem);
}

void RWHistory::RecordRWEvent(GenericValue ptr, Thread thr, RWType type, int label) {
	assert((type == FLUSH_CAS_PSO || type == FLUSH_RANDOM_PSO) && 
         "A wrong RECORED function is called!");

	rwtrace_elem elem;

	elem.location = (int*)ptr.PointerVal;	
	elem.thr = thr;
	elem.type = type;
	elem.label = label;

	rwtrace_rec.push_back(elem);
}

void RWHistory::FindSharedRW() {

	if (Params::logging) {
		map<int*, vector<int> > m; // index vector per memory location
		map<int*, set<int> > t; // thread per memory location
	
		bool* index = new bool[rwtrace_rec.size()];
		bool* sharedIndex = new bool[rwtrace_rec.size()];
		bool hasSpawn = false;
		bool hasJoin = false;
		for (unsigned i = 0; i < rwtrace_rec.size(); i++) {
			/* to filter the accesses that are not between spawn and join */
			if (rwtrace_rec[i].type == SPAWN) {
				hasSpawn = true;
				hasJoin = false;
			}

			if (rwtrace_rec[i].type == JOIN) {
				hasSpawn = false;
				hasJoin = true;
			}

			if (hasSpawn && !hasJoin) {
				sharedIndex[i] = true;
			} else {
				sharedIndex[i] = false;
			}
			
			/* to find out which locations have been shared
 			 * However, if the location is accessed by two threads in 
 			 * different spawn and join regions, it is considered as 
 			 * shared */
			index[i] = false; // set to false, and some will be reset to true
			if (rwtrace_rec[i].type == FLUSH_RANDOM_TSO || 
					rwtrace_rec[i].type == FLUSH_RANDOM_PSO || 
					rwtrace_rec[i].type == FLUSH_FENCE || 
					rwtrace_rec[i].type == FLUSH_INSTR || 
          rwtrace_rec[i].type == FLUSH_CAS_TSO ||
          rwtrace_rec[i].type == FLUSH_CAS_PSO) {
				index[i] = true;
			} else if (rwtrace_rec[i].type == SPAWN || 
								 rwtrace_rec[i].type == JOIN) {
				index[i] = false;
			} else if (rwtrace_rec[i].type == WRITE ||
								 rwtrace_rec[i].type == READ){
				m[rwtrace_rec[i].location].push_back(i);
				t[rwtrace_rec[i].location].insert(rwtrace_rec[i].thr.tid());
			} else {
				assert(0 && "Unrecognized shared type!");
			}
		}
	
		// the problem with this algorithm is that it over-claims the sharing
		for (map<int*, set<int> >::iterator iter = t.begin(); 
			iter != t.end(); ++iter) { // for each location
			if (iter->second.size() > 0) { // set to 0, because I think all variables are shared.  
				for (vector<int>::iterator it = m[iter->first].begin(); 
          it != m[iter->first].end(); ++it) { // then set index at that location
					index[*it] = true;
				}
			}
		}

		for (unsigned i = 0; i < rwtrace_rec.size(); i++) {
			if (index[i] && sharedIndex[i]) {
				shared_rec.push_back(rwtrace_rec[i]);
			}
		}
		delete[] sharedIndex; 
		delete[] index;
	}
}

void RWHistory::PrintSharedRW() {
#define PRINT_DEBUG
#ifdef PRINT_DEBUG
#define cout dbgs()
#endif
	cout << "RECORDED SHARED READs AND WRITEs" << "\n";
	for (unsigned i = 0; i < shared_rec.size(); i++) {
		if (shared_rec[i].type == READ) {
			cout << "READ at " << shared_rec[i].location 
            << " of value " << shared_rec[i].value 
            << " by thread " << shared_rec[i].thr.tid()
            << " with label " << shared_rec[i].label << "\n";
		} else if (shared_rec[i].type == WRITE) {
			cout << "WRITE at " << shared_rec[i].location 
            << " of value " << shared_rec[i].value 
            << " by thread " << shared_rec[i].thr.tid()
            << " with label " << shared_rec[i].label << "\n";
		} else if (shared_rec[i].type == FLUSH_CAS_TSO) {
			cout << "Flush CAS_TSO ----------------- " 
            << " by thread " << shared_rec[i].thr.tid()
            << " with label " << shared_rec[i].label << "\n";
		} else if (shared_rec[i].type == FLUSH_CAS_PSO) {
			cout << "Flush CAS_PSO ----------------- " 
            << " by thread " << shared_rec[i].thr.tid()
            << " with label " << shared_rec[i].label << "\n";
		} else if (shared_rec[i].type == FLUSH_INSTR) {
			cout << "Flush INSTR --------------- " 
            << " by thread " << shared_rec[i].thr.tid()
            << " with label " << shared_rec[i].label << "\n";
		} else if (shared_rec[i].type == FLUSH_FENCE) {
			cout << "Flush FENCE---------------- " 
            << " by thread " << shared_rec[i].thr.tid()
            << " with label " << shared_rec[i].label << "\n";
		} else if (shared_rec[i].type == FLUSH_RANDOM_TSO) {
			cout << "Flush RANDOM TSO---------------- " 
            << " by thread " << shared_rec[i].thr.tid()
            << " with label " << shared_rec[i].label << "\n";
		} else if (shared_rec[i].type == FLUSH_RANDOM_PSO) {
			cout << "Flush RANDOM PSO---------------- " 
            << " by thread " << shared_rec[i].thr.tid()
            << " with label " << shared_rec[i].label << "\n";
		} else {
			assert(0 && "Unrecognized shared type!");
		}
	}
	cout << "END OF RECORDED SHARED READs AND WRITEs" << "\n";
#ifdef PRINT_DEBUG
#undef cout
#endif
}

