//===-- ExecutionEngine.cpp - Common Implementation shared by EEs ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the common interface used by the various execution engine
// subclasses.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "jit"
#include "llvm/ExecutionEngine/ExecutionEngine.h"

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MutexGuard.h"
#include "llvm/Support/ValueHandle.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/DynamicLibrary.h"
#include "llvm/System/Host.h"
#include "llvm/Target/TargetData.h"
#include <cmath>
#include <cstring>
using namespace llvm;

STATISTIC(NumInitBytes, "Number of bytes of global vars initialized");
STATISTIC(NumGlobals  , "Number of global vars initialized");

ExecutionEngine *(*ExecutionEngine::JITCtor)(
		Module *M,
		std::string *ErrorStr,
		JITMemoryManager *JMM,
		CodeGenOpt::Level OptLevel,
		bool GVsWithCode,
		CodeModel::Model CMM,
		StringRef MArch,
		StringRef MCPU,
		const SmallVectorImpl<std::string>& MAttrs) = 0;
ExecutionEngine *(*ExecutionEngine::InterpCtor)(Module *M,
		std::string *ErrorStr) = 0;
ExecutionEngine::EERegisterFn ExecutionEngine::ExceptionTableRegister = 0;


ExecutionEngine::ExecutionEngine(Module *M)
	: EEState(*this),
	LazyFunctionCreator(0) {
		CompilingLazily         = false;
		GVCompilationDisabled   = false;
		SymbolSearchingDisabled = false;
		Modules.push_back(M);
#if defined(VIRTUALMEMORY)
		// set the nextVirtualAddress to 1;
		nextVirtualAddress = 1 << 12;
#endif
		ASSERT(M, "Module is null?");
	}

ExecutionEngine::~ExecutionEngine() {
	clearAllGlobalMappings();
	for (unsigned i = 0, e = Modules.size(); i != e; ++i)
		delete Modules[i];
}

char* ExecutionEngine::getMemoryForGV(const GlobalVariable* GV) {
	const Type *ElTy = GV->getType()->getElementType();
	size_t GVSize = (size_t)getTargetData()->getTypeAllocSize(ElTy);
	return new char[GVSize];
}

/// removeModule - Remove a Module from the list of modules.
bool ExecutionEngine::removeModule(Module *M) {
	for(SmallVector<Module *, 1>::iterator I = Modules.begin(), 
			E = Modules.end(); I != E; ++I) {
		Module *Found = *I;
		if (Found == M) {
			Modules.erase(I);
			clearGlobalMappingsFromModule(M);
			return true;
		}
	}
	return false;
}

/// FindFunctionNamed - Search all of the active modules to find the one that
/// defines FnName.  This is very slow operation and shouldn't be used for
/// general code.
Function *ExecutionEngine::FindFunctionNamed(const char *FnName) {
	for (unsigned i = 0, e = Modules.size(); i != e; ++i) {
		if (Function *F = Modules[i]->getFunction(FnName))
			return F;
	}
	return 0;
}


void *ExecutionEngineState::RemoveMapping(
		const MutexGuard &, const GlobalValue *ToUnmap) {
	GlobalAddressMapTy::iterator I = GlobalAddressMap.find(ToUnmap);
	void *OldVal;
	if (I == GlobalAddressMap.end())
		OldVal = 0;
	else {
		OldVal = I->second;
		GlobalAddressMap.erase(I);
	}

	GlobalAddressReverseMap.erase(OldVal);
	return OldVal;
}

/// addGlobalMapping - Tell the execution engine that the specified global is
/// at the specified location.  This is used internally as functions are JIT'd
/// and as global variables are laid out in memory.  It can and should also be
/// used by clients of the EE that want to have an LLVM global overlay
/// existing data in memory.
void ExecutionEngine::addGlobalMapping(const GlobalValue *GV, void *Addr) {
	MutexGuard locked(lock);

	DEBUG(dbgs() << "JIT: Map \'" << GV->getName() 
			<< "\' to [" << Addr << "]\n";);
	void *&CurVal = EEState.getGlobalAddressMap(locked)[GV];
	ASSERT((CurVal == 0 || Addr == 0) , "GlobalMapping already established!");
	CurVal = Addr;

	// If we are using the reverse mapping, add it too
	if (!EEState.getGlobalAddressReverseMap(locked).empty()) {
		AssertingVH<const GlobalValue> &V =
			EEState.getGlobalAddressReverseMap(locked)[Addr];
		ASSERT((V == 0 || GV == 0) , "GlobalMapping already established!");
		V = GV;
	}
}

/// clearAllGlobalMappings - Clear all global mappings and start over again
/// use in dynamic compilation scenarios when you want to move globals
void ExecutionEngine::clearAllGlobalMappings() {
	MutexGuard locked(lock);

	EEState.getGlobalAddressMap(locked).clear();
	EEState.getGlobalAddressReverseMap(locked).clear();
}

/// clearGlobalMappingsFromModule - Clear all global mappings that came from a
/// particular module, because it has been removed from the JIT.
void ExecutionEngine::clearGlobalMappingsFromModule(Module *M) {
	MutexGuard locked(lock);

	for (Module::iterator FI = M->begin(), FE = M->end(); FI != FE; ++FI) {
		EEState.RemoveMapping(locked, FI);
	}
	for (Module::global_iterator GI = M->global_begin(), GE = M->global_end(); 
			GI != GE; ++GI) {
		EEState.RemoveMapping(locked, GI);
	}
}

/// updateGlobalMapping - Replace an existing mapping for GV with a new
/// address.  This updates both maps as required.  If "Addr" is null, the
/// entry for the global is removed from the mappings.
void *ExecutionEngine::updateGlobalMapping(const GlobalValue *GV, void *Addr) {
	MutexGuard locked(lock);

	ExecutionEngineState::GlobalAddressMapTy &Map =
		EEState.getGlobalAddressMap(locked);

	// Deleting from the mapping?
	if (Addr == 0) {
		return EEState.RemoveMapping(locked, GV);
	}

	void *&CurVal = Map[GV];
	void *OldVal = CurVal;

	if (CurVal && !EEState.getGlobalAddressReverseMap(locked).empty())
		EEState.getGlobalAddressReverseMap(locked).erase(CurVal);
	CurVal = Addr;

	// If we are using the reverse mapping, add it too
	if (!EEState.getGlobalAddressReverseMap(locked).empty()) {
		AssertingVH<const GlobalValue> &V =
			EEState.getGlobalAddressReverseMap(locked)[Addr];
		ASSERT((V == 0 || GV == 0) , "GlobalMapping already established!");
		V = GV;
	}
	return OldVal;
}

