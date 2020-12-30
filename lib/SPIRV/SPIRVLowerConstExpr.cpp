//===- SPIRVLowerConstExpr.cpp - Regularize LLVM for SPIR-V ------- C++ -*-===//
//
//                     The LLVM/SPIRV Translator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// Copyright (c) 2014 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal with the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimers.
// Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimers in the documentation
// and/or other materials provided with the distribution.
// Neither the names of Advanced Micro Devices, Inc., nor the names of its
// contributors may be used to endorse or promote products derived from this
// Software without specific prior written permission.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH
// THE SOFTWARE.
//
//===----------------------------------------------------------------------===//
//
// This file implements regularization of LLVM module for SPIR-V.
//
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "spv-lower-const-expr"

#include "OCLUtil.h"
#include "SPIRVInternal.h"
#include "libSPIRV/SPIRVDebug.h"

#include "llvm/IR/InstVisitor.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"

#include <list>
#include <unordered_map>

using namespace llvm;
using namespace SPIRV;
using namespace OCLUtil;

namespace SPIRV {

cl::opt<bool> SPIRVLowerConst(
    "spirv-lower-const-expr", cl::init(true),
    cl::desc("LLVM/SPIR-V translation enable lowering constant expression"));

/// Since SPIR-V cannot represent constant expression, constant expressions
/// in LLVM needs to be lowered to instructions.
/// For each function, the constant expressions used by instructions of the
/// function are replaced by instructions placed in the entry block since it
/// dominates all other BB's. Each constant expression only needs to be lowered
/// once in each function and all uses of it by instructions in that function
/// is replaced by one instruction.

class LowerConstExprVisitor : public InstVisitor<LowerConstExprVisitor> {
  std::unordered_map<ConstantExpr *, Instruction *> ConstExprMap;
  Function *F = nullptr;
public:
  LowerConstExprVisitor(Function *Fun): F(Fun){}

  Instruction * lowerConstExprOperand(ConstantExpr *CE, Instruction *InsPoint) {
    auto It = ConstExprMap.find(CE);
    if (It != ConstExprMap.end())
      return It->second;
    Instruction *ReplInst = CE->getAsInstruction();
    ReplInst->insertBefore(InsPoint);
    for (unsigned I = 0, E = ReplInst->getNumOperands(); I != E; ++I) {
      if (ConstantExpr *CSE = dyn_cast<ConstantExpr>(ReplInst->getOperand(I))) {
        ReplInst->setOperand(I, lowerConstExprOperand(CSE, ReplInst));
      }
    }
    ConstExprMap[CE] = ReplInst;
    return ReplInst;
  }
  
  Value * lowerConstMetadataOperand(Metadata *MD, Instruction &InsertPoint) {
    if (auto *ConstMD = dyn_cast<ConstantAsMetadata>(MD)) {
      Constant *C = ConstMD->getValue();
      if (auto *CE = dyn_cast<ConstantExpr>(C)) {
        Instruction *ReplInst = lowerConstExprOperand(CE, &InsertPoint);
        Metadata *RepMD = ValueAsMetadata::get(ReplInst);
        return MetadataAsValue::get(F->getContext(), RepMD);
      }
    }
    return nullptr;
  }

  Value * lowerConstVectorOperand(ConstantVector *Vec, Instruction &I,
      Instruction & InsertPoint, unsigned It) {
    if (!std::all_of(Vec->op_begin(), Vec->op_end(), [](Value *V) {
        return isa<ConstantExpr>(V) || isa<Function>(V);}))
      return nullptr;
    // Expand a vector of constexprs and construct it back with series of
    // insertelement instructions
    std::list<Value *> OpList;
    std::transform(Vec->op_begin(), Vec->op_end(),
                   std::back_inserter(OpList),
                   [&](Value *V) {
                   return lowerConstExprOperand(static_cast<ConstantExpr*>(V),
                       &InsertPoint); });
    Value *Repl = nullptr;
    unsigned Idx = 0;
    PHINode *PhiInst = dyn_cast<PHINode>(&I);
    auto *InsPoint = PhiInst ? &PhiInst->getIncomingBlock(It)->back() : &I;
    std::list<Instruction *> ReplList;
    for (auto *V : OpList) {
      if (auto *Inst = dyn_cast<Instruction>(V))
        ReplList.push_back(Inst);
      Repl = InsertElementInst::Create(
          (Repl ? Repl : UndefValue::get(Vec->getType())), V,
          ConstantInt::get(Type::getInt32Ty(F->getContext()), Idx++), "",
          InsPoint);
    }
    return Repl;
  }

  void visitInstruction(Instruction &I) {
      BasicBlock *EntryBB = &*F->begin();
      Instruction &InsertPoint = (I.getParent() == EntryBB) ? I : EntryBB->back();
      for (unsigned It = 0, E = I.getNumOperands(); It != E; ++It) {
        Value *Op = I.getOperand(It);
        Value *ReplInst = nullptr;
        if (auto *CE = dyn_cast<ConstantExpr>(Op)) {
          ReplInst = lowerConstExprOperand(CE, &InsertPoint);
        } else if (auto *MDAsVal = dyn_cast<MetadataAsValue>(Op)) {
          ReplInst =  lowerConstMetadataOperand(MDAsVal->getMetadata(), InsertPoint);
        } else if (auto *Vec = dyn_cast<ConstantVector>(Op)) {
          ReplInst = lowerConstVectorOperand(Vec, I, InsertPoint, It);
        }
        if (ReplInst)
          I.setOperand(It, ReplInst);
      }
    }
};

class SPIRVLowerConstExpr : public FunctionPass {
public:
  SPIRVLowerConstExpr() : FunctionPass(ID), Ctx(nullptr) {
    initializeSPIRVLowerConstExprPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

  static char ID;

private:
  LLVMContext *Ctx;
};

char SPIRVLowerConstExpr::ID = 0;

bool SPIRVLowerConstExpr::runOnFunction(Function &F) {
  if (!SPIRVLowerConst)
    return false;

  Ctx = &F.getContext();

  LLVM_DEBUG(dbgs() << "Enter SPIRVLowerConstExpr:\n");
  LowerConstExprVisitor CEV(&F);
  CEV.visit(F);
  Module *M = F.getParent();
  verifyRegularizationPass(*M, "SPIRVLowerConstExpr");

  return true;
}

} // namespace SPIRV

INITIALIZE_PASS(SPIRVLowerConstExpr, "spv-lower-const-expr",
                "Regularize LLVM for SPIR-V", false, false)

FunctionPass *llvm::createSPIRVLowerConstExpr() {
  return new SPIRVLowerConstExpr();
}
