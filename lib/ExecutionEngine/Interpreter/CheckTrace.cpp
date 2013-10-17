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

#include "CheckTrace.h"
#include "Params.h"
#include "wsq.h"
#include "linkset.h"
#include "SpecMalloc.h"
#include "llvm/ExecutionEngine/Thread.h"

#include <vector>
#include <map>
#include <algorithm>

using namespace llvm;
using namespace std;

vector<int> CheckTrace::thread_perm;
vector<int> CheckTrace::curr_perm;
vector<pair<pair<int, int>, trace_elem > > CheckTrace::init_perm;

void CheckTrace::gen_init_sc_perm(History* history, int nextThreadNum) {
	std::map<Thread, int> calls;
	trace_elem elem;

	for (unsigned i = 0; i < history->trace_rec.size(); i++) {
		if (history->trace_rec[i].type == CALL_FUNC) {
			calls[history->trace_rec[i].thread] = i;
		}
		else if (history->trace_rec[i].type == RETURN_FUNC) {
			elem.type = NONE;
			elem.func = history->trace_rec[i].func;
			elem.thread = history->trace_rec[i].thread;
			elem.ret_val = history->trace_rec[i].ret_val;
			elem.arg_vals.clear();
			int call_index = calls[history->trace_rec[i].thread];
			calls.erase(calls.find(history->trace_rec[i].thread));
			for (std::list<int>::iterator cit = history->trace_rec[call_index].arg_vals.begin(); 
        cit != history->trace_rec[call_index].arg_vals.end(); ++cit) {
				elem.arg_vals.push_back(*cit);
			}
			init_perm.push_back(std::make_pair(std::make_pair(call_index , i), elem));
		}
	}

	unsigned* index = (unsigned*)malloc((nextThreadNum + 1) * sizeof(unsigned));
	for (std::vector<std::pair<std::pair<int, int>, trace_elem > >::const_iterator ci = init_perm.begin(); 
    ci != init_perm.end(); ++ci) {
		thread_perm.push_back((*ci).second.thread.tid());
		curr_perm.push_back(0);
	}
	for (int i = 0; i <= nextThreadNum; i++) {
		index[i] = 0;
	}
	std::sort(thread_perm.begin(), thread_perm.end());

	for (unsigned i = 0; i < thread_perm.size(); i++) {
		for (unsigned j = index[thread_perm[i]]; j < init_perm.size(); j++) {
			if (init_perm[j].second.thread.tid() == thread_perm[i]) {
				curr_perm[i] = j;
				index[thread_perm[i]] = j + 1;
				break;
			}
		}
	}
	free(index);
}

// Added nextThreadNum as parameter. nextThreadNum is the field from Interpreter
bool CheckTrace::gen_next_sc_perm(int nextThreadNum) {

	if (!next_permutation(thread_perm.begin(), thread_perm.end())) {
		return false;
	}
	unsigned* index = (unsigned*)malloc((nextThreadNum + 1) * sizeof(unsigned));
	for (int i = 0; i < nextThreadNum; i++) {
		index[i] = 0;
	}

	for (unsigned i = 0; i < thread_perm.size(); i++) {
		for (unsigned j = index[thread_perm[i]]; j < init_perm.size(); j++) {
			if (init_perm[j].second.thread.tid() == thread_perm[i]) {
				curr_perm[i] = j;
				index[thread_perm[i]] = j + 1;
				break;
			}
		}
	}
	free(index);
	return true;
}

void CheckTrace::free_init_sc_perm() {
	init_perm.clear();
	thread_perm.clear();
	curr_perm.clear();
}

