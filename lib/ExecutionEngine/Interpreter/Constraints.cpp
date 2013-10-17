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

#include "llvm/Function.h"
#include "llvm/BasicBlock.h"
#include "llvm/Instructions.h"
#include "llvm/Instruction.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Type.h"

#include <iostream>
#include <new>
#include <list>

#include "Constraints.h"
#include "Params.h"

using namespace std;

typedef pair<int, Instruction*> InstrLabel_pair;
typedef map<int, Instruction*> InstrLabel_map;
InstrLabel_map instrLabelMap;

/* find next */
int findNextBegin(int end, Trace& trace) {
	while (1) {
		if (end == trace.size()) {
			return end;
		}
		int label = trace[end].label;
		if (label != 0) {
			return end; 
		}	
		end++;
	}
	assert(0 && "Can not arrive here!");
	return 0;
}

int findNextEnd(int begin, Trace& trace) {
	while (1) {
		if (begin == trace.size()) { // the max index should be size - 1, right?
			return begin;
		}
		int label = trace[begin].label;
		if (label == 0) {
			return begin;
		}
		begin++;
	}
	assert(0 && "Can not arrive here!");
	return 0;
}

void InsertSLFencesAfter(Instruction* instr, Module* M) {
	LLVMContext &Context = M->getContext();
	const Type* voidTy = Type::getVoidTy(Context);
	FunctionType* FT = FunctionType::get(voidTy, 1);
	Constant* fence = M->getOrInsertFunction("membar_sl", FT); 
	BasicBlock::iterator I = instr;
	I++;
	CallInst* call = CallInst::Create(fence, "", I); // insert before
	call->label_instr = 0; 
}

void InsertSSFencesAfter(Instruction* instr, Module* M) {
	LLVMContext &Context = M->getContext();
	const Type* voidTy = Type::getVoidTy(Context);
	FunctionType* FT = FunctionType::get(voidTy, 1);
	Constant* fence = M->getOrInsertFunction("membar_ss", FT); 
	BasicBlock::iterator I = instr;
	I++;
	CallInst* call = CallInst::Create(fence, "", I); // insert before
	call->label_instr = 0; 
}

void printInstr(Instruction* instr) {
	BasicBlock* bb = instr->getParent();
	Function* fn = bb->getParent();
	dbgs() << "In function: " << fn->getNameStr() 
				 << "; block: " << bb->getNameStr() << "\n";
	instr->print(dbgs());
	dbgs() << "\n";
}

void Constraints::SetupInstructionLabelMap(Module* M) {
	for (Module::iterator F = M->begin(), FE = M->end(); F != FE; F++) {
		for (Function::iterator BB = F->begin(), BBE = F->end(); BB != BBE; BB++) {
			for (BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; I++) {
				instrLabelMap.insert(InstrLabel_pair(I->label_instr, I));
			}
		}
	}
}

void Constraints::InsertFences(Module* M) {
	for (ClausesList::iterator it = mergedSatSolution.begin(), 
		ite = mergedSatSolution.end(); it != ite; it++) {
		int lit = *it;
		bool flag_is_true = false;
		tso_constraint_pair pair;
		for (Tso_Constraint_To_Lit::iterator mit = mapToLit.begin(), 
			mite = mapToLit.end(); mit != mite; mit++) {
			if ((*mit).second == lit) {
				pair = (*mit).first;
				flag_is_true = true;
				break;
			}
		}
		
    if (flag_is_true) {
			InsertSLFencesAfter(instrLabelMap[pair.first], M);
		} else {
			pso_constraint_pair pair;
			for (Pso_Constraint_To_Lit::iterator mit = mapToLit_ss.begin(), 
				mite = mapToLit_ss.end(); mit != mite; mit++) {
				if ((*mit).second == lit) {
					pair = (*mit).first;
					break;
				}
			}
			InsertSSFencesAfter(instrLabelMap[pair.first], M); 	
		}
	}
}

