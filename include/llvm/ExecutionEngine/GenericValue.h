//===-- GenericValue.h - Represent any type of LLVM value -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The GenericValue class is used to represent an LLVM value of arbitrary type.
//
//===----------------------------------------------------------------------===//


#ifndef GENERIC_VALUE_H
#define GENERIC_VALUE_H

#include "llvm/ADT/APInt.h"
#include "llvm/System/DataTypes.h"

namespace llvm {

	typedef void* PointerTy;
	class APInt;

	struct GenericValue {
		union {
			double          DoubleVal;
			float           FloatVal;
			PointerTy       PointerVal;
			struct { unsigned int first; unsigned int second; } UIntPairVal;
			unsigned char   Untyped[8];
		};
		APInt IntVal;   // also used for long doubles

		GenericValue() : DoubleVal(0.0), IntVal(1,0) {}
		explicit GenericValue(void *V) : PointerVal(V), IntVal(1,0) { }
//#if defined(PSO_WMM)  
		// NN: operator< for adding GenericValue to std::map<>
		bool operator<(const GenericValue& gv) const {
			//return this->IntVal.getLimitedValue() < gv.IntVal.getLimitedValue();
			return this->PointerVal < gv.PointerVal;
		}
		bool operator==(const GenericValue& gv) const {
			return this->PointerVal == gv.PointerVal;
		}
//#endif
	};

	inline GenericValue PTOGV(void *P) { return GenericValue(P); }
	inline void* GVTOP(const GenericValue &GV) { return GV.PointerVal; }

} // End llvm namespace
#endif
