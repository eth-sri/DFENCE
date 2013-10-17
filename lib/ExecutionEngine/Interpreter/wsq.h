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

#ifndef LLI_WSQ_H
#define LLI_WSQ_H

#include <list>
#include <deque>

namespace llvm {

class WSQ {
       private:
	std::deque<int> q;
       public:
	int seq_wsq_put_chase(int);
	int seq_wsq_take_chase();
	int seq_wsq_steal_chase();

	int seq_wsq_put_lifo(int);
	int seq_wsq_take_lifo();
	int seq_wsq_steal_lifo();

	int seq_wsq_put_fifo(int);
	int seq_wsq_take_fifo();
	int seq_wsq_steal_fifo();

	int seq_wsq_put_the(int);
	int seq_wsq_take_the();
	int seq_wsq_steal_the();

	int seq_wsq_put_anchor(int);
	int seq_wsq_take_anchor();
	int seq_wsq_steal_anchor();

	/* ms2 and msn share the same specification */
	int seq_queue_enqueue(int);
	int seq_queue_dequeue();

	int seq_deque_add_left(int);
	int seq_deque_add_right(int);
	int seq_deque_remove_left();
	int seq_deque_remove_right();

};
}
#endif
