//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the abstract interface that implements execution support
// for LLVM. The file was added for DFENCE.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_THREAD_H
#define LLVM_THREAD_H

#include <vector>
#include <map>
#include <string>
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ValueMap.h"
#include "llvm/Support/ValueHandle.h"
#include "llvm/System/Mutex.h"
#include "llvm/Target/TargetMachine.h"

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

	class Thread
	{
	private:
		int _tid;
	public:
		Thread() : _tid(0) {}
		Thread(int t) : _tid(t) {}
		int tid() const { return _tid; }

		static Thread getThreadByNumber(int n){
			Thread ret;
			ret._tid = n;
			return ret;
		}
		bool operator< ( const Thread& p ) const {
			return _tid < p.tid();
		}
		bool operator> ( const Thread& p ) const{
			return _tid > p.tid();
		}
		bool operator== ( const Thread& p ) const {
			return _tid == p.tid();
		}
		Thread& operator= ( const Thread& p ) {
			_tid = p.tid();
			return *this;
		}
		bool operator< ( const int& tid ) const {
			return _tid < tid;
		}
		Thread& operator= ( const int& tid) {
			_tid = tid;
			return *this;
		}
		Thread& operator-- () {
			if (_tid > 0) {
				_tid--;
			} else {
				_tid = 0;
			}
			return *this;
		}
		Thread& operator++ () {
			_tid++;
			return *this;
		}
	};
} // End llvm namespace

#endif
