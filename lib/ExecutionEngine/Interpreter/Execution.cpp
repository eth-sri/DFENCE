//===-- Execution.cpp - Implement code to simulate the program ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file contains the actual instruction interpreter.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "interpreter"
#include "Interpreter.h"
#include "CheckTrace.h"
#include "Params.h"
#include "Scheduler.h"
#include "Constraints.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/ExecutionEngine/Thread.h"
//#include "llvm/ExecutionEngine/RWHistory.h" // to record the FLUSH
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <list>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <map>
#include <sys/mman.h>
#include <algorithm>
#include <sys/time.h>
#include <time.h>

clock_t timeofChecking;

using std::cout;
using std::endl;
using namespace llvm;

STATISTIC(NumDynamicInsts, "Number of dynamic instructions executed");

static cl::opt<bool> PrintVolatile("interpreter-print-volatile", cl::Hidden,
		cl::desc("make the interpreter print every volatile load and store"));

//===----------------------------------------------------------------------===//
//                     Various Helper Functions
//===----------------------------------------------------------------------===//

static void SetValue(Value *V, GenericValue Val, ExecutionContext &SF)
{
	SF.Values[V] = Val;
}

//===----------------------------------------------------------------------===//
//                    Binary Instruction Implementations
//===----------------------------------------------------------------------===//

#define IMPLEMENT_BINARY_OPERATOR(OP, TY) \
	case Type::TY##TyID: \
Dest.TY##Val = Src1.TY##Val OP Src2.TY##Val; \
break