/// getPointerToGlobalIfAvailable - This returns the address of the specified
/// global value if it is has already been codegen'd, otherwise it returns null.
///
void *ExecutionEngine::getPointerToGlobalIfAvailable(const GlobalValue *GV) {
	MutexGuard locked(lock);

	ExecutionEngineState::GlobalAddressMapTy::iterator I =
		EEState.getGlobalAddressMap(locked).find(GV);
	return I != EEState.getGlobalAddressMap(locked).end() ? I->second : 0;
}

/// getGlobalValueAtAddress - Return the LLVM global value object that starts
/// at the specified address.
///
const GlobalValue *ExecutionEngine::getGlobalValueAtAddress(void *Addr) {
	MutexGuard locked(lock);

	// If we haven't computed the reverse mapping yet, do so first.
	if (EEState.getGlobalAddressReverseMap(locked).empty()) {
		for (ExecutionEngineState::GlobalAddressMapTy::iterator
				I = EEState.getGlobalAddressMap(locked).begin(),
				E = EEState.getGlobalAddressMap(locked).end(); I != E; ++I)
			EEState.getGlobalAddressReverseMap(locked).insert(std::make_pair(I->second,
						I->first));
	}

	std::map<void *, AssertingVH<const GlobalValue> >::iterator I =
		EEState.getGlobalAddressReverseMap(locked).find(Addr);
	return I != EEState.getGlobalAddressReverseMap(locked).end() ? I->second : 0;
}

// CreateArgv - Turn a vector of strings into a nice argv style array of
// pointers to null terminated strings.
//
static void *CreateArgv(LLVMContext &C, ExecutionEngine *EE,
		const std::vector<std::string> &InputArgv) {
	unsigned PtrSize = EE->getTargetData()->getPointerSize();
	char *Result = new char[(InputArgv.size()+1)*PtrSize];

	DEBUG(dbgs() << "JIT: ARGV = " << (void*)Result << "\n");
	const Type *SBytePtr = Type::getInt8PtrTy(C);

	for (unsigned i = 0; i != InputArgv.size(); ++i) {
		unsigned Size = InputArgv[i].size()+1;
		char *Dest = new char[Size];
		DEBUG(dbgs() << "JIT: ARGV[" << i << "] = " << (void*)Dest << "\n");

		std::copy(InputArgv[i].begin(), InputArgv[i].end(), Dest);
		Dest[Size-1] = 0;

		// Endian safe: Result[i] = (PointerTy)Dest;
		EE->StoreValueToMemory(PTOGV(Dest), (GenericValue*)(Result+i*PtrSize),
				SBytePtr);
	}

	// Null terminate it
	EE->StoreValueToMemory(PTOGV(0),
			(GenericValue*)(Result+InputArgv.size()*PtrSize),
			SBytePtr);
	return Result;
}


/// runStaticConstructorsDestructors - This method is used to execute all of
/// the static constructors or destructors for a module, depending on the
/// value of isDtors.
void ExecutionEngine::runStaticConstructorsDestructors(Module *module,
		bool isDtors) {
	const char *Name = isDtors ? "llvm.global_dtors" : "llvm.global_ctors";

	// Execute global ctors/dtors for each module in the program.

	GlobalVariable *GV = module->getNamedGlobal(Name);

	// If this global has internal linkage, or if it has a use, then it must be
	// an old-style (llvmgcc3) static ctor with __main linked in and in use.  If
	// this is the case, don't execute any of the global ctors, __main will do
	// it.
	if (!GV || GV->isDeclaration() || GV->hasLocalLinkage()) return;

	// Should be an array of '{ int, void ()* }' structs.  The first value is
	// the init priority, which we ignore.
	ConstantArray *InitList = dyn_cast<ConstantArray>(GV->getInitializer());
	if (!InitList) return;
	for (unsigned i = 0, e = InitList->getNumOperands(); i != e; ++i)
		if (ConstantStruct *CS = 
				dyn_cast<ConstantStruct>(InitList->getOperand(i))) {
			if (CS->getNumOperands() != 2) return; // Not array of 2-element structs.

			Constant *FP = CS->getOperand(1);
			if (FP->isNullValue())
				break;  // Found a null terminator, exit.

			if (ConstantExpr *CE = dyn_cast<ConstantExpr>(FP))
				if (CE->isCast())
					FP = CE->getOperand(0);
			if (Function *F = dyn_cast<Function>(FP)) {
				// Execute the ctor/dtor function!
				runFunction(F, std::vector<GenericValue>());
			}
		}
}

/// runStaticConstructorsDestructors - This method is used to execute all of
/// the static constructors or destructors for a program, depending on the
/// value of isDtors.
void ExecutionEngine::runStaticConstructorsDestructors(bool isDtors) {
	// Execute global ctors/dtors for each module in the program.
	for (unsigned m = 0, e = Modules.size(); m != e; ++m)
		runStaticConstructorsDestructors(Modules[m], isDtors);
}

#ifndef NDEBUG
/// isTargetNullPtr - Return whether the target pointer stored at Loc is null.
static bool isTargetNullPtr(ExecutionEngine *EE, void *Loc) {
	unsigned PtrSize = EE->getTargetData()->getPointerSize();
	for (unsigned i = 0; i < PtrSize; ++i)
		if (*(i + (uint8_t*)Loc))
			return false;
	return true;
}
#endif