bool CheckTrace::checkWSQ() {

	WSQ wsq;
	int task;

	for (unsigned i = 0; i < curr_perm.size(); i++) {

		if (init_perm[curr_perm[i]].second.func->getName().str() == "wsq_put") {
			task = init_perm[curr_perm[i]].second.arg_vals.back();
			if (Params::programToCheck == WSQ_CHASE)
				wsq.seq_wsq_put_chase(task);
			else if(Params::programToCheck == WSQ_LIFO)
				wsq.seq_wsq_put_lifo(task);
			else if(Params::programToCheck == WSQ_FIFO)
				wsq.seq_wsq_put_fifo(task);
			else if(Params::programToCheck == WSQ_THE)
				wsq.seq_wsq_put_the(task);
			else if(Params::programToCheck == WSQ_ANCHOR)
				wsq.seq_wsq_put_anchor(task);

			if (init_perm[curr_perm[i]].second.ret_val != 1) {
//				std::cout << "Failed! wsq_put: " <<
//					init_perm[curr_perm[i]].second.ret_val << " spec: 1" << std::endl;
				return false;
			}
		}
		else if (init_perm[curr_perm[i]].second.func->getName().str() == "wsq_take") {

			if(Params::programToCheck == WSQ_CHASE)
				task = wsq.seq_wsq_take_chase();
			else if(Params::programToCheck == WSQ_LIFO)
				task = wsq.seq_wsq_take_lifo();
			else if(Params::programToCheck == WSQ_FIFO)
				task = wsq.seq_wsq_take_fifo();
			else if(Params::programToCheck == WSQ_THE)
				task = wsq.seq_wsq_take_the();
			else if(Params::programToCheck == WSQ_ANCHOR)
				task = wsq.seq_wsq_take_anchor();

			if (task != init_perm[curr_perm[i]].second.ret_val) {
//				std::cout << "Failed! wsq_take: " <<
//				              init_perm[curr_perm[i]].second.ret_val << " spec: " << task << std::endl;
				return false;
			}
		  }
		  else if ((init_perm[curr_perm[i]].second.func->getName().str() == "wsq_steal")) {

			if(Params::programToCheck == WSQ_CHASE)
				task = wsq.seq_wsq_steal_chase();
			else if(Params::programToCheck == WSQ_LIFO)
				task = wsq.seq_wsq_steal_lifo();
			else if(Params::programToCheck == WSQ_FIFO)
				task = wsq.seq_wsq_steal_fifo();
			else if(Params::programToCheck == WSQ_THE)
				task = wsq.seq_wsq_steal_the();
			else if(Params::programToCheck == WSQ_ANCHOR)
				task = wsq.seq_wsq_steal_anchor();

			if (task != init_perm[curr_perm[i]].second.ret_val) {
//				std::cout << "Failed! wsq_steal: " <<
//					              init_perm[curr_perm[i]].second.ret_val << "spec: " << task << std::endl;
				return false;
			}
		  }
	}
	return true;
}

bool CheckTrace::checkQueue() {

	WSQ wsq;
	int task;

	for (unsigned i = 0; i < curr_perm.size(); i++) {

		if (init_perm[curr_perm[i]].second.func->getName().str() == "queue_enqueue") {
			task = init_perm[curr_perm[i]].second.arg_vals.back();
			if (Params::programToCheck == QUEUE)
				wsq.seq_queue_enqueue(task);

			if (init_perm[curr_perm[i]].second.ret_val != 1) {
//				std::cout << "Failed! wsq_put: " <<
//					init_perm[curr_perm[i]].second.ret_val << " spec: 1" << std::endl;
				return false;
			}
		}
		else if (init_perm[curr_perm[i]].second.func->getName().str() == "queue_dequeue") {
			if(Params::programToCheck == QUEUE)
				task = wsq.seq_queue_dequeue();

			if (task != init_perm[curr_perm[i]].second.ret_val) {
//				std::cout << "Failed! wsq_take: " <<
//				              init_perm[curr_perm[i]].second.ret_val << " spec: " << task << std::endl;
				return false;
			}
	  }
	}
	return true;
}