static void executeFAddInst(GenericValue &Dest, GenericValue Src1,
		GenericValue Src2, const Type *Ty)
{
	switch (Ty->getTypeID())
	{
		IMPLEMENT_BINARY_OPERATOR(+, Float);
		IMPLEMENT_BINARY_OPERATOR(+, Double);
		default:
		dbgs() << "Unhandled type for FAdd instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
}

static void executeFSubInst(GenericValue &Dest, GenericValue Src1,
		GenericValue Src2, const Type *Ty)
{
	switch (Ty->getTypeID())
	{
		IMPLEMENT_BINARY_OPERATOR(-, Float);
		IMPLEMENT_BINARY_OPERATOR(-, Double);
		default:
		dbgs() << "Unhandled type for FSub instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
}

static void executeFMulInst(GenericValue &Dest, GenericValue Src1,
		GenericValue Src2, const Type *Ty)
{
	switch (Ty->getTypeID())
	{
		IMPLEMENT_BINARY_OPERATOR(*, Float);
		IMPLEMENT_BINARY_OPERATOR(*, Double);
		default:
		dbgs() << "Unhandled type for FMul instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
}

static void executeFDivInst(GenericValue &Dest, GenericValue Src1,
		GenericValue Src2, const Type *Ty)
{
	switch (Ty->getTypeID())
	{
		IMPLEMENT_BINARY_OPERATOR(/, Float);
		IMPLEMENT_BINARY_OPERATOR(/, Double);
		default:
		dbgs() << "Unhandled type for FDiv instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
}

static void executeFRemInst(GenericValue &Dest, GenericValue Src1,
		GenericValue Src2, const Type *Ty)
{
	switch (Ty->getTypeID())
	{
		case Type::FloatTyID:
			Dest.FloatVal = fmod(Src1.FloatVal, Src2.FloatVal);
			break;
		case Type::DoubleTyID:
			Dest.DoubleVal = fmod(Src1.DoubleVal, Src2.DoubleVal);
			break;
		default:
			dbgs() << "Unhandled type for Rem instruction: " << *Ty << "\n";
			llvm_unreachable(0);
	}
}

#define IMPLEMENT_INTEGER_ICMP(OP, TY) \
	case Type::IntegerTyID:  \
Dest.IntVal = APInt(1,Src1.IntVal.OP(Src2.IntVal)); \
break;

// Handle pointers specially because they must be compared with only as much
// width as the host has.  We _do not_ want to be comparing 64 bit values when
// running on a 32-bit target, otherwise the upper 32 bits might mess up
// comparisons if they contain garbage.
#define IMPLEMENT_POINTER_ICMP(OP) \
	case Type::PointerTyID: \
Dest.IntVal = APInt(1,(void*)(intptr_t)Src1.PointerVal OP \
		(void*)(intptr_t)Src2.PointerVal); \
break;

static GenericValue executeICMP_EQ(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(eq,Ty);
		IMPLEMENT_POINTER_ICMP(==);
		default:
		dbgs() << "Unhandled type for ICMP_EQ predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_NE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(ne,Ty);
		IMPLEMENT_POINTER_ICMP(!=);
		default:
		dbgs() << "Unhandled type for ICMP_NE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_ULT(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(ult,Ty);
		IMPLEMENT_POINTER_ICMP(<);
		default:
		dbgs() << "Unhandled type for ICMP_ULT predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_SLT(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(slt,Ty);
		IMPLEMENT_POINTER_ICMP(<);
		default:
		dbgs() << "Unhandled type for ICMP_SLT predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_UGT(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(ugt,Ty);
		IMPLEMENT_POINTER_ICMP(>);
		default:
		dbgs() << "Unhandled type for ICMP_UGT predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_SGT(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(sgt,Ty);
		IMPLEMENT_POINTER_ICMP(>);
		default:
		dbgs() << "Unhandled type for ICMP_SGT predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_ULE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(ule,Ty);
		IMPLEMENT_POINTER_ICMP(<=);
		default:
		dbgs() << "Unhandled type for ICMP_ULE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_SLE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(sle,Ty);
		IMPLEMENT_POINTER_ICMP(<=);
		default:
		dbgs() << "Unhandled type for ICMP_SLE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_UGE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(uge,Ty);
		IMPLEMENT_POINTER_ICMP(>=);
		default:
		dbgs() << "Unhandled type for ICMP_UGE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_SGE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_INTEGER_ICMP(sge,Ty);
		IMPLEMENT_POINTER_ICMP(>=);
		default:
		dbgs() << "Unhandled type for ICMP_SGE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

void Interpreter::visitICmpInst(ICmpInst &I)
{
	ExecutionContext &SF = ECStack->back();
	const Type *Ty    = I.getOperand(0)->getType();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue R;   // Result

	switch (I.getPredicate())
	{
		case ICmpInst::ICMP_EQ:
			R = executeICMP_EQ(Src1,  Src2, Ty);
			break;
		case ICmpInst::ICMP_NE:
			R = executeICMP_NE(Src1,  Src2, Ty);
			break;
		case ICmpInst::ICMP_ULT:
			R = executeICMP_ULT(Src1, Src2, Ty);
			break;
		case ICmpInst::ICMP_SLT:
			R = executeICMP_SLT(Src1, Src2, Ty);
			break;
		case ICmpInst::ICMP_UGT:
			R = executeICMP_UGT(Src1, Src2, Ty);
			break;
		case ICmpInst::ICMP_SGT:
			R = executeICMP_SGT(Src1, Src2, Ty);
			break;
		case ICmpInst::ICMP_ULE:
			R = executeICMP_ULE(Src1, Src2, Ty);
			break;
		case ICmpInst::ICMP_SLE:
			R = executeICMP_SLE(Src1, Src2, Ty);
			break;
		case ICmpInst::ICMP_UGE:
			R = executeICMP_UGE(Src1, Src2, Ty);
			break;
		case ICmpInst::ICMP_SGE:
			R = executeICMP_SGE(Src1, Src2, Ty);
			break;
		default:
			dbgs() << "Don't know how to handle this ICmp predicate!\n-->" << I;
			llvm_unreachable(0);
	}

	SetValue(&I, R, SF);
}

#define IMPLEMENT_FCMP(OP, TY) \
	case Type::TY##TyID: \
Dest.IntVal = APInt(1,Src1.TY##Val OP Src2.TY##Val); \
break

static GenericValue executeFCMP_OEQ(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_FCMP(==, Float);
		IMPLEMENT_FCMP(==, Double);
		default:
		dbgs() << "Unhandled type for FCmp EQ instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeFCMP_ONE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_FCMP(!=, Float);
		IMPLEMENT_FCMP(!=, Double);

		default:
		dbgs() << "Unhandled type for FCmp NE instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeFCMP_OLE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_FCMP(<=, Float);
		IMPLEMENT_FCMP(<=, Double);
		default:
		dbgs() << "Unhandled type for FCmp LE instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeFCMP_OGE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_FCMP(>=, Float);
		IMPLEMENT_FCMP(>=, Double);
		default:
		dbgs() << "Unhandled type for FCmp GE instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeFCMP_OLT(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_FCMP(<, Float);
		IMPLEMENT_FCMP(<, Double);
		default:
		dbgs() << "Unhandled type for FCmp LT instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeFCMP_OGT(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	switch (Ty->getTypeID())
	{
		IMPLEMENT_FCMP(>, Float);
		IMPLEMENT_FCMP(>, Double);
		default:
		dbgs() << "Unhandled type for FCmp GT instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

#define IMPLEMENT_UNORDERED(TY, X,Y)                                     \
	if (TY->isFloatTy()) {                                                 \
		if (X.FloatVal != X.FloatVal || Y.FloatVal != Y.FloatVal) {          \
			Dest.IntVal = APInt(1,true);                                       \
			return Dest;                                                       \
		}                                                                    \
	} else if (X.DoubleVal != X.DoubleVal || Y.DoubleVal != Y.DoubleVal) { \
		Dest.IntVal = APInt(1,true);                                         \
		return Dest;                                                         \
	}


static GenericValue executeFCMP_UEQ(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
		return executeFCMP_OEQ(Src1, Src2, Ty);
}

static GenericValue executeFCMP_UNE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
		return executeFCMP_ONE(Src1, Src2, Ty);
}

static GenericValue executeFCMP_ULE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
		return executeFCMP_OLE(Src1, Src2, Ty);
}

static GenericValue executeFCMP_UGE(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
		return executeFCMP_OGE(Src1, Src2, Ty);
}

static GenericValue executeFCMP_ULT(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
		return executeFCMP_OLT(Src1, Src2, Ty);
}

static GenericValue executeFCMP_UGT(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
		return executeFCMP_OGT(Src1, Src2, Ty);
}

static GenericValue executeFCMP_ORD(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	if (Ty->isFloatTy())
		Dest.IntVal = APInt(1,(Src1.FloatVal == Src1.FloatVal &&
					Src2.FloatVal == Src2.FloatVal));
	else
		Dest.IntVal = APInt(1,(Src1.DoubleVal == Src1.DoubleVal &&
					Src2.DoubleVal == Src2.DoubleVal));
	return Dest;
}

static GenericValue executeFCMP_UNO(GenericValue Src1, GenericValue Src2,
		const Type *Ty)
{
	GenericValue Dest;
	if (Ty->isFloatTy())
		Dest.IntVal = APInt(1,(Src1.FloatVal != Src1.FloatVal ||
					Src2.FloatVal != Src2.FloatVal));
	else
		Dest.IntVal = APInt(1,(Src1.DoubleVal != Src1.DoubleVal ||
					Src2.DoubleVal != Src2.DoubleVal));
	return Dest;
}

void Interpreter::visitFCmpInst(FCmpInst &I)
{
	ExecutionContext &SF = ECStack->back();
	const Type *Ty    = I.getOperand(0)->getType();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue R;   // Result

	switch (I.getPredicate())
	{
		case FCmpInst::FCMP_FALSE:
			R.IntVal = APInt(1,false);
			break;
		case FCmpInst::FCMP_TRUE:
			R.IntVal = APInt(1,true);
			break;
		case FCmpInst::FCMP_ORD:
			R = executeFCMP_ORD(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_UNO:
			R = executeFCMP_UNO(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_UEQ:
			R = executeFCMP_UEQ(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_OEQ:
			R = executeFCMP_OEQ(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_UNE:
			R = executeFCMP_UNE(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_ONE:
			R = executeFCMP_ONE(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_ULT:
			R = executeFCMP_ULT(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_OLT:
			R = executeFCMP_OLT(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_UGT:
			R = executeFCMP_UGT(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_OGT:
			R = executeFCMP_OGT(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_ULE:
			R = executeFCMP_ULE(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_OLE:
			R = executeFCMP_OLE(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_UGE:
			R = executeFCMP_UGE(Src1, Src2, Ty);
			break;
		case FCmpInst::FCMP_OGE:
			R = executeFCMP_OGE(Src1, Src2, Ty);
			break;
		default:
			dbgs() << "Don't know how to handle this FCmp predicate!\n-->" << I;
			llvm_unreachable(0);
	}

	SetValue(&I, R, SF);
}

static GenericValue executeCmpInst(unsigned predicate, GenericValue Src1,
		GenericValue Src2, const Type *Ty)
{
	GenericValue Result;
	switch (predicate)
	{
		case ICmpInst::ICMP_EQ:
			return executeICMP_EQ(Src1, Src2, Ty);
		case ICmpInst::ICMP_NE:
			return executeICMP_NE(Src1, Src2, Ty);
		case ICmpInst::ICMP_UGT:
			return executeICMP_UGT(Src1, Src2, Ty);
		case ICmpInst::ICMP_SGT:
			return executeICMP_SGT(Src1, Src2, Ty);
		case ICmpInst::ICMP_ULT:
			return executeICMP_ULT(Src1, Src2, Ty);
		case ICmpInst::ICMP_SLT:
			return executeICMP_SLT(Src1, Src2, Ty);
		case ICmpInst::ICMP_UGE:
			return executeICMP_UGE(Src1, Src2, Ty);
		case ICmpInst::ICMP_SGE:
			return executeICMP_SGE(Src1, Src2, Ty);
		case ICmpInst::ICMP_ULE:
			return executeICMP_ULE(Src1, Src2, Ty);
		case ICmpInst::ICMP_SLE:
			return executeICMP_SLE(Src1, Src2, Ty);
		case FCmpInst::FCMP_ORD:
			return executeFCMP_ORD(Src1, Src2, Ty);
		case FCmpInst::FCMP_UNO:
			return executeFCMP_UNO(Src1, Src2, Ty);
		case FCmpInst::FCMP_OEQ:
			return executeFCMP_OEQ(Src1, Src2, Ty);
		case FCmpInst::FCMP_UEQ:
			return executeFCMP_UEQ(Src1, Src2, Ty);
		case FCmpInst::FCMP_ONE:
			return executeFCMP_ONE(Src1, Src2, Ty);
		case FCmpInst::FCMP_UNE:
			return executeFCMP_UNE(Src1, Src2, Ty);
		case FCmpInst::FCMP_OLT:
			return executeFCMP_OLT(Src1, Src2, Ty);
		case FCmpInst::FCMP_ULT:
			return executeFCMP_ULT(Src1, Src2, Ty);
		case FCmpInst::FCMP_OGT:
			return executeFCMP_OGT(Src1, Src2, Ty);
		case FCmpInst::FCMP_UGT:
			return executeFCMP_UGT(Src1, Src2, Ty);
		case FCmpInst::FCMP_OLE:
			return executeFCMP_OLE(Src1, Src2, Ty);
		case FCmpInst::FCMP_ULE:
			return executeFCMP_ULE(Src1, Src2, Ty);
		case FCmpInst::FCMP_OGE:
			return executeFCMP_OGE(Src1, Src2, Ty);
		case FCmpInst::FCMP_UGE:
			return executeFCMP_UGE(Src1, Src2, Ty);
		case FCmpInst::FCMP_FALSE:
			{
				GenericValue Result;
				Result.IntVal = APInt(1, false);
				return Result;
			}
		case FCmpInst::FCMP_TRUE:
			{
				GenericValue Result;
				Result.IntVal = APInt(1, true);
				return Result;
			}
		default:
			dbgs() << "Unhandled Cmp predicate\n";
			llvm_unreachable(0);
	}
}

void Interpreter::visitBinaryOperator(BinaryOperator &I)
{
	ExecutionContext &SF = ECStack->back();
	const Type *Ty    = I.getOperand(0)->getType();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue R;   // Result

	switch (I.getOpcode())
	{
		case Instruction::Add:
			R.IntVal = Src1.IntVal + Src2.IntVal;
			break;
		case Instruction::Sub:
			R.IntVal = Src1.IntVal - Src2.IntVal;
			break;
		case Instruction::Mul:
			R.IntVal = Src1.IntVal * Src2.IntVal;
			break;
		case Instruction::FAdd:
			executeFAddInst(R, Src1, Src2, Ty);
			break;
		case Instruction::FSub:
			executeFSubInst(R, Src1, Src2, Ty);
			break;
		case Instruction::FMul:
			executeFMulInst(R, Src1, Src2, Ty);
			break;
		case Instruction::FDiv:
			executeFDivInst(R, Src1, Src2, Ty);
			break;
		case Instruction::FRem:
			executeFRemInst(R, Src1, Src2, Ty);
			break;
		case Instruction::UDiv:
			R.IntVal = Src1.IntVal.udiv(Src2.IntVal);
			break;
		case Instruction::SDiv:
			R.IntVal = Src1.IntVal.sdiv(Src2.IntVal);
			break;
		case Instruction::URem:
			R.IntVal = Src1.IntVal.urem(Src2.IntVal);
			break;
		case Instruction::SRem:
			R.IntVal = Src1.IntVal.srem(Src2.IntVal);
			break;
		case Instruction::And:
			R.IntVal = Src1.IntVal & Src2.IntVal;
			break;
		case Instruction::Or:
			R.IntVal = Src1.IntVal | Src2.IntVal;
			break;
		case Instruction::Xor:
			R.IntVal = Src1.IntVal ^ Src2.IntVal;
			break;
		default:
			dbgs() << "Don't know how to handle this binary operator!\n-->" << I;
			llvm_unreachable(0);
	}

	SetValue(&I, R, SF);
}

static GenericValue executeSelectInst(GenericValue Src1, GenericValue Src2,
		GenericValue Src3)
{
	return Src1.IntVal == 0 ? Src3 : Src2;
}

void Interpreter::visitSelectInst(SelectInst &I)
{
	ExecutionContext &SF = ECStack->back();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Src3 = getOperandValue(I.getOperand(2), SF);
	GenericValue R = executeSelectInst(Src1, Src2, Src3);
	SetValue(&I, R, SF);
}


//===----------------------------------------------------------------------===//
//                     Terminator Instruction Implementations
//===----------------------------------------------------------------------===//

void Interpreter::exitCalled(GenericValue GV)
{
	// runAtExitHandlers() assumes there are no stack frames, but
	// if exit() was called, then it had a stack frame. Blow away
	// the stack before interpreting atexit handlers.
	ECStack->clear();
	runAtExitHandlers();
	exit(GV.IntVal.zextOrTrunc(32).getZExtValue());
}

/// Pop the last stack frame off of ECStack and then copy the result
/// back into the result variable if we are not returning void. The
/// result variable may be the ExitValue, or the Value of the calling
/// CallInst if there was a previous stack frame. This method may
/// invalidate any ECStack iterators you have. This method also takes
/// care of switching to the normal destination BB, if we are returning
/// from an invoke.
///
void Interpreter::popStackAndReturnValueToCaller(const Type *RetTy,
		GenericValue Result)
{
	// before popping the stack free the memory pointed to by Allocas
	ExecutionContext& SF = ECStack->back();
	freeAllocas(SF);
	// Pop the current stack frame.
	ECStack->pop_back();

	if (ECStack->empty())    // Finished main.  Put result into exit code...
	{
		if (RetTy && RetTy->isIntegerTy())            // Nonvoid return type?
		{
			ExitValue = Result;   // Capture the exit value of the program
		}
		else
		{
			memset(&ExitValue.Untyped, 0, sizeof(ExitValue.Untyped));
		}
	}
	else
	{
		// If we have a previous stack frame, and we have a previous call,
		// fill in the return value...
		ExecutionContext &CallingSF = ECStack->back();
		if (Instruction *I = CallingSF.Caller.getInstruction())
		{
			// Save result...
			if (!CallingSF.Caller.getType()->isVoidTy())
				SetValue(I, Result, CallingSF);
			if (InvokeInst *II = dyn_cast<InvokeInst> (I))
				SwitchToNewBasicBlock (II->getNormalDest (), CallingSF);
			CallingSF.Caller = CallSite();          // We returned from the call...
		}
	}
}

void Interpreter::visitReturnInst(ReturnInst &I)
{
	ExecutionContext &SF = ECStack->back();
	const Type *RetTy = Type::getVoidTy(I.getContext());
	GenericValue Result;

	// Save away the return value... (if we are not 'ret void')
	if (I.getNumOperands())
	{
		RetTy  = I.getReturnValue()->getType();
		Result = getOperandValue(I.getReturnValue(), SF);
	}

	history->RecordReturnEvent(RetTy, Result, SF.CurFunction, currThread);
	popStackAndReturnValueToCaller(RetTy, Result);
}

void Interpreter::visitUnwindInst(UnwindInst &I)
{
	// Unwind stack
	Instruction *Inst;
	do
	{
		ECStack->pop_back();
		if (ECStack->empty())
			llvm_report_error("Empty stack during unwind!");
		Inst = ECStack->back().Caller.getInstruction();
	}
	while (!(Inst && isa<InvokeInst>(Inst)));

	// Return from invoke
	ExecutionContext &InvokingSF = ECStack->back();
	InvokingSF.Caller = CallSite();

	// Go to exceptional destination BB of invoke instruction
	SwitchToNewBasicBlock(cast<InvokeInst>(Inst)->getUnwindDest(), InvokingSF);
}

void Interpreter::visitUnreachableInst(UnreachableInst &I)
{
	llvm_report_error("Program executed an 'unreachable' instruction!");
}

void Interpreter::visitBranchInst(BranchInst &I)
{
	ExecutionContext &SF = ECStack->back();
	BasicBlock *Dest;

	Dest = I.getSuccessor(0);          // Uncond branches have a fixed dest...
	if (!I.isUnconditional())
	{
		Value *Cond = I.getCondition();
		if (getOperandValue(Cond, SF).IntVal == 0) // If false cond...
			Dest = I.getSuccessor(1);
	}
	SwitchToNewBasicBlock(Dest, SF);
}

void Interpreter::visitSwitchInst(SwitchInst &I)
{
	ExecutionContext &SF = ECStack->back();
	GenericValue CondVal = getOperandValue(I.getOperand(0), SF);
	const Type *ElTy = I.getOperand(0)->getType();

	// Check to see if any of the cases match...
	BasicBlock *Dest = 0;
	for (unsigned i = 2, e = I.getNumOperands(); i != e; i += 2)
		if (executeICMP_EQ(CondVal, getOperandValue(I.getOperand(i), SF), ElTy)
				.IntVal != 0)
		{
			Dest = cast<BasicBlock>(I.getOperand(i+1));
			break;
		}

	if (!Dest) Dest = I.getDefaultDest();   // No cases matched: use default
	SwitchToNewBasicBlock(Dest, SF);
}

void Interpreter::visitIndirectBrInst(IndirectBrInst &I)
{
	ExecutionContext &SF = ECStack->back();
	void *Dest = GVTOP(getOperandValue(I.getAddress(), SF));
	SwitchToNewBasicBlock((BasicBlock*)Dest, SF);
}


// SwitchToNewBasicBlock - This method is used to jump to a new basic block.
// This function handles the actual updating of block and instruction iterators
// as well as execution of all of the PHI nodes in the destination block.
//
// This method does this because all of the PHI nodes must be executed
// atomically, reading their inputs before any of the results are updated.  Not
// doing this can cause problems if the PHI nodes depend on other PHI nodes for
// their inputs.  If the input PHI node is updated before it is read, incorrect
// results can happen.  Thus we use a two phase approach.
//
void Interpreter::SwitchToNewBasicBlock(BasicBlock *Dest, ExecutionContext &SF)
{
	BasicBlock *PrevBB = SF.CurBB;      // Remember where we came from...
	SF.CurBB   = Dest;                  // Update CurBB to branch destination
	SF.CurInst = SF.CurBB->begin();     // Update new instruction ptr...

	if (!isa<PHINode>(SF.CurInst)) return;  // Nothing fancy to do

	// Loop over all of the PHI nodes in the current block, reading their inputs.
	std::vector<GenericValue> ResultValues;

	for (; PHINode *PN = dyn_cast<PHINode>(SF.CurInst); ++SF.CurInst)
	{
		// Search for the value corresponding to this previous bb...
		int i = PN->getBasicBlockIndex(PrevBB);
		ASSERT(i != -1, "PHINode doesn't contain entry for predecessor??");
		Value *IncomingValue = PN->getIncomingValue(i);

		// Save the incoming value for this PHI node...
		ResultValues.push_back(getOperandValue(IncomingValue, SF));
	}

	// Now loop over all of the PHI nodes setting their values...
	SF.CurInst = SF.CurBB->begin();
	for (unsigned i = 0; isa<PHINode>(SF.CurInst); ++SF.CurInst, ++i)
	{
		PHINode *PN = cast<PHINode>(SF.CurInst);
		SetValue(PN, ResultValues[i], SF);
	}
}

//===----------------------------------------------------------------------===//
//                     Memory Instruction Implementations
//===----------------------------------------------------------------------===//

void Interpreter::visitAllocaInst(AllocaInst &I)
{
	ExecutionContext &SF = ECStack->back();

	const Type *Ty = I.getType()->getElementType();  // Type to be allocated

	// Get the number of elements being allocated by the array...
	unsigned NumElements =
		getOperandValue(I.getOperand(0), SF).IntVal.getZExtValue();

	unsigned TypeSize = (size_t)TD.getTypeAllocSize(Ty);

	// Avoid malloc-ing zero bytes, use max()...
	unsigned MemToAlloc = std::max(1U, NumElements * TypeSize);

	// Allocate enough memory to hold the type...
#if defined(VIRTUALMEMORY)
	void *nativeAddr = malloc(MemToAlloc);
	void *virtualAddr = (void *)nextVirtualAddress;
	nextVirtualAddress = nextVirtualAddress + (int)MemToAlloc;
	nextVirtualAddress += MEMDIFF;
	nextVirtualAddress = (size_t)makeAddressAlligned((void*)nextVirtualAddress);
	nativeToVirtual[nativeAddr] = virtualAddr;
	virtualToNative[virtualAddr] = nativeAddr;
#else
	void *virtualAddr = malloc(MemToAlloc);
#endif

	DEBUG(dbgs() << "Allocated Type: " << *Ty << " (" << TypeSize << " bytes) x "
			<< NumElements << " (Total: " << MemToAlloc << ") at "
			<< uintptr_t(virtualAddr) << '\n');

	GenericValue Result = PTOGV(virtualAddr);
	ASSERT(Result.PointerVal != 0, "Null pointer returned by malloc!");
	SetValue(&I, Result, SF);

	// changed to insert with number of bytes when on relaxed memory model
	if (I.getOpcode() == Instruction::Alloca) {
		//ECStack->back().Allocas.add(Memory);
		SF.Allocas.addWithSize(virtualAddr,MemToAlloc);
	}
}

// getElementOffset - The workhorse for getelementptr.
//
GenericValue Interpreter::executeGEPOperation(Value *Ptr, gep_type_iterator I,
		gep_type_iterator E,
		ExecutionContext &SF)
{
	ASSERT(Ptr->getType()->isPointerTy(),
			"Cannot getElementOffset of a nonpointer type!");

	uint64_t Total = 0;

	for (; I != E; ++I)
	{
		if (const StructType *STy = dyn_cast<StructType>(*I))
		{
			const StructLayout *SLO = TD.getStructLayout(STy);

			const ConstantInt *CPU = cast<ConstantInt>(I.getOperand());
			unsigned Index = unsigned(CPU->getZExtValue());

			Total += SLO->getElementOffset(Index);
		}
		else
		{
			const SequentialType *ST = cast<SequentialType>(*I);
			// Get the index number for the array... which must be long type...
			GenericValue IdxGV = getOperandValue(I.getOperand(), SF);

			int64_t Idx;
			unsigned BitWidth =
				cast<IntegerType>(I.getOperand()->getType())->getBitWidth();
			if (BitWidth == 32)
				Idx = (int64_t)(int32_t)IdxGV.IntVal.getZExtValue();
			else
			{
				ASSERT(BitWidth == 64, "Invalid index type for getelementptr");
				Idx = (int64_t)IdxGV.IntVal.getZExtValue();
			}
			Total += TD.getTypeAllocSize(ST->getElementType())*Idx;
		}
	}

	GenericValue Result;
	Result.PointerVal = ((char*)getOperandValue(Ptr, SF).PointerVal) + Total;
	DEBUG(dbgs() << "GEP Index " << Total << " bytes.\n");
	return Result;
}

void Interpreter::visitGetElementPtrInst(GetElementPtrInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeGEPOperation(I.getPointerOperand(),
				gep_type_begin(I), gep_type_end(I), SF), SF);
}

/* support store memory operation */
void Interpreter::visitStoreInstNoWmm(StoreInst &I) {
	ExecutionContext &SF = ECStack->back();
	GenericValue Val = getOperandValue(I.getOperand(0), SF);
#if defined(VIRTUALMEMORY)
	GenericValue virSRC = getOperandValue(I.getPointerOperand(), SF);
	GenericValue natSRC = virSRC;
	natSRC.PointerVal = getNativeAddressFull(virSRC.PointerVal,SF);
	StoreValueToMemory(Val, (GenericValue *)GVTOP(natSRC),
			I.getOperand(0)->getType());
	// logging read/write non-local accesses
	if (!isAddressOnStack(virSRC.PointerVal, SF)) {	
		rw_history->RecordRWEvent(virSRC, Val, currThread, WRITE, I.label_instr);
		instr_info.isSharedAccessing = true; // added
	}
#else
	GenericValue SRC = getOperandValue(I.getPointerOperand(), SF);
	StoreValueToMemory(Val, (GenericValue *)GVTOP(SRC),
			I.getOperand(0)->getType());
	// logging rw non-local accesses
	if (!isAddressOnStack(SRC.PointerVal, SF)) {	
		rw_history->RecordRWEvent(SRC, Val, currThread, WRITE, I.label_instr);
		instr_info.isSharedAccessing = true; // added
	}
#endif
	if (I.isVolatile() && PrintVolatile)
		dbgs() << "Volatile store: " << I;
}

void Interpreter::visitStoreInstTSO(StoreInst &I) {
	tso_buff_elem elem;
	ExecutionContext &SF = ECStack->back();
	elem.value = getOperandValue(I.getOperand(0), SF);         // Val
	elem.pointer = getOperandValue(I.getPointerOperand(), SF); // virSRC 
	elem.type = const_cast<Type*>(I.getOperand(0)->getType());
	
	// Add check if the address is local(on the stack)
	if(isAddressOnStack(elem.pointer.PointerVal,SF)) {
		// local variable. execute store instead of inserting in the buffer. 
		// must have the native address here
		GenericValue natAddr = elem.pointer;
#if defined(VIRTUALMEMORY)
		natAddr.PointerVal = getNativeAddressFull(elem.pointer.PointerVal,SF);
#endif
		StoreValueToMemory(elem.value, (GenericValue *)GVTOP(natAddr), elem.type);
		return;
	}

	thread_buffer_tso[currThread].push_back(elem);
	// logging read/write non-local accesses
	rw_history->RecordRWEvent(elem.pointer, elem.value, currThread, WRITE, I.label_instr);
	instr_info.isSharedAccessing = true; // added
	if (I.isVolatile() && PrintVolatile)
		dbgs() << "Volatile store: " << I;
}

void Interpreter::visitStoreInstPSO(StoreInst &I) {
	ExecutionContext &SF = ECStack->back();
	GenericValue Val = getOperandValue(I.getOperand(0), SF);
	GenericValue virSRC = getOperandValue(I.getPointerOperand(), SF); 

	if(isAddressOnStack(virSRC.PointerVal,SF)) {
		GenericValue natSRC = virSRC;
#if defined(VIRTUALMEMORY)
		natSRC.PointerVal = getNativeAddressFull(virSRC.PointerVal, SF);
#endif
		StoreValueToMemory(Val, (GenericValue *)GVTOP(natSRC), I.getOperand(0)->getType());
		return;
	}

	pso_types[getOperandValue(I.getPointerOperand(), SF)] = const_cast<Type*>(I.getOperand(0)->getType());
	thread_buffer_pso[currThread][getOperandValue(I.getPointerOperand(), SF)].push_back(getOperandValue(I.getOperand(0), SF));
	// logging read/write non-local accesses
	rw_history->RecordRWEvent(virSRC, Val, currThread, WRITE, I.label_instr);
	instr_info.isSharedAccessing = true; // added
}

void Interpreter::visitStoreInst(StoreInst &I) {
	instr_info.isWriteOrRead = false; // added: this is a write instruction
	if (Params::WMM == WMM_NONE)	
		visitStoreInstNoWmm(I);
	else if (Params::WMM == WMM_TSO)
		visitStoreInstTSO(I);
	else if (Params::WMM == WMM_PSO)
		visitStoreInstPSO(I);
	else {
		ASSERT(false, "Failed to invoke visitStoreInst");
	}
}

/* support load memory operation */
/*
GenericValue Interpreter::loadValueNoWmm(void *virAddr, const Type* type) {
#if defined(VIRTUALMEMORY)
	ExecutionContext &SF = ECStack->back();
	GenericValue virSRC;
	virSRC.PointerVal = virAddr;
	GenericValue natSRC = virSRC;
	natSRC.PointerVal = getNativeAddressFull(virSRC.PointerVal, SF);
	GenericValue *natPtr = (GenericValue*)GVTOP(natSRC);
	GenericValue Result;
	LoadValueFromMemory(Result, natPtr, type);
#else
	GenericValue SRC;
	SRC.PointerVal = virAddr;
	GenericValue *Ptr = (GenericValue*)GVTOP(SRC);
	GenericValue Result;
	LoadValueFromMemory(Result, Ptr, type);
#endif
	return Result;
}
*/
void Interpreter::visitLoadInstNoWmm(LoadInst &I) {
	ExecutionContext &SF = ECStack->back();
#if defined(VIRTUALMEMORY)
	GenericValue virSRC = getOperandValue(I.getPointerOperand(), SF);
	GenericValue natSRC = virSRC;
	natSRC.PointerVal = getNativeAddressFull(virSRC.PointerVal, SF);
	GenericValue *natPtr = (GenericValue*)GVTOP(natSRC);
	GenericValue Result;
	LoadValueFromMemory(Result, natPtr, I.getType());
	// logging read/write non-local accesses
	if (!isAddressOnStack(virSRC.PointerVal, SF)) {
		rw_history->RecordRWEvent(virSRC, Result, currThread, READ, I.label_instr);
		instr_info.isSharedAccessing = true; //added
	}
#else
	GenericValue SRC = getOperandValue(I.getPointerOperand(), SF);
	GenericValue *Ptr = (GenericValue*)GVTOP(SRC);
	GenericValue Result;
	LoadValueFromMemory(Result, Ptr, I.getType());
	// logging read/write non-local accesses
	if (!isAddressOnStack(SRC.PointerVal, SF)) {
		rw_history->RecordRWEvent(SRC, Result, currThread, READ, I.label_instr);
		instr_info.isSharedAccessing = true; //added
	}
#endif
	SetValue(&I, Result, SF);
	if (I.isVolatile() && PrintVolatile)
		dbgs() << "Volatile load " << I;
}
/*
GenericValue Interpreter::loadValueTSO(void *virAddr, const Type *type) {
	GenericValue virSRC;
	virSRC.PointerVal = virAddr;
	std::list<tso_buff_elem>::reverse_iterator it = thread_buffer_tso[currThread].rbegin();
	GenericValue Result;

	bool fl = false;
	for ( ; it != thread_buffer_tso[currThread].rend(); ++it) {
		if (it->pointer.PointerVal == virSRC.PointerVal) {
			Result = it->value;
			fl = true;
			break;
		}
	}
	if (!fl) {
		GenericValue natSRC = virSRC;
#if defined(VIRTUALMEMORY)
		ExecutionContext &SF = ECStack->back();
		natSRC.PointerVal = getNativeAddressFull(virSRC.PointerVal, SF);
#endif
		GenericValue *Ptr = (GenericValue*)GVTOP(natSRC);
		LoadValueFromMemory(Result, Ptr, type);
	}
	return Result;
}
*/
void Interpreter::visitLoadInstTSO(LoadInst &I) {
	ExecutionContext &SF = ECStack->back();
	GenericValue virSRC = getOperandValue(I.getPointerOperand(), SF);
	std::list<tso_buff_elem>::reverse_iterator it = thread_buffer_tso[currThread].rbegin();
	GenericValue Result;

	bool fl = false;
	for ( ; it != thread_buffer_tso[currThread].rend(); ++it) {
		if (it->pointer.PointerVal == virSRC.PointerVal) {
			Result = it->value;
			SetValue(&I, Result, SF);
			fl = true;
			rw_history->RecordRWEvent(virSRC, Result, currThread, READ, I.label_instr);
			break;
		}
	}
	if (!fl) {
		GenericValue natSRC = virSRC;
#if defined(VIRTUALMEMORY)
		natSRC.PointerVal = getNativeAddressFull(virSRC.PointerVal, SF);
#endif
		GenericValue *Ptr = (GenericValue*)GVTOP(natSRC);
		LoadValueFromMemory(Result, Ptr, I.getType());
		// logging read/write non-local accesses
		if (!isAddressOnStack(virSRC.PointerVal,SF)) {
			rw_history->RecordRWEvent(virSRC, Result, currThread, READ, I.label_instr);
			instr_info.isSharedAccessing = true; //added
		}
		SetValue(&I, Result, SF);
	}
	if (I.isVolatile() && PrintVolatile)
		dbgs() << "Volatile load " << I;
}
/*
GenericValue Interpreter::loadValuePSO(void *virAddr, const Type *type) {
	GenericValue Result;
	GenericValue virSRC;
	virSRC.PointerVal = virAddr;
	if (!thread_buffer_pso[currThread][virSRC].empty()) {
		Result = thread_buffer_pso[currThread][virSRC].back();
		return Result;
	} else {
		GenericValue natSRC = virSRC;
#if defined(VIRTUALMEMORY)
		ExecutionContext &SF = ECStack->back();
		natSRC.PointerVal = getNativeAddressFull(virSRC.PointerVal, SF);
#endif
		GenericValue *Ptr = (GenericValue*)GVTOP(natSRC);
		LoadValueFromMemory(Result, Ptr, type);
		return Result;
	}
}
*/

void Interpreter::visitLoadInstPSO(LoadInst &I) {
	ExecutionContext &SF = ECStack->back();
	GenericValue Result;

	if (!thread_buffer_pso[currThread][getOperandValue(I.getPointerOperand(), SF)].empty()) {
		Result = thread_buffer_pso[currThread][getOperandValue(I.getPointerOperand(), SF)].back();
		// logging read/write non-local accesses
		if (!isAddressOnStack(getOperandValue(I.getPointerOperand(), SF).PointerVal,SF)) {

#if defined(VIRTUALMEMORY) 
	// added to track segment fault
	void *virtualAddr = getOperandValue(I.getPointerOperand(),SF).PointerVal;
	void *virtualBase = getVirtualBaseAddressHeap(virtualAddr);
	if (virtualBase == NULL) {
		segmentFaultFlag = true;
		return;
	}
#else
	// added to track segment fault
	void *virtualAddr = getOperandValue(I.getPointerOperand(),SF).PointerVal;
	void *virtualBase = getPhysicalBaseAddressHeap(virtualAddr);
	if (virtualBase == NULL) {
		segmentFaultFlag = true;
		return;
	}
#endif

			rw_history->RecordRWEvent(getOperandValue(I.getPointerOperand(), SF), Result, currThread, READ, I.label_instr);
			instr_info.isSharedAccessing = true; // added
		}
		SetValue(&I, Result, SF);
	} else {
		GenericValue virSRC = getOperandValue(I.getPointerOperand(), SF);
		GenericValue natSRC = virSRC;
#if defined(VIRTUALMEMORY)
		natSRC.PointerVal = getNativeAddressFull(virSRC.PointerVal, SF);
#else 
#endif
		// logging read/write non-local accesses		
		if (!isAddressOnStack(virSRC.PointerVal,SF)) {

#if defined(VIRTUALMEMORY) 
	// added to track segment fault
	void *virtualAddr = getOperandValue(I.getPointerOperand(),SF).PointerVal;
	void *virtualBase = getVirtualBaseAddressHeap(virtualAddr);
	if (virtualBase == NULL) {
		segmentFaultFlag = true;
		return;
	}
#else
	// added to track segment fault
	void *virtualAddr = getOperandValue(I.getPointerOperand(),SF).PointerVal;
	void *virtualBase = getPhysicalBaseAddressHeap(virtualAddr);
	if (virtualBase == NULL) {
		segmentFaultFlag = true;
		return;
	}
#endif

			rw_history->RecordRWEvent(virSRC, Result, currThread, READ, I.label_instr);
			instr_info.isSharedAccessing = true; // added
		}

		GenericValue *Ptr = (GenericValue*)GVTOP(natSRC);
		LoadValueFromMemory(Result, Ptr, I.getType());

		SetValue(&I, Result, SF);
	}
}

void Interpreter::visitLoadInst(LoadInst &I) {
	instr_info.isWriteOrRead = true; // this is a read instruction
	if (Params::WMM == WMM_NONE)	
		visitLoadInstNoWmm(I);
	else if (Params::WMM == WMM_TSO)
		visitLoadInstTSO(I);
	else if (Params::WMM == WMM_PSO)
		visitLoadInstPSO(I);
	else {
		ASSERT(false, "Failed to invoke visitStoreInst");
	}
}

/* support flush buffer and fence instructions */
void Interpreter::flush_buffer_pso(Thread t, GenericValue p) {
	if (!thread_buffer_pso[t][p].empty()) {
		GenericValue v = thread_buffer_pso[t][p].front();
		thread_buffer_pso[t][p].pop_front();
		GenericValue native = p;
#if defined(VIRTUALMEMORY)
		native.PointerVal = getNativeAddressGlobal(p.PointerVal);
#endif

#if defined(VIRTUALMEMORY) 
	// added to track segment fault
	void *virtualAddr = native.PointerVal;
	void *virtualBase = getVirtualBaseAddressHeap(virtualAddr);
	if (virtualBase == NULL) {
		segmentFaultFlag = true;
		return;
	}
#else
	// added to track segment fault
	void *virtualAddr = native.PointerVal;
	void *virtualBase = getPhysicalBaseAddressHeap(virtualAddr);
	if (virtualBase == NULL) {
		segmentFaultFlag = true;
		return;
	}
#endif

		StoreValueToMemory(v, (GenericValue *)GVTOP(native), pso_types[p]);
	}
}

void Interpreter::flush_buffer_tso(Thread t) {
	if (!thread_buffer_tso[t].empty()) {
		tso_buff_elem elem = thread_buffer_tso[t].front();
		GenericValue Val;
		GenericValue virSRC;
		Type* type;

		thread_buffer_tso[t].pop_front();
		Val = elem.value;
		virSRC = elem.pointer;
		type = elem.type;
		GenericValue natSRC = virSRC;
#if defined(VIRTUALMEMORY)
		natSRC.PointerVal = getNativeAddressGlobal(virSRC.PointerVal);
#endif
		StoreValueToMemory(Val, (GenericValue *)GVTOP(natSRC), type);
	}
}

void Interpreter::membar_ss(Thread t) {

	if (Params::WMM == WMM_NONE) {
		printf("warning: membar_ss has no effect on an SC WMM.\n");
	} else if (Params::WMM == WMM_TSO) {
		printf("warning: membar_ss has no effect on TSO WMM.\n");
	} else if (Params::WMM == WMM_PSO) {
		std::map<GenericValue, std::list<GenericValue> >::iterator mit;
		mit = thread_buffer_pso[t].begin();
		for ( ; mit != thread_buffer_pso[t].end(); ++mit) {
			while (!mit->second.empty()) {
				flush_buffer_pso(t, mit->first);
			}
		}
	}
	rw_history->RecordRWEvent(t, FLUSH_FENCE, 0); // added
}

void Interpreter::membar_sl(Thread t) {
	if (Params::WMM == WMM_NONE) {
		printf("warning: membar_sl has no effect on an SC WMM.\n");
	} else if (Params::WMM == WMM_TSO) {
		while (!thread_buffer_tso[t].empty()) {
			flush_buffer_tso(t);
		}
	} else if (Params::WMM == WMM_PSO) {
		std::map<GenericValue, std::list<GenericValue> >::iterator mit;
		mit = thread_buffer_pso[t].begin();
		for ( ; mit != thread_buffer_pso[t].end(); ++mit) {
			while (!mit->second.empty()) {
				flush_buffer_pso(t, mit->first);
			}
		}
	}
	// record the flush operation
	rw_history->RecordRWEvent(t, FLUSH_FENCE, 0); // added
}

/* support multithreads */
void Interpreter::visitSpawnThread(ExecutionContext &SF) {
	// Create new thread and load the initial stack frame for it
	ASSERT(SF.Caller.arg_size() == 1, "spawn_thread must have exactly one argument"); // simple and not enough check for the parameters
	Value *V = *SF.Caller.arg_begin(); // get the first parameter
	ASSERT(V->getType()->isPointerTy(), "spawn_thread must accept pointer type");
	GenericValue Arg = getOperandValue(V,SF); // this code gets the value of the argument. The argument is the address of the function that the new thread will execute
	createThread(Arg); // create the thread itself
	history->RecordFirstEvent();
	// Caller is use when returning from function and poping the stack, we need to know who called the function and eventually return value
	// for fork it is created, but since we're not really calling function the next line just reset it
	// don't think it is a problem if the next line is absent
	SF.Caller = CallSite();

	// record a sync instruction
	rw_history->RecordRWEvent(currThread, SPAWN, 0);
}

void Interpreter::visitAssert(ExecutionContext &SF) {
	// optionally pass a second parameter to assert (can be made nicer).
	ASSERT(SF.Caller.arg_size() <= 2 && SF.Caller.arg_size() > 0, "assert should have 1-2 arguments");

	CallSite::arg_iterator iter = SF.Caller.arg_begin();
	Value* assval = *iter; //The value we're asserting on
	ASSERT(assval->getType()->isIntegerTy(), "First parameter to assert must be an integer");
	int assertPassed = getOperandValue(assval, SF).IntVal.getLimitedValue();
	// if assert fails, do something more drastic)
	if (assertPassed == 0) {
		// Second parameter should be a pointer to a string.
		iter++;
		if (iter != SF.Caller.arg_end()) {
			Value *strval = *iter;
			ASSERT(strval->getType()->isPointerTy(), "Second parameter to assert must be a pointer (to char)");
			GenericValue arg = getOperandValue(strval,SF);

			// Since this is a pointer to char, convert to native if required...
#if defined(VIRTUALMEMORY)
			void *virAddr = (void *)arg.PointerVal;
			char*assError = (char *)getNativeAddressFull(virAddr,SF);
#else
			char* assError = (char *)arg.PointerVal;
#endif
			cout << "Assert failed: " << assError << endl;
		} else {
			cout << "Assert failed!" << endl;
		}
	}
	// If assert passes, it is a no-op.
	SF.Caller = CallSite();
}


void Interpreter::visitAssertExist(ExecutionContext &SF) {

	allonAssertExist = true;

	ASSERT(SF.Caller.arg_size() == 3, "not the right number of parameters for assert_exist");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V, SF);

#if defined(VIRTUALMEMORY)
	void *virAddr = arg.PointerVal;
	int *natAddr = (int *)getNativeAddressFull(virAddr,SF);
#else
	int *natAddr = (int *)arg.PointerVal;
#endif
	ASSERT(natAddr, "This address can not be 0");

	++ait;
	V = *ait;
	arg = getOperandValue(V, SF);	
	size_t length = (size_t)arg.IntVal.getLimitedValue();
	++ait;
	V = *ait;
	arg = getOperandValue(V, SF);	
	int val = (int)arg.IntVal.getLimitedValue();

	bool flag = false;
	for (int i = 0; i < length; i++) {
		if (natAddr[i] == val) {
			flag = true;
			break;
		}
	}
	if (!flag) {
		segmentFaultFlag = true;
	}

	SF.Caller = CallSite();
}

void Interpreter::visitJoinAll(ExecutionContext &SF) {
	// This should be fixed at some point. The right thing to do would be not to visit the instruction
	//at all. However, since we don't want to mess up the rest of the code, what this is going
	//to do for now is roll back the instruction pointer.
	ASSERT(SF.Caller.arg_size() == 0, "join_all should have no arguments");
	//Count how many stacks are non-empty. If > 1, then unroll.
	int liveThreads = 0;
	std::map<Thread, std::vector<ExecutionContext> >::iterator it;
	for(it = threadStacks.begin(); it != threadStacks.end(); ++it) {
		if(!it->second.empty())
			liveThreads++;
	}
	if (liveThreads > 1) {
		instr_info.isBlocked = true; // this thread is blocked
		SF.CurInst--;
	}
	SF.Caller = CallSite();

	// record a sync instruction
	rw_history->RecordRWEvent(currThread, JOIN, 0);
}

/* Works only with integers. */
void Interpreter::visitCAS(ExecutionContext &SF, int inst) {

	ASSERT(SF.Caller.arg_size()==3, "not the right number of parameters for cas32");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	ASSERT(V->getType()->isPointerTy(), "first argument of cas32 must be pointer");
	GenericValue arg1 = getOperandValue(V,SF); // first, address?

	/* TSO flush the only buffer, but PSO only flush the buffert with that address */
	if (Params::WMM == WMM_TSO) {
		while (!thread_buffer_tso[currThread].empty()) {
			flush_buffer_tso(currThread);
		}
	}	else if (Params::WMM == WMM_PSO)	{
		while (!thread_buffer_pso[currThread][arg1].empty()) {
			flush_buffer_pso(currThread, arg1);
		}
	}

#if defined(VIRTUALMEMORY)
	int *virAddr = (int *)arg1.PointerVal;
	int *natAddr = (int *)getNativeAddressFull((void *)virAddr,SF);

	// added to track segment fault
	void *virtualBase = getVirtualBaseAddressHeap(virAddr);
	if (virtualBase == NULL) {
		segmentFaultFlag = true;
		return;
	}
#else
	int *natAddr = (int *)arg1.PointerVal;
	void *virtualBase = getPhysicalBaseAddressHeap(natAddr);
	if (virtualBase == NULL) {
		segmentFaultFlag = true;
		return;
	}
#endif

	++ait;
	V = *ait;

	if (inst == CAS32) {
		ASSERT(V->getType()->isIntegerTy(32), "second argument of cas32 must be integer");
	} else if (inst == CASIO) {
		ASSERT(V->getType()->isIntegerTy(), "second argument of casio must be integer");
	}

	GenericValue arg2 = getOperandValue(V,SF); // second, integer;
	int x = arg2.IntVal.getLimitedValue();
	++ait;
	V = *ait;

	if (inst == CAS32) {
		ASSERT(V->getType()->isIntegerTy(32), "third argument of cas32 must be integer");
	} else if (inst == CASIO) {
		ASSERT(V->getType()->isIntegerTy(), "third argument of casio must be integer");
	}

	GenericValue arg3 = getOperandValue(V,SF); // third, integer;
	int y = arg3.IntVal.getLimitedValue();

	int ret;

	GenericValue val;
	val.IntVal = *natAddr;

	if (inst == CAS32) {
		ret = 0;
		if (*natAddr == x) {
			*natAddr = y;
			ret = 1;
			if (Params::WMM == WMM_TSO) {
				rw_history->RecordRWEvent(arg1, arg3, currThread, WRITE, 0);
			} else if (Params::WMM == WMM_PSO) {
				//Instruction* I = SF.CurInst;
				Instruction* I = SF.Caller.getInstruction();
				rw_history->RecordRWEvent(arg1, arg3, currThread, WRITE, I->label_instr);
			}
		}
	} else if (inst == CASIO) {
		ret = *natAddr;
		if( *natAddr == x) {
			*natAddr = y;
			assert(0 && "CASIO is not implemented correctly!");
		}
	}
	
	if (Params::WMM == WMM_TSO) {
		rw_history->RecordRWEvent(currThread, FLUSH_CAS_TSO, 0);
	} else if (Params::WMM == WMM_PSO) {
		rw_history->RecordRWEvent(arg1, currThread, FLUSH_CAS_PSO, -1);
	}

	// This code is to return the value to whoever called cas32
	if ( Instruction* I = SF.Caller.getInstruction()) {
		GenericValue Result;
		Result.IntVal = APInt::getNullValue(32);
		Result.IntVal = Result.IntVal + ret;// this is not correct for cas
		SetValue(I,Result,SF);
		SF.Caller = CallSite();
	}
	SF.Caller = CallSite();
}

// we did not implement rwrecord in this and the following several functions
void Interpreter::visitCASPO(ExecutionContext &SF)
{
	// TODO: Should this only be called for TSO ?
	membar_sl(currThread);

	ASSERT(SF.Caller.arg_size()==3, "not the right number of parameters for caspo");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	ASSERT(V->getType()->isPointerTy(), "first argument of caspo must be pointer");
	GenericValue arg = getOperandValue(V,SF);

	if (Params::WMM == WMM_PSO)	{
		while (!thread_buffer_pso[currThread][arg].empty()) {
			flush_buffer_pso(currThread, arg);
		}	
	}

#if defined(VIRTUALMEMORY)
	void *virAddr = arg.PointerVal;
	void *natAddr = getNativeAddressFull(virAddr, SF);
#else
	void *natAddr = arg.PointerVal;
#endif
	++ait;
	V = *ait;
	ASSERT(V->getType()->isPointerTy(), "second argument of caspo must be pointer");
	arg = getOperandValue(V,SF);
	void* x = arg.PointerVal;

	++ait;
	V = *ait;
	ASSERT(V->getType()->isPointerTy(), "third argument of caspo must be pointer");
	arg = getOperandValue(V,SF);
	void* y = arg.PointerVal;

	void* ret = *((void **)natAddr);
	if( ret == x )
	{
		*((void **)natAddr) = y;
	}
	if( Instruction* I = SF.Caller.getInstruction() )
	{
		GenericValue Result;
		Result.PointerVal = ret;
		SetValue(I,Result,SF);
		SF.Caller = CallSite();
	}
	SF.Caller = CallSite();
}

void Interpreter::visitFASIO(ExecutionContext &SF) {

	SF.Caller = CallSite();

	if (Params::WMM == WMM_NONE)
		return;

	ASSERT(0, "fasio unsupported under TSO or PSO");
}

void Interpreter::visitFASPO(ExecutionContext &SF) {

	if (Params::WMM == WMM_TSO)
		membar_sl(currThread);

	ASSERT(SF.Caller.arg_size() == 2, "not the right number of parameters for faspo");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	ASSERT(V->getType()->isPointerTy(), "first argument of faspo must be pointer");
	GenericValue arg = getOperandValue(V,SF);
#if defined(VIRTUALMEMORY)
	void *virAddr = arg.PointerVal;
	void *natAddr = getNativeAddressFull(virAddr, SF);
#else
	void *natAddr = arg.PointerVal;
#endif
	++ait;
	V = *ait;
	ASSERT(V->getType()->isPointerTy(), "second argument of faspo must be pointer");
	arg = getOperandValue(V,SF);
	void* x = arg.PointerVal;

	void* ret = *((void **)natAddr);
	*((void **)natAddr) = x;
	if( Instruction* I = SF.Caller.getInstruction() )
	{
		GenericValue Result;
		Result.PointerVal = ret;
		SetValue(I,Result,SF);
		SF.Caller = CallSite();
	}
	SF.Caller = CallSite();
}

void Interpreter::visitMalloc(ExecutionContext &SF) {

	// intercept for malloc. The implementation is similar to the previous function intercepts
	// runs the real malloc
	ASSERT(SF.Caller.arg_size() == 1, "not the right number of parameters for malloc");
	Value *V = *SF.Caller.arg_begin();
	ASSERT(V->getType()->isIntegerTy(), "malloc must receive integer as input");
	GenericValue arg = getOperandValue(V, SF);
	int numBytes = arg.IntVal.getLimitedValue();
	// dealing with second layer of memory addressing
#if defined(VIRTUALMEMORY)
	void *nativeAddr = malloc(numBytes);
	void *virtualAddr = (void*)nextVirtualAddress;
	bytesAtVirtualAddress[virtualAddr] = numBytes;
	nextVirtualAddress = nextVirtualAddress + numBytes;
	nextVirtualAddress += MEMDIFF;
	nativeToVirtual[nativeAddr] = virtualAddr;
	virtualToNative[virtualAddr] = nativeAddr;
#else
	void *virtualAddr = malloc(numBytes);
	bytesAtPhysicalAddress[virtualAddr] = numBytes;
#endif

	//globalAddresses.push_back( std::pair<int,int> ( (int)virtualAddr, (int)virtualAddr + numBytes ) );
	if (Instruction* I = SF.Caller.getInstruction()) {
		GenericValue Result;
		Result.PointerVal = virtualAddr;
		SetValue(I, Result, SF);
	}
	SF.Caller = CallSite();
}

void Interpreter::visitFree(ExecutionContext &SF) {

	ASSERT(SF.Caller.arg_size()==1,  "not the right number of parameters for free");
	Value *V = *SF.Caller.arg_begin();
	ASSERT(V->getType()->isPointerTy(), "argument to free must be pointer");
	GenericValue arg = getOperandValue(V,SF);
#if defined(VIRTUALMEMORY)
	void *virtualAddr = arg.PointerVal;
	void *virtualBase = getVirtualBaseAddressHeap(virtualAddr);

	if (virtualBase == NULL) {
		ASSERT(0, "pointer for free is out-of-bounds");
	}

	if (virtualBase != virtualAddr) {
		ASSERT(0, "pointer for free is not base pointer");
	}

	void *nativeAddr = virtualToNative[virtualBase];
	free(nativeAddr);
	bytesAtVirtualAddress[virtualBase] = 0;
#else
	void *virtualAddr = arg.PointerVal;
	void *virtualBase = getPhysicalBaseAddressHeap(virtualAddr);
	if (virtualBase == NULL) {
		ASSERT(0, "pointer for free is out-of-bounds");
	}
	if (virtualBase != virtualAddr) {
		ASSERT(0, "pointer for free is not base pointer");
	}
	free(virtualBase);
	bytesAtPhysicalAddress[virtualBase] = 0;
#endif

	SF.Caller = CallSite();
}

void Interpreter::visitMemset(ExecutionContext &SF) {

// see malloc
	ASSERT(SF.Caller.arg_size() == 3, "not the right number of parameters for memset");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V,SF);
#if defined(VIRTUALMEMORY)
	void *virPtr = arg.PointerVal;
	void *natPtr = getNativeAddressFull(virPtr,SF);
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	int value = arg.IntVal.getLimitedValue();
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	int size = arg.IntVal.getLimitedValue();
	natPtr = memset(natPtr,value,size);
#else
	void *virPtr = arg.PointerVal;
	++ait;
	V = *ait;
	arg = getOperandValue(V, SF);
	int value = arg.IntVal.getLimitedValue();
	++ait;
	V = *ait;
	arg = getOperandValue(V, SF);
	int size = arg.IntVal.getLimitedValue();
	virPtr = memset(virPtr, value, size);
#endif
	if (Instruction *I = SF.Caller.getInstruction()) {
		GenericValue Result;
		Result.PointerVal = virPtr;
		SetValue(I,Result,SF);
		SF.Caller = CallSite();
	}
	SF.Caller = CallSite();
}

void Interpreter::visitMemcpy(ExecutionContext &SF, CallSite CS) {
	ASSERT(SF.Caller.arg_size()==3, "not the right number of parameters for memcpy");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V,SF);
	GenericValue rw_history_dst = arg; // added 
	void *virDest = arg.PointerVal;
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	GenericValue rw_history_src = arg; // added 
	void *virSrc = arg.PointerVal;
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	int size = arg.IntVal.getLimitedValue();
	void *natDest, *natSrc;
#if defined(VIRTUALMEMORY)
	natDest = getNativeAddressFull(virDest,SF);
	natSrc = getNativeAddressFull(virSrc,SF);;
#else
	natDest = virDest;
	natSrc = virSrc;
#endif
	if (Params::WMM == WMM_NONE) {
		memcpy(natDest, natSrc, size);
	}
	else if (Params::WMM == WMM_TSO) {
		// for heap locations, do not perform the memcpy immediately.
		//Rather, put every byte on the buffer individually. This makes the copy
		//non-atomic. Note that for TSO the flush order is consistent (beginning-to-end)
		//while for PSO it is non-deterministic.
		//Should it be non-deterministic for TSO? 
		if(isAddressOnStack(virDest,SF))
		{
			memcpy(natDest, natSrc, size);
		}
		else
		{
			int offset = 0;
			typedef vector<pair<GenericValue, GenericValue> > write_elem_t;
			write_elem_t write_buffer;

			while (offset < size)
			{
				tso_buff_elem elem;
				GenericValue gv;
				std::list<tso_buff_elem>::reverse_iterator it = thread_buffer_tso[currThread].rbegin();

				bool fl = false;
				//Default granularity is 4 bytes, but if there's a type of a different
				//size on the buffer, then advance by that much. At least, in theory.
				int toadd = 4;
				for ( ; it != thread_buffer_tso[currThread].rend(); ++it)
				{
					if (it->pointer.PointerVal == (char *)virSrc + offset)
					{
						elem.value = it->value;
						elem.type = it->type;
						toadd = getTargetData()->getTypeStoreSize(elem.type);
						//In practice however, we want this to be 4!)
						ASSERT(toadd == 4, "Unalgined type is on the buffer");
						fl = true;
						break;
					}
				}

				if (!fl)
				{
					//If not found in the buffer, take from global memory.
					//We assume what's stored there is a 32-bit int, and that's our granularity.
					elem.value.IntVal = APInt(32, *(int*)((char*)natSrc + offset));
					elem.type = (Type*)Type::getInt32Ty(CS.getInstruction()->getContext());
				}
				elem.pointer = GenericValue((char *)virDest + offset);
				thread_buffer_tso[currThread].push_back(elem);

				// Not sure whether we need this or not
				Instruction* I = SF.Caller.getInstruction();
				rw_history->RecordRWEvent(rw_history_src, elem.value, currThread, READ, I->label_instr);
				write_buffer.push_back(pair<GenericValue, GenericValue>(rw_history_dst, elem.value));

				offset += toadd;
			}
		
			Instruction* I = SF.Caller.getInstruction();
			write_elem_t::iterator wb_iter;	
			for (write_elem_t::iterator wb_iter = write_buffer.begin(); 
				wb_iter != write_buffer.end(); wb_iter++) {	
				rw_history->RecordRWEvent(wb_iter->first, wb_iter->second, currThread, WRITE, I->label_instr);
			}
		}
	}
	else if (Params::WMM == WMM_PSO) {

		if(isAddressOnStack(virDest,SF))
		{
			memcpy(natDest, natSrc, size);
		}
		else
		{
			int offset = 0;
			typedef vector<pair<GenericValue, GenericValue> > write_elem_t;
			write_elem_t write_buffer;

			while (offset < size)
			{
				GenericValue gv;
				const Type* storeType;
				if (thread_buffer_pso[currThread][GenericValue((char*)virSrc + offset)].empty())
				{
					// Source buffer is empty, read from local memory
					gv.IntVal = APInt(32, *(((char*)natSrc) + offset));
					storeType = Type::getInt32Ty(CS.getInstruction()->getContext());
				}
				else
				{
					// Source buffer is not empty, read from it.
					gv = thread_buffer_pso[currThread][GenericValue((char*)virSrc + offset)].back();
					storeType = pso_types[GenericValue((char*)virSrc + offset)];
				}

				// Now, how about the dest buffer?
				if (thread_buffer_pso[currThread][GenericValue((char*)virDest + offset)].empty())
				{
					// Empty, no constraints!
					pso_types[GenericValue(((char*)virDest + offset))] = (Type*)storeType;
				}
				else
				{
					// message in the assert added
					ASSERT(pso_types[GenericValue(((char*)virDest + offset))] == storeType, "Execution.cpp: visitMemCpy");
				}
				// type check passed, we can add the value...
				thread_buffer_pso[currThread][GenericValue(((char*)virDest + offset))].push_back(gv);
				ASSERT(getTargetData()->getTypeStoreSize(storeType) == 4, "Unalgined type is on the buffer!");
	
				// not sure whether we need this or not
				Instruction* I = SF.Caller.getInstruction();
				rw_history->RecordRWEvent(rw_history_src, gv, currThread, READ, I->label_instr);
				write_buffer.push_back(pair<GenericValue, GenericValue>(rw_history_dst, gv));

				offset += getTargetData()->getTypeStoreSize(storeType);
			}

			Instruction* I = SF.Caller.getInstruction();
			write_elem_t::iterator wb_iter;	
			for (write_elem_t::iterator wb_iter = write_buffer.begin(); 
				wb_iter != write_buffer.end(); wb_iter++) {	
				rw_history->RecordRWEvent(wb_iter->first, wb_iter->second, currThread, WRITE, I->label_instr);
			}

		}
	}
	else {
		ASSERT(false, "No memory model defined!");
	}

	if (Instruction *I = SF.Caller.getInstruction()) {
		GenericValue Result;
		Result.PointerVal = virDest;
		SetValue(I,Result,SF);
		SF.Caller = CallSite();
	}
	SF.Caller = CallSite();
}

void Interpreter::visitNprintString(ExecutionContext &SF) {

	// print a single string represented as char *;
	ASSERT(SF.Caller.arg_size()==1, "not the right number of parameters for nprint_string");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V,SF);
#if defined(VIRTUALMEMORY)
	void *virAddr = arg.PointerVal;
	void *natAddr = getNativeAddressFull(virAddr,SF);
	printf( (char *)natAddr );
#else
	void *virAddr = arg.PointerVal;
	printf( (char *)virAddr );
#endif
	SF.Caller = CallSite();
}

void Interpreter::visitNprintInt(ExecutionContext &SF) {

	ASSERT(SF.Caller.arg_size()== 2, "not the right number of parameters for nprint_int");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V,SF);
#if defined(VIRTUALMEMORY)
	void *virAddr = arg.PointerVal;
	void *natAddr = getNativeAddressFull(virAddr,SF);
#else
	void *natAddr = arg.PointerVal;
#endif
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	printf( (char*)natAddr, arg.IntVal.getLimitedValue() );
	SF.Caller = CallSite();
}

void Interpreter::visitGetEnv(ExecutionContext &SF) {

	ASSERT(SF.Caller.arg_size() == 1, "not the right number of parameters for getenv");
	Value *V = *SF.Caller.arg_begin();
	GenericValue arg = getOperandValue(V, SF);
#if defined(VIRTUALMEMORY)
	char *virEnvr = (char*)arg.PointerVal;
	char *natEnvr = (char *)( getNativeAddressFull((void*)virEnvr,SF) );
	char *natRes = getenv(natEnvr);
	char *virRes;
	if(natRes != NULL) {
		virRes = (char*)( (void*)nextVirtualAddress );
		nextVirtualAddress += strlen(natRes);
		nextVirtualAddress += MEMDIFF;
		virtualToNative[virRes] = natRes;
		nativeToVirtual[natRes] = virRes;
		bytesAtVirtualAddress[virRes] = strlen(natRes);
	}
	else {
		virRes = NULL;
	}
#else
	char *virEnvr = (char*)arg.PointerVal;
	char *virRes = getenv(virEnvr);
	if(virRes != NULL) {
		bytesAtPhysicalAddress[virRes] = strlen(virRes);
	} 
#endif
	if (Instruction* I = SF.Caller.getInstruction()) {
		GenericValue Result;
		Result.PointerVal = virRes;
		SetValue(I, Result, SF);
	}
	SF.Caller = CallSite();
}

void Interpreter::visitRand(ExecutionContext &SF) {

	ASSERT(SF.Caller.arg_size() == 0, "not the right number of parameters for rand");
	if (Instruction* I = SF.Caller.getInstruction() ) {
		GenericValue Result;
		Result.IntVal = APInt::getNullValue(32);
		Result.IntVal = Result.IntVal + rand();
		SetValue(I, Result, SF);
	}
	SF.Caller = CallSite();
}

void Interpreter::visitSysConf(ExecutionContext &SF) {

	ASSERT(SF.Caller.arg_size() == 1, "not the right number of parameters for sysconf");
	Value *V = *SF.Caller.arg_begin();
	GenericValue arg = getOperandValue(V, SF);
	int name = arg.IntVal.getLimitedValue();
	long ret = sysconf(name);
	if (Instruction* I = SF.Caller.getInstruction()) {
		GenericValue Result;
		Result.IntVal = APInt::getNullValue(32);
		Result.IntVal = Result.IntVal + ret;
		SetValue(I, Result, SF);
	}
	SF.Caller = CallSite();
}

void Interpreter::visitMmap(ExecutionContext &SF) {
		
	ASSERT(SF.Caller.arg_size() == 6, "not the right number of parameters for mmap");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V,SF);
	void *address = arg.PointerVal;
	// message in the assert added
	ASSERT(0==(size_t)address, "Execution.cpp: visitMmap");
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	size_t length = (size_t)arg.IntVal.getLimitedValue();
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	int protect = arg.IntVal.getLimitedValue();
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	int flags = arg.IntVal.getLimitedValue();
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	int filedes = arg.IntVal.getLimitedValue();
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	off_t offset = (off_t)arg.IntVal.getLimitedValue();
#if defined(VIRTUALMEMORY)
	void *natPtr = mmap(address, length, protect, flags, filedes, offset);
	//ASSERT((size_t)natPtr%128==0, "Address returned by mmap not aligned on 128");
	void *virPtr;
	if(natPtr != NULL) {
		virPtr = (void *)nextVirtualAddress;
		nextVirtualAddress += length;
		nextVirtualAddress += MEMDIFF;
		nextVirtualAddress = (size_t)makeAddressAlligned((void*)nextVirtualAddress);
		virtualToNative[virPtr] = natPtr;
		nativeToVirtual[natPtr] = virPtr;
		bytesAtVirtualAddress[virPtr] = length;
	}
	else {
		virPtr = NULL;
	}
#else
	void *virPtr = mmap(address, length, protect, flags, filedes, offset);
	bytesAtPhysicalAddress[virPtr] = length;
#endif
	if (Instruction *I = SF.Caller.getInstruction()) {
		GenericValue Result;
		Result.PointerVal = virPtr;
		SetValue(I,Result,SF);
		SF.Caller = CallSite();
	}
	SF.Caller = CallSite();
}

void Interpreter::visitMunmap(ExecutionContext &SF) {
	
	ASSERT(SF.Caller.arg_size() == 2, "not the right number of parameters for munmap");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V,SF);
#if defined(VIRTUALMEMORY)
	void *virAddress = arg.PointerVal;
	void *natAddress;
	if(virAddress != NULL) {
		natAddress = getNativeAddressFull(virAddress, SF);
	}
	else natAddress = NULL;
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);
	size_t length = (size_t)arg.IntVal.getLimitedValue();
	int ret = munmap(natAddress, length);
	bytesAtVirtualAddress[virAddress] = 0;
#else
	void *addr = arg.PointerVal;
	++ait;
	V = *ait;
	arg = getOperandValue(V, SF);
	size_t length = (size_t)arg.IntVal.getLimitedValue();
	int ret = munmap(addr, length);
	bytesAtPhysicalAddress[addr] = 0;
#endif
	if(Instruction *I = SF.Caller.getInstruction()) {
		GenericValue Result;
		Result.IntVal = APInt::getNullValue(32);
		Result.IntVal = Result.IntVal + ret;
		SetValue(I,Result,SF);
		SF.Caller = CallSite();
	}
	SF.Caller = CallSite();
}

void Interpreter::visitPthreadSelf(ExecutionContext &SF) {

		if(Instruction *I = SF.Caller.getInstruction())
		{
			GenericValue Result;
			Result.IntVal = APInt::getNullValue(32);
			Result.IntVal = Result.IntVal + currThread.tid();
			SetValue(I,Result,SF);
			SF.Caller = CallSite();
		}
		SF.Caller = CallSite();
}

void Interpreter::visitKeyCreate(ExecutionContext &SF) {

	ASSERT(SF.Caller.arg_size() == 2, "not the right number of parameters for key_create");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V,SF);
#if defined(VIRTUALMEMORY)
	void *virAddr = arg.PointerVal;
	void *natAddr = getNativeAddressFull(virAddr,SF);
#else
	void *natAddr = arg.PointerVal;
#endif
	int ret = 0;
	++ait;
	V = *ait;
	arg = getOperandValue(V,SF);

	Module::iterator it;
	Function* funcAddr;
	bool found;
	for(it = Mod->begin(); it != Mod->end(); ++it) {
		if(arg.PointerVal == getPointerToFunction(it)) {
			found = true;
			funcAddr = it;
			break;
		}
	}

	ASSERT(found, "function in given as thread destructor cannot be found");

	// set NULL key for every thread
	ThreadKey thread_key;
	std::set<Thread> tmp_threads;
	for (std::map<std::pair<Thread, char*>, ThreadKey>::const_iterator cit = threadKeys.begin(); 
		cit != threadKeys.end(); ++cit) {
		if (tmp_threads.find(cit->first.first) != tmp_threads.end()) {
			tmp_threads.insert(cit->first.first);
		}
	}

	thread_key.setKey(NULL);
	thread_key.setDestructor(NULL);
	for (std::set<Thread>::const_iterator cit = tmp_threads.begin(); cit != tmp_threads.end(); ++cit) {
		threadKeys[std::make_pair(*cit, (char*)natAddr)] = thread_key;
	}

	// set destructor function for the current thread
	thread_key.setKey(NULL);
	thread_key.setDestructor(funcAddr);
	threadKeys[std::make_pair(currThread, (char*)natAddr)] = thread_key;

	if(Instruction *I = SF.Caller.getInstruction()) {
		GenericValue Result;
		Result.IntVal = APInt::getNullValue(32);
		Result.IntVal = Result.IntVal + ret;
		SetValue(I,Result,SF);
		SF.Caller = CallSite();
	}
	SF.Caller = CallSite();
}

void Interpreter::visitKeyGetSpecific(ExecutionContext &SF) {

	ASSERT(SF.Caller.arg_size() == 1, "not right number of parameters for get_specific");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V, SF);
#if defined(VIRTUALMEMORY)
	void *virAddr = arg.PointerVal;
	void *natAddr = getNativeAddressFull(virAddr, SF);
#else
	void *natAddr = arg.PointerVal;
#endif
	if (Instruction *I = SF.Caller.getInstruction()) {
		GenericValue Result;
		if (threadKeys.count(std::make_pair(currThread, (char*)natAddr)) > 0) {
			Result.PointerVal = threadKeys[std::make_pair(currThread, (char*)natAddr)].getKey();
		}
		else {
			Result.PointerVal = NULL;
		}
		SetValue(I, Result, SF);
		SF.Caller = CallSite();
	}
	SF.Caller = CallSite();
	return;
}

void Interpreter::visitKeySetSpecific(ExecutionContext &SF) {

	ASSERT(SF.Caller.arg_size() == 2, "not right number of parameters for set_specific");
	CallSite::arg_iterator ait = SF.Caller.arg_begin();
	Value *V = *ait;
	GenericValue arg = getOperandValue(V, SF);
#if defined(VIRTUALMEMORY)
	void *virAddr = arg.PointerVal;
	char *name = (char*)getNativeAddressFull(virAddr, SF);
#else
	char *name = (char*)arg.PointerVal;
#endif
	++ait;
	V = *ait;
	arg = getOperandValue(V, SF);
#if defined(VIRTUALMEMORY)
	void* val = arg.PointerVal;
#else
	void *val = arg.PointerVal;
#endif
	ThreadKey thread_key;
	thread_key = threadKeys[std::make_pair(currThread, name)];
	thread_key.setKey(val);
	threadKeys[std::make_pair(currThread, name)] = thread_key;
	SF.Caller = CallSite();
	return;
}

void Interpreter::visitCallSite(CallSite CS) {

	ExecutionContext &SF = ECStack->back();
	Function *F = CS.getCalledFunction();
	SF.Caller = CS;
	
	if (F->getName().str() == "spawn_thread")
		visitSpawnThread(SF);
	else if (F->getName().str() == "assert")
		visitAssert(SF);
	else if (F->getName().str() == "assert_exist") 
		visitAssertExist(SF);
	else if (F->getName().str() == "join_all")
		visitJoinAll(SF);
	else if (F->getName().str() == "cas32") 
		visitCAS(SF, CAS32);
	else if (F->getName().str() == "casio") 
		visitCAS(SF, CASIO);
	else if (F->getName().str() == "caspo") 
		visitCASPO(SF);
	else if (F->getName().str() == "fasio") 
		visitFASIO(SF);
	else if (F->getName().str() == "faspo")  
		visitFASPO(SF);
	else if (F->getName().str() == "membar_sl") {
		membar_sl(currThread);
		SF.Caller = CallSite();
	}
	else if (F->getName().str() == "membar_ss") {
		membar_ss(currThread);
		SF.Caller = CallSite();
	}
	else if (F->getName().str() == "malloc")
		visitMalloc(SF);
	else if (F->getName().str() == "free")
		visitFree(SF);
	else if (F->getName().str() == "memset")
		visitMemset(SF);
	else if (F->getName().str() == "memcpy32")
		visitMemcpy(SF, CS);
	else if (F->getName().str() == "nprint_string") 
		visitNprintString(SF);
	else if (F->getName().str() == "nprint_int") 
		visitNprintInt(SF);
	else if (F->getName().str() == "getenv") 
		visitGetEnv(SF);
	else if (F->getName().str() == "rand") 
		visitRand(SF);
	else if (F->getName().str() == "sysconf") 
		visitSysConf(SF);
	else if (F->getName().str() == "mmap") 
		visitMmap(SF);
	else if (F->getName().str() == "munmap") 
		visitMunmap(SF);
	else if (F->getName().str() == "pthread_self") 
		visitPthreadSelf(SF);
	else if (F->getName().str() == "key_create") 
		visitKeyCreate(SF);
	else if (F->getName().str() == "key_getspecific") 
		visitKeyGetSpecific(SF);
	else if (F->getName().str() == "key_setspecific") 
		visitKeySetSpecific(SF);
	else {
		getInvokeHistoryData(SF);
		history->RecordInvokeEvent((Function*)GVTOP(getOperandValue(SF.Caller.getCalledValue(), SF)), currThread);
	
		if (F && F->isDeclaration()) { 
			switch (F->getIntrinsicID()) {
				case Intrinsic::not_intrinsic:
					break;
				case Intrinsic::vastart:   // va_start	
				{
					GenericValue ArgIndex;
					ArgIndex.UIntPairVal.first = ECStack->size() - 1;
					ArgIndex.UIntPairVal.second = 0;
					SetValue(CS.getInstruction(), ArgIndex, SF);
					return;
				}
				case Intrinsic::vaend:    // va_end is a noop for the interpreter
					return;
				case Intrinsic::vacopy:   // va_copy: dest = src
					SetValue(CS.getInstruction(), getOperandValue(*CS.arg_begin(), SF), SF);
					return;
				default:
					// If it is an unknown intrinsic function, use the intrinsic lowering
					// class to transform it into hopefully tasty LLVM code.
					//
					BasicBlock::iterator me(CS.getInstruction());
					BasicBlock *Parent = CS.getInstruction()->getParent();
					bool atBegin(Parent->begin() == me);
					if (!atBegin)
						--me;
					IL->LowerIntrinsicCall(cast<CallInst>(CS.getInstruction()));
	
					// Restore the CurInst pointer to the first instruction newly inserted, if any
					if (atBegin)
					{
						SF.CurInst = Parent->begin();
					}
					else
					{
						SF.CurInst = me;
						++SF.CurInst;
					}
					return;
			}
		}
	
		std::vector<GenericValue> ArgVals;
		const unsigned NumArgs = SF.Caller.arg_size();
		ArgVals.reserve(NumArgs);
		uint16_t pNum = 1;
		for (CallSite::arg_iterator i = SF.Caller.arg_begin(), e = SF.Caller.arg_end(); i != e; ++i, ++pNum) {
			Value *V = *i;
			ArgVals.push_back(getOperandValue(V, SF));
		}
	
		// To handle indirect calls, we must get the pointer value from the argument and treat it as a function pointer.
		GenericValue SRC = getOperandValue(SF.Caller.getCalledValue(), SF);
		callFunction((Function*)GVTOP(SRC), ArgVals);
	}
}

void Interpreter::visitShl(BinaryOperator &I)
{
	ExecutionContext &SF = ECStack->back();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Dest;
	if (Src2.IntVal.getZExtValue() < Src1.IntVal.getBitWidth())
		Dest.IntVal = Src1.IntVal.shl(Src2.IntVal.getZExtValue());
	else
		Dest.IntVal = Src1.IntVal;

	SetValue(&I, Dest, SF);
}

void Interpreter::visitLShr(BinaryOperator &I)
{
	ExecutionContext &SF = ECStack->back();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Dest;
	if (Src2.IntVal.getZExtValue() < Src1.IntVal.getBitWidth())
		Dest.IntVal = Src1.IntVal.lshr(Src2.IntVal.getZExtValue());
	else
		Dest.IntVal = Src1.IntVal;

	SetValue(&I, Dest, SF);
}

void Interpreter::visitAShr(BinaryOperator &I)
{
	ExecutionContext &SF = ECStack->back();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Dest;
	if (Src2.IntVal.getZExtValue() < Src1.IntVal.getBitWidth())
		Dest.IntVal = Src1.IntVal.ashr(Src2.IntVal.getZExtValue());
	else
		Dest.IntVal = Src1.IntVal;

	SetValue(&I, Dest, SF);
}

GenericValue Interpreter::executeTruncInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	const IntegerType *DITy = cast<IntegerType>(DstTy);
	unsigned DBitWidth = DITy->getBitWidth();
	Dest.IntVal = Src.IntVal.trunc(DBitWidth);
	return Dest;
}

GenericValue Interpreter::executeSExtInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	const IntegerType *DITy = cast<IntegerType>(DstTy);
	unsigned DBitWidth = DITy->getBitWidth();
	Dest.IntVal = Src.IntVal.sext(DBitWidth);
	return Dest;
}

GenericValue Interpreter::executeZExtInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	const IntegerType *DITy = cast<IntegerType>(DstTy);
	unsigned DBitWidth = DITy->getBitWidth();
	Dest.IntVal = Src.IntVal.zext(DBitWidth);
	return Dest;
}

GenericValue Interpreter::executeFPTruncInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	ASSERT(SrcVal->getType()->isDoubleTy() && DstTy->isFloatTy(),
			"Invalid FPTrunc instruction");
	Dest.FloatVal = (float) Src.DoubleVal;
	return Dest;
}

GenericValue Interpreter::executeFPExtInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	ASSERT(SrcVal->getType()->isFloatTy() && DstTy->isDoubleTy(),
			"Invalid FPTrunc instruction");
	Dest.DoubleVal = (double) Src.FloatVal;
	return Dest;
}

GenericValue Interpreter::executeFPToUIInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	const Type *SrcTy = SrcVal->getType();
	uint32_t DBitWidth = cast<IntegerType>(DstTy)->getBitWidth();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	ASSERT(SrcTy->isFloatingPointTy(), "Invalid FPToUI instruction");

	if (SrcTy->getTypeID() == Type::FloatTyID)
		Dest.IntVal = APIntOps::RoundFloatToAPInt(Src.FloatVal, DBitWidth);
	else
		Dest.IntVal = APIntOps::RoundDoubleToAPInt(Src.DoubleVal, DBitWidth);
	return Dest;
}

GenericValue Interpreter::executeFPToSIInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	const Type *SrcTy = SrcVal->getType();
	uint32_t DBitWidth = cast<IntegerType>(DstTy)->getBitWidth();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	ASSERT(SrcTy->isFloatingPointTy(), "Invalid FPToSI instruction");

	if (SrcTy->getTypeID() == Type::FloatTyID)
		Dest.IntVal = APIntOps::RoundFloatToAPInt(Src.FloatVal, DBitWidth);
	else
		Dest.IntVal = APIntOps::RoundDoubleToAPInt(Src.DoubleVal, DBitWidth);
	return Dest;
}