void Constraints::Calculate(RWHistory* history, int nextThreadNum) {
	clauses.clear();

	Trace* trace = &history->shared_rec; // to the accesses which are shared. 

	MapThrdToTrace allTrace;

	for (Thread i = 1; i < nextThreadNum; ++i) {
		allTrace[i] = new Trace;
	}

	/* convert total order to partial order */
	for (Trace::iterator it = trace->begin(), ite = trace->end();
    it != ite; it++) {
		Thread index = it->thr;
		allTrace[index]->push_back(*it);
	}

#ifdef DEBUG_TOOL
	for (Thread i = 1; i < nextThreadNum; ++i) {
		Trace* trace_temp = allTrace[i];
		cout << "Thread " << i.tid() << " : " << trace_temp->size() << endl;
	}
#endif

	/* for each partial order, generate constraints */
	for (Thread i = 1; i < nextThreadNum; ++i) {
		Trace* trace_temp = allTrace[i];
		int size = trace_temp->size();
		assert(trace_temp->size() > 0); 
		assert(trace_temp->begin()->label != 0); 
		int front = 0;
		while (1) {
			if ((*trace_temp)[front].label != 0) {
				break;
      }
			front++;
		}
		int back = findNextEnd(front, *trace_temp);
		while (1) {
#ifdef DEBUG_TOOL
		cout << "Front # " << front << endl;
		cout << "Back # " << back << endl;
#endif
			if (front == back) break;
			GenerateClauses(front, back, *trace_temp);
			front = findNextBegin(back, *trace_temp);
			back = findNextEnd(front, *trace_temp);
		}
	}

	for (Thread i = 1; i < nextThreadNum; ++i) {
		delete allTrace[i];
	}
}

void Constraints::GenerateClauses(int begin, int end, Trace& trace) {

	if (Params::WMM == WMM_TSO) {
		typedef list<rwtrace_elem> STBuffer;
		STBuffer store_buffer;
		for (int i = begin; i < end; i++) {
			if (trace[i].type == READ) {
				for (STBuffer::iterator itb = store_buffer.begin(); 
					itb != store_buffer.end(); itb++) {
					if (trace[i].location != itb->location) {
						int lit;
						int st = itb->label;
						int ld = trace[i].label;
						tso_constraint_pair stld_pair = tso_constraint_pair(st, ld);
						Tso_Constraint_To_Lit::iterator it = mapToLit.find(stld_pair);
						if (it == mapToLit.end()) {
							lit = clauseIndex; // clauses?
							mapToLit.insert(Tso_Constraint_To_Lit_Elem(stld_pair, lit));
							clauseIndex = clauseIndex + 1;
						} else {
							lit = it->second;
						}
						clauses.insert(lit);
					}
				}
			} else if (trace[i].type == WRITE) {
				store_buffer.push_back(trace[i]);
			} else if (trace[i].type == FLUSH_RANDOM_TSO) {
				if (!store_buffer.empty()) {
					store_buffer.pop_front();
				}
			} else {
				cout << "UNRECOGNIZED record type!" << endl;
			}
		}
	
	} else if (Params::WMM == WMM_PSO) {
		typedef map<int*, list<rwtrace_elem> > SSBuffer;
		SSBuffer var_store_buffer;

		for (int i = begin; i < end; i++) {
			if (trace[i].type == READ) {
				for (SSBuffer::iterator itb = var_store_buffer.begin();
					itb != var_store_buffer.end(); itb++) {
					for (list<rwtrace_elem>::iterator itl = itb->second.begin();
						itl != itb->second.end(); itl++) {
						if (trace[i].location != itl->location) {
							int lit;
							int st = itl->label; 
							int ld = trace[i].label;
							tso_constraint_pair stld_pair = tso_constraint_pair(st, ld);
							Tso_Constraint_To_Lit::iterator it = mapToLit.find(stld_pair);
							if (it == mapToLit.end()) {
								lit = clauseIndex; // clauses? 
								mapToLit.insert(Tso_Constraint_To_Lit_Elem(stld_pair, lit));
								clauseIndex = clauseIndex + 1;
							} else {
								lit = it->second;
							}
							clauses.insert(lit);
						}
					}
				}

			} else if (trace[i].type == WRITE) {
				for (SSBuffer::iterator itb = var_store_buffer.begin();
					itb != var_store_buffer.end(); itb++) {
					for (list<rwtrace_elem>::iterator itl = itb->second.begin();
						itl != itb->second.end(); itl++) {
						if (trace[i].location != itl->location) {
							int lit;
							int st1 = itl->label;
							int st2 = trace[i].label;
							pso_constraint_pair stst_pair = pso_constraint_pair(st1, st2);
							Pso_Constraint_To_Lit::iterator it = mapToLit_ss.find(stst_pair);
							if (it == mapToLit_ss.end()) {
								lit = clauseIndex; // clauses? 
								mapToLit_ss.insert(Pso_Constraint_To_Lit_Elem(stst_pair, lit));
								clauseIndex = clauseIndex + 1;
							} else {
								lit = it->second;
							}
							clauses.insert(lit);
						}
					}
				}
				int* location = trace[i].location;
				var_store_buffer[location].push_back(trace[i]);

			} else if (trace[i].type == FLUSH_RANDOM_PSO) { 
				int* location = trace[i].location;
				if (!var_store_buffer[location].empty()) {
					var_store_buffer[location].pop_front();	
				}

			} else if (trace[i].type == FLUSH_CAS_PSO) {
				int* location = trace[i].location;
				while (!var_store_buffer[location].empty()) {
					var_store_buffer[location].pop_front();
				}	
			} else {
				cout << "UNRECOGNIZED record type!" << endl;
			}
		}
	} else {
		cout << "UNRECOGNIZED memory model!" << endl;
		exit(1);
	}
}