/// runFunctionAsMain - This is a helper function which wraps runFunction to
/// handle the common task of starting up main with the specified argc, argv,
/// and envp parameters.
int ExecutionEngine::runFunctionAsMain(Function *Fn,
		const std::vector<std::string> &argv,
		const char * const * envp) {
	std::vector<GenericValue> GVArgs;
	GenericValue GVArgc;
	GVArgc.IntVal = APInt(32, argv.size());

	// Check main() type
	unsigned NumArgs = Fn->getFunctionType()->getNumParams();
	const FunctionType *FTy = Fn->getFunctionType();
	const Type* PPInt8Ty = Type::getInt8PtrTy(Fn->getContext())->getPointerTo();
	switch (NumArgs) {
		case 3:
			if (FTy->getParamType(2) != PPInt8Ty) {
				llvm_report_error("Invalid type for third argument of main() supplied");
			}
			// FALLS THROUGH
		case 2:
			if (FTy->getParamType(1) != PPInt8Ty) {
				llvm_report_error("Invalid type for second argument of main() supplied");
			}
			// FALLS THROUGH
		case 1:
			if (!FTy->getParamType(0)->isIntegerTy(32)) {
				llvm_report_error("Invalid type for first argument of main() supplied");
			}
			// FALLS THROUGH
		case 0:
			if (!FTy->getReturnType()->isIntegerTy() &&
					!FTy->getReturnType()->isVoidTy()) {
				llvm_report_error("Invalid return type of main() supplied");
			}
			break;
		default:
			llvm_report_error("Invalid number of arguments of main() supplied");
	}

	if (NumArgs) {
		GVArgs.push_back(GVArgc); // Arg #0 = argc.
		if (NumArgs > 1) {
			// Arg #1 = argv.
			GVArgs.push_back(PTOGV(CreateArgv(Fn->getContext(), this, argv))); 
			ASSERT(!isTargetNullPtr(this, GVTOP(GVArgs[1])),
					"argv[0] was null after CreateArgv");
			if (NumArgs > 2) {
				std::vector<std::string> EnvVars;
				for (unsigned i = 0; envp[i]; ++i)
					EnvVars.push_back(envp[i]);
				// Arg #2 = envp.
				GVArgs.push_back(PTOGV(CreateArgv(Fn->getContext(), this, EnvVars)));
			}
		}
	}
	return runFunction(Fn, GVArgs).IntVal.getZExtValue();
}

/// If possible, create a JIT, unless the caller specifically requests an
/// Interpreter or there's an error. If even an Interpreter cannot be created,
/// NULL is returned.
///
ExecutionEngine *ExecutionEngine::create(Module *M,
		bool ForceInterpreter,
		std::string *ErrorStr,
		CodeGenOpt::Level OptLevel,
		bool GVsWithCode) {
	return EngineBuilder(M)
		.setEngineKind(ForceInterpreter
				? EngineKind::Interpreter
				: EngineKind::JIT)
		.setErrorStr(ErrorStr)
		.setOptLevel(OptLevel)
		.setAllocateGVsWithCode(GVsWithCode)
		.create();
}

ExecutionEngine *EngineBuilder::create() {
	// Make sure we can resolve symbols in the program as well. The zero arg
	// to the function tells DynamicLibrary to load the program, not a library.
	if (sys::DynamicLibrary::LoadLibraryPermanently(0, ErrorStr))
		return 0;

	// If the user specified a memory manager but didn't specify which engine to
	// create, we assume they only want the JIT, and we fail if they only want
	// the interpreter.
	if (JMM) {
		if (WhichEngine & EngineKind::JIT)
			WhichEngine = EngineKind::JIT;
		else {
			if (ErrorStr)
				*ErrorStr = "Cannot create an interpreter with a memory manager.";
			return 0;
		}
	}

	// Unless the interpreter was explicitly selected or the JIT is not linked,
	// try making a JIT.
	if (WhichEngine & EngineKind::JIT) {
		if (ExecutionEngine::JITCtor) {
			ExecutionEngine *EE =
				ExecutionEngine::JITCtor(M, ErrorStr, JMM, OptLevel,
						AllocateGVsWithCode, CMModel,
						MArch, MCPU, MAttrs);
			if (EE) return EE;
		}
	}

	// If we can't make a JIT and we didn't request one specifically, try making
	// an interpreter instead.
	if (WhichEngine & EngineKind::Interpreter) {
		if (ExecutionEngine::InterpCtor)
			return ExecutionEngine::InterpCtor(M, ErrorStr);
		if (ErrorStr)
			*ErrorStr = "Interpreter has not been linked in.";
		return 0;
	}

	if ((WhichEngine & EngineKind::JIT) && ExecutionEngine::JITCtor == 0) {
		if (ErrorStr)
			*ErrorStr = "JIT has not been linked in.";
	}    
	return 0;
}

/// getPointerToGlobal - This returns the address of the specified global
/// value.  This may involve code generation if it's a function.
///
	void *ExecutionEngine::getPointerToGlobal(const GlobalValue *GV) {
		if (Function *F = const_cast<Function*>(dyn_cast<Function>(GV)))
			return getPointerToFunction(F);

		MutexGuard locked(lock);
		void *p = EEState.getGlobalAddressMap(locked)[GV];
		if (p)
			return p;

		// Global variable might have been added since interpreter started.
		if (GlobalVariable *GVar =
				const_cast<GlobalVariable *>(dyn_cast<GlobalVariable>(GV)))
			EmitGlobalVariable(GVar);
		else
			llvm_unreachable("Global hasn't had an address allocated yet!");
		return EEState.getGlobalAddressMap(locked)[GV];
	}