GenericValue Interpreter::executeUIToFPInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	ASSERT(DstTy->isFloatingPointTy(), "Invalid UIToFP instruction");

	if (DstTy->getTypeID() == Type::FloatTyID)
		Dest.FloatVal = APIntOps::RoundAPIntToFloat(Src.IntVal);
	else
		Dest.DoubleVal = APIntOps::RoundAPIntToDouble(Src.IntVal);
	return Dest;
}

GenericValue Interpreter::executeSIToFPInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	ASSERT(DstTy->isFloatingPointTy(), "Invalid SIToFP instruction");

	if (DstTy->getTypeID() == Type::FloatTyID)
		Dest.FloatVal = APIntOps::RoundSignedAPIntToFloat(Src.IntVal);
	else
		Dest.DoubleVal = APIntOps::RoundSignedAPIntToDouble(Src.IntVal);
	return Dest;

}

GenericValue Interpreter::executePtrToIntInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	uint32_t DBitWidth = cast<IntegerType>(DstTy)->getBitWidth();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	ASSERT(SrcVal->getType()->isPointerTy(), "Invalid PtrToInt instruction");

	Dest.IntVal = APInt(DBitWidth, (intptr_t) Src.PointerVal);
	return Dest;
}

GenericValue Interpreter::executeIntToPtrInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	ASSERT(DstTy->isPointerTy(), "Invalid PtrToInt instruction");

	uint32_t PtrSize = TD.getPointerSizeInBits();
	if (PtrSize != Src.IntVal.getBitWidth())
		Src.IntVal = Src.IntVal.zextOrTrunc(PtrSize);

	Dest.PointerVal = PointerTy(intptr_t(Src.IntVal.getZExtValue()));
	return Dest;
}

