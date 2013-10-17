//===-- ThreadKey.h - Represent any type of LLVM value -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The ThreadKey class was added for DFENCE.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_THREAD_KEY_H
#define LLVM_THREAD_KEY_H

#include <vector>
#include <map>
#include <string>
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ValueMap.h"
#include "llvm/Support/ValueHandle.h"
#include "llvm/System/Mutex.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Function.h"

namespace llvm
{

	struct GenericValue;
	class Constant;
	class ExecutionEngine;
	class Function;
	class GlobalVariable;
	class GlobalValue;
	class JITEventListener;
	class JITMemoryManager;
	class MachineCodeInfo;
	class Module;
	class MutexGuard;
	class TargetData;
	class Type;
	class Function;

	class ThreadKey
	{
	private:
		void* key;
//		void (*destructor) (void*);
		Function* destructor;
	public:
		ThreadKey() {
			key = NULL;
			destructor = NULL;		
		}

		void setKey(void* _key) {
			key = _key;
		}
/*
		void setDestructor(void (*_destructor) (void*)) {
			destructor = _destructor;
		}
*/
		
		void setDestructor(Function* _destructor) {
			destructor = _destructor;		
		}

		void* getKey() const {
			return key;		
		}
		
		Function* getDestructor() const {
			return destructor;		
		}
	};
} // End llvm namespace

#endif