/// This function converts a Constant* into a GenericValue. The interesting 
/// part is if C is a ConstantExpr.
/// @brief Get a GenericValue for a Constant*
GenericValue ExecutionEngine::getConstantValue(const Constant *C) {
	// If its undefined, return the garbage.
	if (isa<UndefValue>(C)) {
		GenericValue Result;
		switch (C->getType()->getTypeID()) {
			case Type::IntegerTyID:
			case Type::X86_FP80TyID:
			case Type::FP128TyID:
			case Type::PPC_FP128TyID:
				// Although the value is undefined, we still have to construct an APInt
				// with the correct bit width.
				Result.IntVal = APInt(C->getType()->getPrimitiveSizeInBits(), 0);
				break;
			default:
				break;
		}
		return Result;
	}

	// If the value is a ConstantExpr
	if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
		Constant *Op0 = CE->getOperand(0);
		switch (CE->getOpcode()) {
			case Instruction::GetElementPtr: {
								 // Compute the index 
								 GenericValue Result = getConstantValue(Op0);
								 SmallVector<Value*, 8> Indices(CE->op_begin()+1, CE->op_end());
								 uint64_t Offset =
									 TD->getIndexedOffset(Op0->getType(), &Indices[0], Indices.size());

								 char* tmp = (char*) Result.PointerVal;
								 Result = PTOGV(tmp + Offset);
								 return Result;
							 }
			case Instruction::Trunc: {
							 GenericValue GV = getConstantValue(Op0);
							 uint32_t BitWidth = cast<IntegerType>(CE->getType())->getBitWidth();
							 GV.IntVal = GV.IntVal.trunc(BitWidth);
							 return GV;
						 }
			case Instruction::ZExt: {
							GenericValue GV = getConstantValue(Op0);
							uint32_t BitWidth = cast<IntegerType>(CE->getType())->getBitWidth();
							GV.IntVal = GV.IntVal.zext(BitWidth);
							return GV;
						}
			case Instruction::SExt: {
							GenericValue GV = getConstantValue(Op0);
							uint32_t BitWidth = cast<IntegerType>(CE->getType())->getBitWidth();
							GV.IntVal = GV.IntVal.sext(BitWidth);
							return GV;
						}
			case Instruction::FPTrunc: {
							   // FIXME long double
							   GenericValue GV = getConstantValue(Op0);
							   GV.FloatVal = float(GV.DoubleVal);
							   return GV;
						   }
			case Instruction::FPExt:{
							// FIXME long double
							GenericValue GV = getConstantValue(Op0);
							GV.DoubleVal = double(GV.FloatVal);
							return GV;
						}
			case Instruction::UIToFP: {
							  GenericValue GV = getConstantValue(Op0);
							  if (CE->getType()->isFloatTy())
								  GV.FloatVal = float(GV.IntVal.roundToDouble());
							  else if (CE->getType()->isDoubleTy())
								  GV.DoubleVal = GV.IntVal.roundToDouble();
							  else if (CE->getType()->isX86_FP80Ty()) {
								  const uint64_t zero[] = {0, 0};
								  APFloat apf = APFloat(APInt(80, 2, zero));
								  (void)apf.convertFromAPInt(GV.IntVal, 
										  false,
										  APFloat::rmNearestTiesToEven);
								  GV.IntVal = apf.bitcastToAPInt();
							  }
							  return GV;
						  }
			case Instruction::SIToFP: {
							  GenericValue GV = getConstantValue(Op0);
							  if (CE->getType()->isFloatTy())
								  GV.FloatVal = float(GV.IntVal.signedRoundToDouble());
							  else if (CE->getType()->isDoubleTy())
								  GV.DoubleVal = GV.IntVal.signedRoundToDouble();
							  else if (CE->getType()->isX86_FP80Ty()) {
								  const uint64_t zero[] = { 0, 0};
								  APFloat apf = APFloat(APInt(80, 2, zero));
								  (void)apf.convertFromAPInt(GV.IntVal, 
										  true,
										  APFloat::rmNearestTiesToEven);
								  GV.IntVal = apf.bitcastToAPInt();
							  }
							  return GV;
						  }
			case Instruction::FPToUI: // double->APInt conversion handles sign
			case Instruction::FPToSI: {
							  GenericValue GV = getConstantValue(Op0);
							  uint32_t BitWidth = cast<IntegerType>(CE->getType())->getBitWidth();
							  if (Op0->getType()->isFloatTy())
								  GV.IntVal = APIntOps::RoundFloatToAPInt(GV.FloatVal, BitWidth);
							  else if (Op0->getType()->isDoubleTy())
								  GV.IntVal = APIntOps::RoundDoubleToAPInt(GV.DoubleVal, BitWidth);
							  else if (Op0->getType()->isX86_FP80Ty()) {
								  APFloat apf = APFloat(GV.IntVal);
								  uint64_t v;
								  bool ignored;
								  (void)apf.convertToInteger(&v, BitWidth,
										  CE->getOpcode()==Instruction::FPToSI, 
										  APFloat::rmTowardZero, &ignored);
								  GV.IntVal = v; // endian?
							  }
							  return GV;
						  }
			case Instruction::PtrToInt: {
							    GenericValue GV = getConstantValue(Op0);
							    uint32_t PtrWidth = TD->getPointerSizeInBits();
							    GV.IntVal = APInt(PtrWidth, uintptr_t(GV.PointerVal));
							    return GV;
						    }
			case Instruction::IntToPtr: {
							    GenericValue GV = getConstantValue(Op0);
							    uint32_t PtrWidth = TD->getPointerSizeInBits();
							    if (PtrWidth != GV.IntVal.getBitWidth())
								    GV.IntVal = GV.IntVal.zextOrTrunc(PtrWidth);
							    ASSERT(GV.IntVal.getBitWidth() <= 64, "Bad pointer width");
							    GV.PointerVal = PointerTy(uintptr_t(GV.IntVal.getZExtValue()));
							    return GV;
						    }
			case Instruction::BitCast: {
							   GenericValue GV = getConstantValue(Op0);
							   const Type* DestTy = CE->getType();
							   switch (Op0->getType()->getTypeID()) {
								   default: llvm_unreachable("Invalid bitcast operand");
								   case Type::IntegerTyID:
									    ASSERT(DestTy->isFloatingPointTy(), "invalid bitcast");
									    if (DestTy->isFloatTy())
										    GV.FloatVal = GV.IntVal.bitsToFloat();
									    else if (DestTy->isDoubleTy())
										    GV.DoubleVal = GV.IntVal.bitsToDouble();
									    break;
								   case Type::FloatTyID: 
									    ASSERT(DestTy->isIntegerTy(32), "Invalid bitcast");
									    GV.IntVal.floatToBits(GV.FloatVal);
									    break;
								   case Type::DoubleTyID:
									    ASSERT(DestTy->isIntegerTy(64), "Invalid bitcast");
									    GV.IntVal.doubleToBits(GV.DoubleVal);
									    break;
								   case Type::PointerTyID:
									    ASSERT(DestTy->isPointerTy(), "Invalid bitcast");
									    break; // getConstantValue(Op0)  above already converted it
							   }
							   return GV;
						   }
			case Instruction::Add:
			case Instruction::FAdd:
			case Instruction::Sub:
			case Instruction::FSub:
			case Instruction::Mul:
			case Instruction::FMul:
			case Instruction::UDiv:
			case Instruction::SDiv:
			case Instruction::URem:
			case Instruction::SRem:
			case Instruction::And:
			case Instruction::Or:
			case Instruction::Xor: {
						       GenericValue LHS = getConstantValue(Op0);
						       GenericValue RHS = getConstantValue(CE->getOperand(1));
						       GenericValue GV;
						       switch (CE->getOperand(0)->getType()->getTypeID()) {
							       default: llvm_unreachable("Bad add type!");
							       case Type::IntegerTyID:
									switch (CE->getOpcode()) {
										default: llvm_unreachable("Invalid integer opcode");
										case Instruction::Add: GV.IntVal = LHS.IntVal + RHS.IntVal; break;
										case Instruction::Sub: GV.IntVal = LHS.IntVal - RHS.IntVal; break;
										case Instruction::Mul: GV.IntVal = LHS.IntVal * RHS.IntVal; break;
										case Instruction::UDiv:GV.IntVal = LHS.IntVal.udiv(RHS.IntVal); break;
										case Instruction::SDiv:GV.IntVal = LHS.IntVal.sdiv(RHS.IntVal); break;
										case Instruction::URem:GV.IntVal = LHS.IntVal.urem(RHS.IntVal); break;
										case Instruction::SRem:GV.IntVal = LHS.IntVal.srem(RHS.IntVal); break;
										case Instruction::And: GV.IntVal = LHS.IntVal & RHS.IntVal; break;
										case Instruction::Or:  GV.IntVal = LHS.IntVal | RHS.IntVal; break;
										case Instruction::Xor: GV.IntVal = LHS.IntVal ^ RHS.IntVal; break;
									}
									break;
							       case Type::FloatTyID:
									switch (CE->getOpcode()) {
										default: llvm_unreachable("Invalid float opcode");
										case Instruction::FAdd:
											 GV.FloatVal = LHS.FloatVal + RHS.FloatVal; break;
										case Instruction::FSub:
											 GV.FloatVal = LHS.FloatVal - RHS.FloatVal; break;
										case Instruction::FMul:
											 GV.FloatVal = LHS.FloatVal * RHS.FloatVal; break;
										case Instruction::FDiv: 
											 GV.FloatVal = LHS.FloatVal / RHS.FloatVal; break;
										case Instruction::FRem: 
											 GV.FloatVal = ::fmodf(LHS.FloatVal,RHS.FloatVal); break;
									}
									break;
							       case Type::DoubleTyID:
									switch (CE->getOpcode()) {
										default: llvm_unreachable("Invalid double opcode");
										case Instruction::FAdd:
											 GV.DoubleVal = LHS.DoubleVal + RHS.DoubleVal; break;
										case Instruction::FSub:
											 GV.DoubleVal = LHS.DoubleVal - RHS.DoubleVal; break;
										case Instruction::FMul:
											 GV.DoubleVal = LHS.DoubleVal * RHS.DoubleVal; break;
										case Instruction::FDiv: 
											 GV.DoubleVal = LHS.DoubleVal / RHS.DoubleVal; break;
										case Instruction::FRem: 
											 GV.DoubleVal = ::fmod(LHS.DoubleVal,RHS.DoubleVal); break;
									}
									break;
							       case Type::X86_FP80TyID:
							       case Type::PPC_FP128TyID:
							       case Type::FP128TyID: {
											     APFloat apfLHS = APFloat(LHS.IntVal);
											     switch (CE->getOpcode()) {
												     default: llvm_unreachable("Invalid long double opcode");llvm_unreachable(0);
												     case Instruction::FAdd:
													      apfLHS.add(APFloat(RHS.IntVal), APFloat::rmNearestTiesToEven);
													      GV.IntVal = apfLHS.bitcastToAPInt();
													      break;
												     case Instruction::FSub:
													      apfLHS.subtract(APFloat(RHS.IntVal), APFloat::rmNearestTiesToEven);
													      GV.IntVal = apfLHS.bitcastToAPInt();
													      break;
												     case Instruction::FMul:
													      apfLHS.multiply(APFloat(RHS.IntVal), APFloat::rmNearestTiesToEven);
													      GV.IntVal = apfLHS.bitcastToAPInt();
													      break;
												     case Instruction::FDiv: 
													      apfLHS.divide(APFloat(RHS.IntVal), APFloat::rmNearestTiesToEven);
													      GV.IntVal = apfLHS.bitcastToAPInt();
													      break;
												     case Instruction::FRem: 
													      apfLHS.mod(APFloat(RHS.IntVal), APFloat::rmNearestTiesToEven);
													      GV.IntVal = apfLHS.bitcastToAPInt();
													      break;
											     }
										     }
										     break;
						       }
						       return GV;
					       }
			default:
					       break;
		}
		std::string msg;
		raw_string_ostream Msg(msg);
		Msg << "ConstantExpr not handled: " << *CE;
		llvm_report_error(Msg.str());
	}

	GenericValue Result;
	switch (C->getType()->getTypeID()) {
		case Type::FloatTyID: 
			Result.FloatVal = cast<ConstantFP>(C)->getValueAPF().convertToFloat(); 
			break;
		case Type::DoubleTyID:
			Result.DoubleVal = cast<ConstantFP>(C)->getValueAPF().convertToDouble();
			break;
		case Type::X86_FP80TyID:
		case Type::FP128TyID:
		case Type::PPC_FP128TyID:
			Result.IntVal = cast <ConstantFP>(C)->getValueAPF().bitcastToAPInt();
			break;
		case Type::IntegerTyID:
			Result.IntVal = cast<ConstantInt>(C)->getValue();
			break;
		case Type::PointerTyID:
			if (isa<ConstantPointerNull>(C))
				Result.PointerVal = 0;
			else if (const Function *F = dyn_cast<Function>(C))
				Result = PTOGV(getPointerToFunctionOrStub(const_cast<Function*>(F)));
			else if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(C))
				Result = PTOGV(getOrEmitGlobalVariable(const_cast<GlobalVariable*>(GV)));
			else if (const BlockAddress *BA = dyn_cast<BlockAddress>(C))
				Result = PTOGV(getPointerToBasicBlock(const_cast<BasicBlock*>(
								BA->getBasicBlock())));
			else
				llvm_unreachable("Unknown constant pointer type!");
			break;
		default:
			std::string msg;
			raw_string_ostream Msg(msg);
			Msg << "ERROR: Constant unimplemented for type: " << *C->getType();
			llvm_report_error(Msg.str());
	}
	return Result;
}