bool CheckTrace::checkDeque() {

	WSQ wsq;
	int task;

	for (unsigned i = 0; i < curr_perm.size(); i++) {

		if (init_perm[curr_perm[i]].second.func->getName().str() == "deque_add_left") {
			task = init_perm[curr_perm[i]].second.arg_vals.back();
			if (Params::programToCheck == DEQUE)
				wsq.seq_deque_add_left(task);

			if (init_perm[curr_perm[i]].second.ret_val != 1) {
//				std::cout << "Failed! wsq_put: " <<
//					init_perm[curr_perm[i]].second.ret_val << " spec: 1" << std::endl;
				return false;
			}
		}
		else if (init_perm[curr_perm[i]].second.func->getName().str() == "deque_add_right") {
			task = init_perm[curr_perm[i]].second.arg_vals.back();
			if (Params::programToCheck == DEQUE)
				wsq.seq_deque_add_right(task);

			if (init_perm[curr_perm[i]].second.ret_val != 1) {
//				std::cout << "Failed! wsq_put: " <<
//					init_perm[curr_perm[i]].second.ret_val << " spec: 1" << std::endl;
				return false;
			}
		}	
		else if (init_perm[curr_perm[i]].second.func->getName().str() == "deque_remove_left") {
			if(Params::programToCheck == DEQUE)
				task = wsq.seq_deque_remove_left();

			if (task != init_perm[curr_perm[i]].second.ret_val) {
//				std::cout << "Failed! wsq_take: " <<
//				              init_perm[curr_perm[i]].second.ret_val << " spec: " << task << std::endl;
				return false;
			}
	  }
		else if (init_perm[curr_perm[i]].second.func->getName().str() == "deque_remove_right") {
			if(Params::programToCheck == DEQUE)
				task = wsq.seq_deque_remove_right();

			if (task != init_perm[curr_perm[i]].second.ret_val) {
//				std::cout << "Failed! wsq_take: " <<
//				              init_perm[curr_perm[i]].second.ret_val << " spec: " << task << std::endl;
				return false;
			}
	  }
	}
	return true;
}

bool CheckTrace::checkLinkSet() {

	LKS wsq;
	int task;
	int rst;

	for (unsigned i = 0; i < curr_perm.size(); i++) {

		if (init_perm[curr_perm[i]].second.func->getName().str() == "linkset_add") {
			task = init_perm[curr_perm[i]].second.arg_vals.back();
			if (Params::programToCheck == LINKSET)
				wsq.seq_linkset_add(task);

			if (init_perm[curr_perm[i]].second.ret_val != 1) {
//				std::cout << "Failed! wsq_put: " <<
//					init_perm[curr_perm[i]].second.ret_val << " spec: 1" << std::endl;
				return false;
			}
		}
		if (init_perm[curr_perm[i]].second.func->getName().str() == "linkset_contains") {
			task = init_perm[curr_perm[i]].second.arg_vals.back();
			if (Params::programToCheck == LINKSET)
				rst = wsq.seq_linkset_contains(task);

			if (init_perm[curr_perm[i]].second.ret_val != rst) {
//				std::cout << "Failed! wsq_put: " <<
//					init_perm[curr_perm[i]].second.ret_val << " spec: 1" << std::endl;
				return false;
			}
		}
		if (init_perm[curr_perm[i]].second.func->getName().str() == "linkset_remove") {
			task = init_perm[curr_perm[i]].second.arg_vals.back();
			if (Params::programToCheck == LINKSET)
				rst = wsq.seq_linkset_remove(task);

			if (init_perm[curr_perm[i]].second.ret_val != rst) {
//				std::cout << "Failed! wsq_put: " <<
//					init_perm[curr_perm[i]].second.ret_val << " spec: 1" << std::endl;
				return false;
			}
		}
	}
	return true;
}