GenericValue Interpreter::executeBitCastInst(Value *SrcVal, const Type *DstTy,
		ExecutionContext &SF)
{

	const Type *SrcTy = SrcVal->getType();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	if (DstTy->isPointerTy())
	{
		ASSERT(SrcTy->isPointerTy(), "Invalid BitCast");
		Dest.PointerVal = Src.PointerVal;
	}
	else if (DstTy->isIntegerTy())
	{
		if (SrcTy->isFloatTy())
		{
			Dest.IntVal.zext(sizeof(Src.FloatVal) * CHAR_BIT);
			Dest.IntVal.floatToBits(Src.FloatVal);
		}
		else if (SrcTy->isDoubleTy())
		{
			Dest.IntVal.zext(sizeof(Src.DoubleVal) * CHAR_BIT);
			Dest.IntVal.doubleToBits(Src.DoubleVal);
		}
		else if (SrcTy->isIntegerTy())
		{
			Dest.IntVal = Src.IntVal;
		}
		else
			llvm_unreachable("Invalid BitCast");
	}
	else if (DstTy->isFloatTy())
	{
		if (SrcTy->isIntegerTy())
			Dest.FloatVal = Src.IntVal.bitsToFloat();
		else
			Dest.FloatVal = Src.FloatVal;
	}
	else if (DstTy->isDoubleTy())
	{
		if (SrcTy->isIntegerTy())
			Dest.DoubleVal = Src.IntVal.bitsToDouble();
		else
			Dest.DoubleVal = Src.DoubleVal;
	}
	else
		llvm_unreachable("Invalid Bitcast");

	return Dest;
}