/// StoreIntToMemory - Fills the StoreBytes bytes of memory starting from Dst
/// with the integer held in IntVal.
static void StoreIntToMemory(const APInt &IntVal, uint8_t *Dst,
		unsigned StoreBytes) {
	ASSERT((IntVal.getBitWidth()+7)/8 >= StoreBytes, "Integer too small!");
	uint8_t *Src = (uint8_t *)IntVal.getRawData();

	if (sys::isLittleEndianHost())
		// Little-endian host - the source is ordered from LSB to MSB.  Order the
		// destination from LSB to MSB: Do a straight copy.
		memcpy(Dst, Src, StoreBytes);
	else {
		// Big-endian host - the source is an array of 64 bit words ordered from
		// LSW to MSW.  Each word is ordered from MSB to LSB.  Order the destination
		// from MSB to LSB: Reverse the word order, but not the bytes in a word.
		while (StoreBytes > sizeof(uint64_t)) {
			StoreBytes -= sizeof(uint64_t);
			// May not be aligned so use memcpy.
			memcpy(Dst + StoreBytes, Src, sizeof(uint64_t));
			Src += sizeof(uint64_t);
		}

		memcpy(Dst, Src + sizeof(uint64_t) - StoreBytes, StoreBytes);
	}
}

/// StoreValueToMemory - Stores the data in Val of type Ty at address Ptr.  Ptr
/// is the address of the memory at which to store Val, cast to GenericValue *.
/// It is not a pointer to a GenericValue containing the address at which to
/// store Val.
void ExecutionEngine::StoreValueToMemory(const GenericValue &Val,
		GenericValue *Ptr, const Type *Ty) {
	const unsigned StoreBytes = getTargetData()->getTypeStoreSize(Ty);

	switch (Ty->getTypeID()) {
		case Type::IntegerTyID:
			StoreIntToMemory(Val.IntVal, (uint8_t*)Ptr, StoreBytes);
			break;
		case Type::FloatTyID:
			*((float*)Ptr) = Val.FloatVal;
			break;
		case Type::DoubleTyID:
			*((double*)Ptr) = Val.DoubleVal;
			break;
		case Type::X86_FP80TyID:
			memcpy(Ptr, Val.IntVal.getRawData(), 10);
			break;
		case Type::PointerTyID:
			// Ensure 64 bit target pointers are fully initialized on 32 bit hosts.
			if (StoreBytes != sizeof(PointerTy))
				memset(Ptr, 0, StoreBytes);

			*((PointerTy*)Ptr) = Val.PointerVal;
			break;
		default:
			dbgs() << "Cannot store value of type " << *Ty << "!\n";
	}

	if (sys::isLittleEndianHost() != getTargetData()->isLittleEndian())
		// Host and target are different endian - reverse the stored bytes.
		std::reverse((uint8_t*)Ptr, StoreBytes + (uint8_t*)Ptr);
}