bool CheckTrace::checkMalloc() {

	LFMALLOC lfmalloc;

	for (unsigned i = 0; i < curr_perm.size(); i++)	{
		trace_elem elem = init_perm[curr_perm[i]].second;
		if (init_perm[curr_perm[i]].second.func->getName().str() == "mmalloc") {
			unsigned start = (unsigned)elem.ret_val;
			unsigned size =  (unsigned)elem.arg_vals.front();
			unsigned finish = start + size;
			typedef std::list<std::pair<int, size_t> >::const_iterator malloc_list_iter;
			for (malloc_list_iter j = lfmalloc.alloc_list.begin();
				j != lfmalloc.alloc_list.end(); ++j) {
				if (start >= (unsigned)j->first && start <= ((unsigned)j->first + (unsigned)j->second)) {
					return false; // malloc can not return an address which is between the space which has been allocated
				}
				if (finish >= (unsigned)j->first && finish <= ((unsigned)j->first + (unsigned)j->second)) {
					return false; // malloc can not also return an address end which is between ...
				}
			}
	  	lfmalloc.alloc_list.push_back(std::make_pair((int)start, (size_t)size)); // put it in the allocated list
	 	}
	  else if (init_perm[curr_perm[i]].second.func->getName().str() == "mfree") {
			unsigned free_addr = (unsigned)elem.arg_vals.front();
			std::list<std::pair<int, size_t> >::iterator j;
			for (j = lfmalloc.alloc_list.begin(); j != lfmalloc.alloc_list.end(); ++j) {
				if (free_addr == (unsigned)j->first) {
					lfmalloc.alloc_list.erase(j);
					break;
				}
			}
			if (j == lfmalloc.alloc_list.end()) {
				return false;
			}
	  }
	}
	return true;
}

/* return: false-not preserved; true-preserved */
bool CheckTrace::isRealTimeOrderPreserved() 
{
	bool flag = true;
	for (unsigned i = 0; i < curr_perm.size(); i++) {
		for (unsigned j = i + 1; j < curr_perm.size(); j++) {
			if (!((init_perm[curr_perm[i]].first.first > init_perm[curr_perm[j]].first.first &&
			       init_perm[curr_perm[i]].first.first < init_perm[curr_perm[j]].first.second) ||
     	      (init_perm[curr_perm[j]].first.first > init_perm[curr_perm[i]].first.first &&
			       init_perm[curr_perm[j]].first.first < init_perm[curr_perm[i]].first.second)) &&
			      (init_perm[curr_perm[i]].first.first > init_perm[curr_perm[j]].first.first)) {
					flag = false;
					break;
				}
		 }
	}
	return flag;
}

bool CheckTrace::checkPermutation() {
	if (Params::programToCheck == WSQ_CHASE || Params::programToCheck == WSQ_LIFO || 
			Params::programToCheck == WSQ_FIFO || Params::programToCheck == WSQ_THE || 
			Params::programToCheck == WSQ_ANCHOR) 
		return checkWSQ();						

	if (Params::programToCheck == QUEUE) 
		return checkQueue();

	if (Params::programToCheck == DEQUE) 
		return checkDeque();


	if (Params::programToCheck == LINKSET) 
		return checkLinkSet();

	if (Params::programToCheck == LF_MALLOC) 
		return checkMalloc();

	std::cout << "checkPerm::undefined program" << std::endl;
	exit(254);
}

void CheckTrace::printPerm() {
	std::cout << "START OF LIN PERMUTATION" << std::endl;
	for (std::vector<int>::const_iterator ci = curr_perm.begin(); ci != curr_perm.end(); ++ci) {
		std::cout << init_perm[(*ci)].second.func->getName().str() << " on thread " 
							<< init_perm[(*ci)].second.thread.tid() << std::endl;
	}
	std::cout << "END OF LIN PERMUTATION" << std::endl;
}

int CheckTrace::checkHistory(History* history, int nextThreadNum) {

	history->printRecordedTrace();

	if (!Params::recTrace())
		return 1;

	gen_init_sc_perm(history, nextThreadNum);
	do {

		if (Params::Property == PROP_LIN && !isRealTimeOrderPreserved()) // additional task for linearizability
			continue;

		if (checkPermutation()) // find an equal trace
		{
			std::cout << "sc/lin check succeeded" << std::endl;
			free_init_sc_perm();
			return 0;
		}

	} while (gen_next_sc_perm(nextThreadNum));

	free_init_sc_perm();
	std::cout << "sc/lin check failed" << std::endl;
	return 253;
}