void Constraints::AddToSolver() {
	vec<Lit> lits;
	
	for (ClausesList::iterator it = clauses.begin(), ite = clauses.end();
		it != ite; it++) {
		int var = *it; 
		while (var >= S->nVars()) {
			S->newVar();	
		}
		lits.push(Lit(var));
	}
	S->addClause(lits);
}

#define MUL
int Constraints::Solve() {
	if (!S->okay()) {
		cout << "Trivial problem\n" << endl;
		cout << "UNSATISFIABLE\n"	<< endl;
		return 0;
	}

	S->solve();

#ifdef MUL
	if (S->okay()) {
		int counter = 0;
		ClausesList* satSolution_ptr = new ClausesList;
		satSolutions.push_back(satSolution_ptr);
		for (int i = 0; i < S->nVars(); i++) {
			if (S->model[i] == l_True) {
				satSolutions[0]->insert(i);
				counter++;
			}
		}
		//dbgs() << "\nSolution has " << counter << " constraints!\n\n";
		return 1;
	} else {
		return 0;
	}
#else
	if (S->okay()) {
		int counter = 0;
		while (S->okay()) {
			counter++;
			ClausesList* satSolution_ptr = new ClausesList;
			satSolutions.push_back(satSolution_ptr);
			vec<Lit> lits;	
			for (int i = 0; i <= S->nVars(); i++) {
				if (S->model[i] == l_True) {
					satSolution_ptr->insert(i);
					lits.push(Lit(i, true)); // made additional constraints	
					dbgs() << i << " ";
				}
			}
			S->addClause(lits);
			S->solve();
			dbgs() << "\n";
		}

		dbgs() << "\nSolution has " << counter << " constraint groups!\n\n";
		return 1;
	} else {
		cout << "UNSATISFIABLE\n"	<< endl;
		return 0;
	}
#endif
}

/* Merge contraints together, to reduce fence number */
/* Just delete some lits from satSolution */
void Constraints::Merge() {
	unsigned counter = clauseIndex;
	for (SatSolutions::iterator i = satSolutions.begin(), ie = satSolutions.end();
		i != ie; i++) {
		ClausesList* list = *i;
		if (counter > list->size()) {
			counter = list->size();
		}
	}

	for (SatSolutions::iterator i = satSolutions.begin(), ie = satSolutions.end();
		i != ie; i++) {
		ClausesList* list = *i;	
		if (counter == list->size()) {
			for (ClausesList::iterator ci = list->begin(), cie = list->end();
				ci != cie; ci++) {
				mergedSatSolution.insert(*ci);
			}
		}
		delete list;		
	}	

	// implemented an algorithm to delete the fences which have been added
	static set<StoreInst*> solvedStores; // to support search
	for (ClausesList::iterator it = mergedSatSolution.begin(), 
		ite = mergedSatSolution.end(); it != ite; ) {
		int lit = *it;
		tso_constraint_pair pair; // find constrain pair
		for (Tso_Constraint_To_Lit::iterator mit = mapToLit.begin(), 
			mite = mapToLit.end(); mit != mite; mit++) {
			if ((*mit).second == lit) {
				pair = (*mit).first;
				break;
			}
		}

		StoreInst* thisStore = (StoreInst*)instrLabelMap[pair.first];	
		if (solvedStores.find(thisStore) != solvedStores.end()) {
			ClausesList::iterator it_temp = it++;
			mergedSatSolution.erase(it_temp);
		} else {
			it++;
			solvedStores.insert(thisStore);
		}
	}
}

void Constraints::Flush_partial() {
	clauses.clear();
	mergedSatSolution.clear();
	satSolutions.clear();
}

void Constraints::Flush() {
	clauses.clear();
	clauseIndex = 1;
	mapToLit.clear();
	mapToLit_ss.clear();
	delete S;
	S = new Solver;
	mergedSatSolution.clear();
	satSolutions.clear();
}