/// LoadIntFromMemory - Loads the integer stored in the LoadBytes bytes starting
/// from Src into IntVal, which is assumed to be wide enough and to hold zero.
static void LoadIntFromMemory(APInt &IntVal, uint8_t *Src, unsigned LoadBytes) {
	ASSERT((IntVal.getBitWidth()+7)/8 >= LoadBytes, "Integer too small!");
	uint8_t *Dst = (uint8_t *)IntVal.getRawData();

	if (sys::isLittleEndianHost())
		// Little-endian host - the destination must be ordered from LSB to MSB.
		// The source is ordered from LSB to MSB: Do a straight copy.
		memcpy(Dst, Src, LoadBytes);
	else {
		// Big-endian - the destination is an array of 64 bit words ordered from
		// LSW to MSW.  Each word must be ordered from MSB to LSB.  The source is
		// ordered from MSB to LSB: Reverse the word order, but not the bytes in
		// a word.
		while (LoadBytes > sizeof(uint64_t)) {
			LoadBytes -= sizeof(uint64_t);
			// May not be aligned so use memcpy.
			memcpy(Dst, Src + LoadBytes, sizeof(uint64_t));
			Dst += sizeof(uint64_t);
		}

		memcpy(Dst + sizeof(uint64_t) - LoadBytes, Src, LoadBytes);
	}
}

/// FIXME: document
///
void ExecutionEngine::LoadValueFromMemory(GenericValue &Result,
		GenericValue *Ptr,
		const Type *Ty) {
	const unsigned LoadBytes = getTargetData()->getTypeStoreSize(Ty);

	switch (Ty->getTypeID()) {
		case Type::IntegerTyID:
			// An APInt with all words initially zero.
			Result.IntVal = APInt(cast<IntegerType>(Ty)->getBitWidth(), 0);
			LoadIntFromMemory(Result.IntVal, (uint8_t*)Ptr, LoadBytes);
			break;
		case Type::FloatTyID:
			Result.FloatVal = *((float*)Ptr);
			break;
		case Type::DoubleTyID:
			Result.DoubleVal = *((double*)Ptr);
			break;
		case Type::PointerTyID:
			Result.PointerVal = *((PointerTy*)Ptr);
			break;
		case Type::X86_FP80TyID: {
						 // This is endian dependent, but it will only work on x86 anyway.
						 // FIXME: Will not trap if loading a signaling NaN.
						 uint64_t y[2];
						 memcpy(y, Ptr, 10);
						 Result.IntVal = APInt(80, 2, y);
						 break;
					 }
		default:
					 std::string msg;
					 raw_string_ostream Msg(msg);
					 Msg << "Cannot load value of type " << *Ty << "!";
					 llvm_report_error(Msg.str());
	}
}