void Interpreter::visitTruncInst(TruncInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeTruncInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitSExtInst(SExtInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeSExtInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitZExtInst(ZExtInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeZExtInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitFPTruncInst(FPTruncInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeFPTruncInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitFPExtInst(FPExtInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeFPExtInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitUIToFPInst(UIToFPInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeUIToFPInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitSIToFPInst(SIToFPInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeSIToFPInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitFPToUIInst(FPToUIInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeFPToUIInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitFPToSIInst(FPToSIInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeFPToSIInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitPtrToIntInst(PtrToIntInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executePtrToIntInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitIntToPtrInst(IntToPtrInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeIntToPtrInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitBitCastInst(BitCastInst &I)
{
	ExecutionContext &SF = ECStack->back();
	SetValue(&I, executeBitCastInst(I.getOperand(0), I.getType(), SF), SF);
}

#define IMPLEMENT_VAARG(TY) \
	case Type::TY##TyID: Dest.TY##Val = Src.TY##Val; break

void Interpreter::visitVAArgInst(VAArgInst &I)
{
	ExecutionContext &SF = ECStack->back();

	// Get the incoming valist parameter.  LLI treats the valist as a
	// (ec-stack-depth var-arg-index) pair.
	GenericValue VAList = getOperandValue(I.getOperand(0), SF);
	GenericValue Dest;
	GenericValue Src = (*ECStack)[VAList.UIntPairVal.first]
		.VarArgs[VAList.UIntPairVal.second];
	const Type *Ty = I.getType();
	switch (Ty->getTypeID())
	{
		case Type::IntegerTyID:
			Dest.IntVal = Src.IntVal;
			IMPLEMENT_VAARG(Pointer);
			IMPLEMENT_VAARG(Float);
			IMPLEMENT_VAARG(Double);
		default:
			dbgs() << "Unhandled dest type for vaarg instruction: " << *Ty << "\n";
			llvm_unreachable(0);
	}

	// Set the Value of this Instruction.
	SetValue(&I, Dest, SF);

	// Move the pointer to the next vararg.
	++VAList.UIntPairVal.second;
}

GenericValue Interpreter::getConstantExprValue (ConstantExpr *CE,
		ExecutionContext &SF)
{
	switch (CE->getOpcode())
	{
		case Instruction::Trunc:
			return executeTruncInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::ZExt:
			return executeZExtInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::SExt:
			return executeSExtInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::FPTrunc:
			return executeFPTruncInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::FPExt:
			return executeFPExtInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::UIToFP:
			return executeUIToFPInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::SIToFP:
			return executeSIToFPInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::FPToUI:
			return executeFPToUIInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::FPToSI:
			return executeFPToSIInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::PtrToInt:
			return executePtrToIntInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::IntToPtr:
			return executeIntToPtrInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::BitCast:
			return executeBitCastInst(CE->getOperand(0), CE->getType(), SF);
		case Instruction::GetElementPtr:
			return executeGEPOperation(CE->getOperand(0), gep_type_begin(CE),
					gep_type_end(CE), SF);
		case Instruction::FCmp:
		case Instruction::ICmp:
			return executeCmpInst(CE->getPredicate(),
					getOperandValue(CE->getOperand(0), SF),
					getOperandValue(CE->getOperand(1), SF),
					CE->getOperand(0)->getType());
		case Instruction::Select:
			return executeSelectInst(getOperandValue(CE->getOperand(0), SF),
					getOperandValue(CE->getOperand(1), SF),
					getOperandValue(CE->getOperand(2), SF));
		default :
			break;
	}

	// The cases below here require a GenericValue parameter for the result
	// so we initialize one, compute it and then return it.
	GenericValue Op0 = getOperandValue(CE->getOperand(0), SF);
	GenericValue Op1 = getOperandValue(CE->getOperand(1), SF);
	GenericValue Dest;
	const Type * Ty = CE->getOperand(0)->getType();
	switch (CE->getOpcode())
	{
		case Instruction::Add:
			Dest.IntVal = Op0.IntVal + Op1.IntVal;
			break;
		case Instruction::Sub:
			Dest.IntVal = Op0.IntVal - Op1.IntVal;
			break;
		case Instruction::Mul:
			Dest.IntVal = Op0.IntVal * Op1.IntVal;
			break;
		case Instruction::FAdd:
			executeFAddInst(Dest, Op0, Op1, Ty);
			break;
		case Instruction::FSub:
			executeFSubInst(Dest, Op0, Op1, Ty);
			break;
		case Instruction::FMul:
			executeFMulInst(Dest, Op0, Op1, Ty);
			break;
		case Instruction::FDiv:
			executeFDivInst(Dest, Op0, Op1, Ty);
			break;
		case Instruction::FRem:
			executeFRemInst(Dest, Op0, Op1, Ty);
			break;
		case Instruction::SDiv:
			Dest.IntVal = Op0.IntVal.sdiv(Op1.IntVal);
			break;
		case Instruction::UDiv:
			Dest.IntVal = Op0.IntVal.udiv(Op1.IntVal);
			break;
		case Instruction::URem:
			Dest.IntVal = Op0.IntVal.urem(Op1.IntVal);
			break;
		case Instruction::SRem:
			Dest.IntVal = Op0.IntVal.srem(Op1.IntVal);
			break;
		case Instruction::And:
			Dest.IntVal = Op0.IntVal & Op1.IntVal;
			break;
		case Instruction::Or:
			Dest.IntVal = Op0.IntVal | Op1.IntVal;
			break;
		case Instruction::Xor:
			Dest.IntVal = Op0.IntVal ^ Op1.IntVal;
			break;
		case Instruction::Shl:
			Dest.IntVal = Op0.IntVal.shl(Op1.IntVal.getZExtValue());
			break;
		case Instruction::LShr:
			Dest.IntVal = Op0.IntVal.lshr(Op1.IntVal.getZExtValue());
			break;
		case Instruction::AShr:
			Dest.IntVal = Op0.IntVal.ashr(Op1.IntVal.getZExtValue());
			break;
		default:
			dbgs() << "Unhandled ConstantExpr: " << *CE << "\n";
			llvm_unreachable(0);
			return GenericValue();
	}
	return Dest;
}

GenericValue Interpreter::getOperandValue(Value *V, ExecutionContext &SF)
{
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V))
	{
		return getConstantExprValue(CE, SF);
	}
	else if (Constant *CPV = dyn_cast<Constant>(V))
	{
		return getConstantValue(CPV);
	}
	else if (GlobalValue *GV = dyn_cast<GlobalValue>(V))
	{
		return PTOGV(getPointerToGlobal(GV));
	}
	else
	{
		return SF.Values[V];
	}
}

//===----------------------------------------------------------------------===//
//                        Dispatch and Execution Code
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// callFunction - Execute the specified function...
//
void Interpreter::callFunction(Function *F,
		const std::vector<GenericValue> &ArgVals)
{
	ASSERT((ECStack->empty() || ECStack->back().Caller.getInstruction() == 0 ||
				ECStack->back().Caller.arg_size() == ArgVals.size()),
			"Incorrect number of arguments passed into function call!");
	// Make a new stack frame... and fill it in.
	ECStack->push_back(ExecutionContext());
	ExecutionContext &StackFrame = ECStack->back();
	StackFrame.CurFunction = F;

	// Special handling for external functions.
	if (F->isDeclaration())
	{
		GenericValue Result = callExternalFunction (F, ArgVals);
		// Simulate a 'ret' instruction of the appropriate type.
		popStackAndReturnValueToCaller (F->getReturnType (), Result);
		return;
	}

	// Get pointers to first LLVM BB & Instruction in function.
	StackFrame.CurBB     = F->begin();
	StackFrame.CurInst   = StackFrame.CurBB->begin();

	// Run through the function arguments and initialize their values...
	ASSERT((ArgVals.size() == F->arg_size() ||
				(ArgVals.size() > F->arg_size() && F->getFunctionType()->isVarArg())),
			"Invalid number of values passed to function invocation!");

	// Handle non-varargs arguments...
	unsigned i = 0;
	for (Function::arg_iterator AI = F->arg_begin(), E = F->arg_end();
			AI != E; ++AI, ++i)
		SetValue(AI, ArgVals[i], StackFrame);

	// Handle varargs arguments...
	StackFrame.VarArgs.assign(ArgVals.begin()+i, ArgVals.end());
}

vector<Thread> Interpreter::getAllActiveThreads() const {
	vector<Thread> enabled;
	std::map<Thread, std::vector<ExecutionContext> >::const_iterator it;
	for (it = threadStacks.begin(); it != threadStacks.end(); ++it) {
		if (!it->second.empty()) {
			enabled.push_back(it->first);
		}
	}
	return enabled;
}


void Interpreter::flushAll() {
	if (Params::WMM == WMM_TSO) {
		vector<Thread> enabled = getAllActiveThreads();
		
		// added an inner for loop to flush all the elem in the buffer and added the history record. 		
		for (unsigned i = 0; i < enabled.size(); i++) {
			while (!thread_buffer_tso[enabled[i]].empty()) {
				flush_buffer_tso(enabled[i]);
			}
			rw_history->RecordRWEvent(enabled[i], FLUSH_INSTR, 0);
		}	
	}
	else if (Params::WMM == WMM_PSO) {
		vector<Thread> enabled = getAllActiveThreads();
		std::map<GenericValue, std::list<GenericValue> >::iterator mit;
		std::vector<GenericValue> possibleAddress;
		for (unsigned i = 0; i < enabled.size(); i++) {
			for(mit = thread_buffer_pso[enabled[i]].begin(); 
				mit != thread_buffer_pso[enabled[i]].end(); ++mit) {
				if (!mit->second.empty()) {
					flush_buffer_pso(enabled[i], mit->first);
				}
			}
			rw_history->RecordRWEvent(enabled[i], FLUSH_INSTR, 0);
		}
	}
}

Constraints constraintsHandler;

// the function where it all happens
void Interpreter::run() {
	cout << "PROGRAM OUTPUT" << endl;
	Scheduler scheduler;
	while (1) {
		if (getAllActiveThreads().size() == 0) {
			flushAll();
			cout << "END OF PROGRAM OUTPUT" << endl;

			if (allonAssertExist == true) {
				history->printRecordedTrace();	
				break;
			}

			clock_t start2 = clock(); // for time measurement
			rw_history->FindSharedRW();
			//rw_history->PrintSharedRW();
			ExitStatus = CheckTrace::checkHistory(history, nextThreadNum);

			/* checking fails, then we build constrains from rw_history, to a data structure. */
			if (ExitStatus == 253) {
				rw_history->PrintSharedRW();
				if (toFix == true) { // lli-synth mode
					//rw_history->PrintSharedRW();
					constraintsHandler.Calculate(rw_history, nextThreadNum);

					timeofChecking += clock() - start2;
 
         				if (constraintsHandler.GetLitSingleNumber() == 0) {
						rw_history->PrintSharedRW();
						exit(255);
					}
				} else { // lli mode
					exit(253);
				}
			}
			break;
		}

		Action action = scheduler.selectAction(this);
		if (action.type == SWITCH_THREAD) {
			// repoint ECStack to point to the stack of the thread that will execute next
			currThread = action.thread;
			ECStack = &threadStacks[currThread];
			ExecutionContext &SF = ECStack->back();  // Current stack frame
			Instruction &I = *SF.CurInst++;
			NumDynamicInsts += 1;

			/* initialize the information of I */
			instr_info.isBlocked = false;
			instr_info.isSharedAccessing = false;

			visit(I);   // Dispatch to one of the visit* methods...

			if (segmentFaultFlag == true && runMain == true) {
				cout << "ERROR: Segmentation Fault!!! Exit!" << endl;
				rw_history->FindSharedRW();
				ExitStatus = 253; 
				if (toFix == true) { // lli-synth mode
					constraintsHandler.Calculate(rw_history, nextThreadNum);
          				if (constraintsHandler.GetLitSingleNumber() == 0) {
						rw_history->PrintSharedRW();
						exit(255);
					}
				} else { // lli mode
					history->printRecordedTrace();	
					rw_history->PrintSharedRW();
					exit(253);
				}
				break;
			}

		} else if (action.type == FLUSH_BUFFER) {
			if (Params::WMM == WMM_TSO) {
				flush_buffer_tso(action.thread);
				// randomly, only one element is flushed
				rw_history->RecordRWEvent(currThread, FLUSH_RANDOM_TSO, -1); 
			}
			else if (Params::WMM == WMM_PSO) {
				flush_buffer_pso(action.thread, action.pso_var);
				rw_history->RecordRWEvent(action.pso_var, currThread, FLUSH_RANDOM_PSO, -1); 
			}
		}
	}
}