void Constraints::PrintConstraintInst(ClausesList* clist) {
	for (ClausesList::iterator it = clist->begin(), ite = clist->end();
 		it != ite; it++) {
		int lit = *it;
		bool flag = false;
		tso_constraint_pair pair1;
		for (Tso_Constraint_To_Lit::iterator mit = mapToLit.begin(), 
			mite = mapToLit.end(); mit != mite; mit++) {
			if ((*mit).second == lit) {
				pair1 = (*mit).first;
				flag = true;
				break;
			}
		}

		pso_constraint_pair pair2;
		if (!flag) {
			Pso_Constraint_To_Lit::iterator mit = mapToLit_ss.begin();
			for ( ; mit != mapToLit_ss.end(); mit++) {
				if ((*mit).second == lit) {
					pair2 = (*mit).first;
					break;
				}
			}
			if (mit == mapToLit_ss.end()) {
				dbgs() << "can not find lits in the map\n";
				dbgs() << "lit: " << lit << "\n";
				assert(0);
				exit(255);
			}
		}

		dbgs() << "==========\n";
		if (flag) {
			Instruction* instr1 = instrLabelMap[pair1.first];
			Instruction* instr2 = instrLabelMap[pair1.second];

			dbgs() << pair1.first << "\n";
			dbgs() << "= store_load_fence  =\n";
			dbgs() << pair1.second << "\n";
			dbgs() << "---\n";
			printInstr(instr1);
			dbgs() << "-----------------\n";
			printInstr(instr2);

			finalSatSolution.push_back(constraint_pair(instr1, instr2));
			finalSatSolutionType.push_back(true);
		} else { 
			Instruction* instr1 = instrLabelMap[pair2.first];
			Instruction* instr2 = instrLabelMap[pair2.second];

			dbgs() << pair2.first << "\n";
			dbgs() << "= store_store_fence =\n";
			dbgs() << pair2.second << "\n";
			dbgs() << "---\n";
			printInstr(instr1);
			dbgs() << "-----------------\n";
			printInstr(instr2);

			finalSatSolution.push_back(constraint_pair(instr1, instr2));
			finalSatSolutionType.push_back(false);
		}
		dbgs() << "==========\n";
	}
}

int Constraints::CheckConstraintInst(ClausesList* clist) {
	int counter = 0;

	for (ClausesList::iterator it = clist->begin(), ite = clist->end();
 		it != ite; it++) {
		int lit = *it;
		bool flag = false;
		tso_constraint_pair pair1;
		for (Tso_Constraint_To_Lit::iterator mit = mapToLit.begin(), 
			mite = mapToLit.end(); mit != mite; mit++) {
			if ((*mit).second == lit) {
				pair1 = (*mit).first;
				flag = true;
				break;
			}
		}

		pso_constraint_pair pair2;
		if (!flag) {
			Pso_Constraint_To_Lit::iterator mit = mapToLit_ss.begin();
			for ( ; mit != mapToLit_ss.end(); mit++) {
				if ((*mit).second == lit) {
					pair2 = (*mit).first;
					break;
				}
			}
			if (mit == mapToLit_ss.end()) {
				dbgs() << "can not find lits in the map\n";
				dbgs() << "lit: " << lit << "\n";
				assert(0);
				exit(255);
			}
		}

		if (flag) {
			Instruction* instr1 = instrLabelMap[pair1.first];
			Instruction* instr2 = instrLabelMap[pair1.second];

			if (pair1.first == 419 || pair1.first == 382 || pair1.first == 521) {
				counter++;
			}
		} else { 
			Instruction* instr1 = instrLabelMap[pair2.first];
			Instruction* instr2 = instrLabelMap[pair2.second];

			if (pair1.first == 419 || pair1.first == 382 || pair1.first == 521) {
				counter++;
			}
		}
	}
	return counter;
}

void Constraints::PrintOrderedInst() {
	PrintConstraintInst(&mergedSatSolution);
}

//This function is used only to generate experimental results.
int Constraints::CheckCorrectness() {
	return CheckConstraintInst(&mergedSatSolution);
}

void Constraints::PrintFinalInst() {
	dbgs() << "There are " << finalSatSolution.size() << " fences in total!\n";
	Constraint_t::iterator it = finalSatSolution.begin();
	Constraint_type_t::iterator typeit = finalSatSolutionType.begin();
	for( ; it != finalSatSolution.end(); it++, typeit++) {
		dbgs() << "==========\n";
		dbgs() << it->first->label_instr << "\n";
		if (*typeit) {
			dbgs() << "= store_load_fence  =\n";
		} else {
			dbgs() << "= store_store_fence  =\n";
		}
		dbgs() << it->second->label_instr << "\n";
		dbgs() << "---\n";
		printInstr(it->first);
		dbgs() << "-----------------\n";
		printInstr(it->second);
		dbgs() << "==========\n";
	}
}

int Constraints::GetLitSingleNumber() {
	return clauses.size(); 
}

int Constraints::GetLitTotalNumber() {
	return S->nVars(); 
}