// InitializeMemory - Recursive function to apply a Constant value into the
// specified memory location...
//
void ExecutionEngine::InitializeMemory(const Constant *Init, void *Addr) {
	DEBUG(dbgs() << "JIT: Initializing " << Addr << " ");
	DEBUG(Init->dump());
	if (isa<UndefValue>(Init)) {
		return;
	} else if (const ConstantVector *CP = dyn_cast<ConstantVector>(Init)) {
		unsigned ElementSize =
			getTargetData()->getTypeAllocSize(CP->getType()->getElementType());
		for (unsigned i = 0, e = CP->getNumOperands(); i != e; ++i)
			InitializeMemory(CP->getOperand(i), (char*)Addr+i*ElementSize);
		return;
	} else if (isa<ConstantAggregateZero>(Init)) {
		memset(Addr, 0, (size_t)getTargetData()->getTypeAllocSize(Init->getType()));
		return;
	} else if (const ConstantArray *CPA = dyn_cast<ConstantArray>(Init)) {
		unsigned ElementSize =
			getTargetData()->getTypeAllocSize(CPA->getType()->getElementType());
		for (unsigned i = 0, e = CPA->getNumOperands(); i != e; ++i)
			InitializeMemory(CPA->getOperand(i), (char*)Addr+i*ElementSize);
		return;
	} else if (const ConstantStruct *CPS = dyn_cast<ConstantStruct>(Init)) {
		const StructLayout *SL =
			getTargetData()->getStructLayout(cast<StructType>(CPS->getType()));
		for (unsigned i = 0, e = CPS->getNumOperands(); i != e; ++i)
			InitializeMemory(CPS->getOperand(i), (char*)Addr+SL->getElementOffset(i));
		return;
	} else if (Init->getType()->isFirstClassType()) {
		GenericValue Val = getConstantValue(Init);
		StoreValueToMemory(Val, (GenericValue*)Addr, Init->getType());
		return;
	}

	dbgs() << "Bad Type: " << *Init->getType() << "\n";
	llvm_unreachable("Unknown constant type to initialize memory with!");
}

/// EmitGlobals - Emit all of the global variables to memory, storing their
/// addresses into GlobalAddress.  This must make sure to copy the contents of
/// their initializers into the memory.
///
void ExecutionEngine::emitGlobals() {

	// Loop over all of the global variables in the program, allocating the memory
	// to hold them.  If there is more than one module, do a prepass over globals
	// to figure out how the different modules should link together.
	//
	std::map<std::pair<std::string, const Type*>,
		const GlobalValue*> LinkedGlobalsMap;

	if (Modules.size() != 1) {
		for (unsigned m = 0, e = Modules.size(); m != e; ++m) {
			Module &M = *Modules[m];
			for (Module::const_global_iterator I = M.global_begin(),
					E = M.global_end(); I != E; ++I) {
				const GlobalValue *GV = I;
				if (GV->hasLocalLinkage() || GV->isDeclaration() ||
						GV->hasAppendingLinkage() || !GV->hasName())
					continue;// Ignore external globals and globals with internal linkage.

				const GlobalValue *&GVEntry = 
					LinkedGlobalsMap[std::make_pair(GV->getName(), GV->getType())];

				// If this is the first time we've seen this global, it is the canonical
				// version.
				if (!GVEntry) {
					GVEntry = GV;
					continue;
				}

				// If the existing global is strong, never replace it.
				if (GVEntry->hasExternalLinkage() ||
						GVEntry->hasDLLImportLinkage() ||
						GVEntry->hasDLLExportLinkage())
					continue;

				// Otherwise, we know it's linkonce/weak, replace it if this is a strong
				// symbol.  FIXME is this right for common?
				if (GV->hasExternalLinkage() || GVEntry->hasExternalWeakLinkage())
					GVEntry = GV;
			}
		}
	}

	std::vector<const GlobalValue*> NonCanonicalGlobals;
	for (unsigned m = 0, e = Modules.size(); m != e; ++m) {
		Module &M = *Modules[m];
		for (Module::const_global_iterator I = M.global_begin(), E = M.global_end();
				I != E; ++I) {
			// In the multi-module case, see what this global maps to.
			if (!LinkedGlobalsMap.empty()) {
				if (const GlobalValue *GVEntry = 
						LinkedGlobalsMap[std::make_pair(I->getName(), I->getType())]) {
					// If something else is the canonical global, ignore this one.
					if (GVEntry != &*I) {
						NonCanonicalGlobals.push_back(I);
						continue;
					}
				}
			}

			if (!I->isDeclaration()) {
				addGlobalMapping(I, getMemoryForGV(I));
			} else {
				// External variable reference. Try to use the dynamic loader to
				// get a pointer to it.
				if (void *SymAddr =
						sys::DynamicLibrary::SearchForAddressOfSymbol(I->getName()))
					addGlobalMapping(I, SymAddr);
				else {
					llvm_report_error("Could not resolve external global address: "
							+I->getName());
				}
			}
		}

		// If there are multiple modules, map the non-canonical globals to their
		// canonical location.
		if (!NonCanonicalGlobals.empty()) {
			for (unsigned i = 0, e = NonCanonicalGlobals.size(); i != e; ++i) {
				const GlobalValue *GV = NonCanonicalGlobals[i];
				const GlobalValue *CGV =
					LinkedGlobalsMap[std::make_pair(GV->getName(), GV->getType())];
				void *Ptr = getPointerToGlobalIfAvailable(CGV);
				ASSERT(Ptr, "Canonical global wasn't codegen'd!");
				addGlobalMapping(GV, Ptr);
			}
		}

		// Now that all of the globals are set up in memory, loop through them all 
		// and initialize their contents.
		for (Module::const_global_iterator I = M.global_begin(), E = M.global_end();
				I != E; ++I) {
			if (!I->isDeclaration()) {
				if (!LinkedGlobalsMap.empty()) {
					if (const GlobalValue *GVEntry = 
							LinkedGlobalsMap[std::make_pair(I->getName(), I->getType())])
						if (GVEntry != &*I)  // Not the canonical variable.
							continue;
				}
				EmitGlobalVariable(I);
			}
		}
	}
}

