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

#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include "llvm/Module.h"

#include <map>
#include <vector>
#include <set>

#include "RWHistory.h"
#include "SatSolver/Solver.h"

using namespace std;

typedef pair<int, int> tso_constraint_pair;
typedef pair<tso_constraint_pair, int> Tso_Constraint_To_Lit_Elem;
typedef map<tso_constraint_pair, int> Tso_Constraint_To_Lit;

typedef pair<int, int> pso_constraint_pair;
typedef pair<pso_constraint_pair, int> Pso_Constraint_To_Lit_Elem;
typedef map<pso_constraint_pair, int> Pso_Constraint_To_Lit;

typedef pair<Instruction*, Instruction*> constraint_pair;
typedef vector<constraint_pair> Constraint_t;
typedef vector<bool> Constraint_type_t;

//typedef vector<int> ClausesList;
typedef set<int> ClausesList;
typedef vector<rwtrace_elem> Trace;
typedef map<Thread, Trace*> MapThrdToTrace;
typedef vector<ClausesList*> SatSolutions;

class Constraints {
private:
	// clauses; need to clean up for each trace
	ClausesList clauses;
	int clauseIndex;

	// record; in case
	Tso_Constraint_To_Lit mapToLit;
	Pso_Constraint_To_Lit mapToLit_ss;
	Constraint_t finalSatSolution;
	Constraint_type_t finalSatSolutionType;
	
	Solver* S;
	SatSolutions satSolutions;
	ClausesList mergedSatSolution;

public:
	Constraints() {
		clauseIndex = 1;
		S = new Solver;
	}
	~Constraints() {
		delete S;	
	}

	void SetupInstructionLabelMap(Module* Mod);
	void InsertFences(Module* Mod);

	void Calculate(RWHistory* history, int nextThreadNum);
	void GenerateClauses(int begin, int end, Trace& trace);
	void AddToSolver();
	int Solve();
	void Merge();
	void Flush();

	/* for debugging propose */
	void PrintConstraintInst(ClausesList* list);
	void PrintOrderedInst();
	void PrintFinalInst();

	/* for status */
	int GetLitSingleNumber(); 
	int GetLitTotalNumber(); 

	/* Both functions and their definitions are for drawing figures */
	int CheckConstraintInst(ClausesList* clist); 
	int CheckCorrectness(); 
	void Flush_partial();
};

#endif