// EmitGlobalVariable - This method emits the specified global variable to the
// address specified in GlobalAddresses, or allocates new memory if it's not
// already in the map.
void ExecutionEngine::EmitGlobalVariable(const GlobalVariable *GV) {
	void *GA = getPointerToGlobalIfAvailable(GV);

	if (GA == 0) {
		// If it's not already specified, allocate memory for the global.
		GA = getMemoryForGV(GV);
		addGlobalMapping(GV, GA);
	}

	// Don't initialize if it's thread local, let the client do it.
	if (!GV->isThreadLocal())
		InitializeMemory(GV->getInitializer(), GA);

	const Type *ElTy = GV->getType()->getElementType();
	size_t GVSize = (size_t)getTargetData()->getTypeAllocSize(ElTy);
	NumInitBytes += (unsigned)GVSize;
	++NumGlobals;
}

ExecutionEngineState::ExecutionEngineState(ExecutionEngine &EE)
	: EE(EE), GlobalAddressMap(this) {
	}

sys::Mutex *ExecutionEngineState::AddressMapConfig::getMutex(
		ExecutionEngineState *EES) {
	return &EES->EE.lock;
}
void ExecutionEngineState::AddressMapConfig::onDelete(
		ExecutionEngineState *EES, const GlobalValue *Old) {
	void *OldVal = EES->GlobalAddressMap.lookup(Old);
	EES->GlobalAddressReverseMap.erase(OldVal);
}

void ExecutionEngineState::AddressMapConfig::onRAUW(
		ExecutionEngineState *, const GlobalValue *, const GlobalValue *) {
	ASSERT(false, "The ExecutionEngine doesn't know how to handle a"
			" RAUW on a value it has a global mapping for.");
}

#if defined(VIRTUALMEMORY)
void ExecutionEngine::virtualizeGlobalVariables() {
	// write now we assume that it is not possible for a GlobalVariable to be in two different modules. I guess it is so, but we are not sure !
	int i;
	Module::global_iterator git;
	void *virtualAddr;
	void *nativeAddr;
	int numBytes;
	virtualToNative[NULL] = NULL;
	nativeToVirtual[NULL] = NULL;
	bytesAtVirtualAddress[NULL] = 0;
	for(i=0;i<(int)Modules.size();++i) {
		for( git = Modules[i]->global_begin(); git != Modules[i]->global_end(); ++git ) {
			nativeAddr = getPointerToGlobal(git);
			virtualAddr = (void *)nextVirtualAddress;
			numBytes = getTargetData()->getTypeAllocSize(git->getType()->getElementType());
			nextVirtualAddress = nextVirtualAddress + numBytes;
			nextVirtualAddress += MEMDIFF;
			nextVirtualAddress = (size_t)makeAddressAlligned((void*)nextVirtualAddress);
			ASSERT(nextVirtualAddress%4==0, "wrong next address after virtualization of global variable");
			bytesAtVirtualAddress[virtualAddr] = numBytes;
			nativeToVirtual[nativeAddr] = virtualAddr;
			virtualToNative[virtualAddr] = nativeAddr;
			if( updateGlobalMapping(git,virtualAddr) == NULL ) {
				ASSERT(0, "virtualized Global Variable while it has no native address");
			}
			//globalAddresses.push_back( std::pair<int,int>( (int)virtualAddr, (int)virtualAddr + numBytes ) );
		}
	}
}

//void *ExecutionEngine::getNativeAddress(void *virtualAddress) {
//	return virtualToNative[virtualAddress];
//}

void *ExecutionEngine::getVirtualBaseAddressHeap(void *addr) {
	// here we only work with virtual addresses
	std::map<void*, int>::iterator mit;
	int A,B,C;
	C = (size_t)addr;
	for(mit = bytesAtVirtualAddress.begin(); mit != bytesAtVirtualAddress.end(); ++mit) {
		A = (size_t)mit->first;
		B = A + mit->second;
		if(C>=A && C<B) {
			return mit->first;
		}
	}
	return NULL;
}
#else
void ExecutionEngine::physicalizeGlobalVariables() {
  int i;
  Module::global_iterator git;
  void *nativeAddr;
  int numBytes;
  bytesAtPhysicalAddress[NULL] = 0;
  for(i=0;i<(int)Modules.size();++i) {
    for( git = Modules[i]->global_begin(); git != Modules[i]->global_end(); ++git ) {
      nativeAddr = getPointerToGlobal(git);
      numBytes = getTargetData()->getTypeAllocSize(git->getType()->getElementType());
      bytesAtPhysicalAddress[nativeAddr] = numBytes;
    }
  }
}

void *ExecutionEngine::getPhysicalBaseAddressHeap(void *addr) {
  // here we only work with virtual addresses
  std::map<void*, int>::iterator mit;
  int A,B,C;
  C = (size_t)addr;
  for(mit = bytesAtPhysicalAddress.begin(); mit != bytesAtPhysicalAddress.end(); ++mit) {
    A = (size_t)mit->first;
    B = A + mit->second;
    if(C>=A && C<B) {
      return mit->first;
    }
  }
  return NULL;
}
#endif

void *ExecutionEngine::makeAddressAlligned(void *addr) {
	int x = (size_t) addr;
	x += (1 << 12);
	x &= ~( (1 << 12) -1 );
	ASSERT(x%(1 << 12)==0, "not good address");
	return (void*)x;
}

