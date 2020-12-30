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
#include <unordered_map>
#define DEBUG_TYPE "spv-lower-const-expr"

#include "OCLUtil.h"
#include "SPIRVInternal.h"
#include "SPIRVMDBuilder.h"
#include "SPIRVMDWalker.h"
#include "libSPIRV/SPIRVDebug.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"

#include <list>
#include <set>

using namespace llvm;
using namespace SPIRV;
using namespace OCLUtil;

namespace SPIRV {

cl::opt<bool> SPIRVLowerConst(
    "spirv-lower-const-expr", cl::init(true),
    cl::desc("LLVM/SPIR-V translation enable lowering constant expression"));

struct LowerConstExprVisitor : public InstVisitor<LowerConstExprVisitor> {
  std::unordered_map<ConstantExpr *, Instruction *> ConstExprMap;
  Function *F = nullptr;
  LowerConstExprVisitor(Function *Fun): F(Fun){}

  /// Since SPIR-V cannot represent constant expression, constant expressions
  /// in LLVM needs to be lowered to instructions.
  /// For each function, the constant expressions used by instructions of the
  /// function are replaced by instructions placed in the entry block since it
  /// dominates all other BB's. Each constant expression only needs to be lowered
  /// once in each function and all uses of it by instructions in that function
  /// is replaced by one instruction.
  /// ToDo: remove redundant instructions for common subexpression
  
  Instruction * lowerOp(ConstantExpr *CE, Instruction *InsPoint) {
    SPIRVDBG(dbgs() << "[lowerConstantExpressions] " << *CE;)
    Instruction *ReplInst = CE->getAsInstruction();
    ReplInst->insertBefore(InsPoint);
    SPIRVDBG(dbgs() << " -> " << *ReplInst << '\n';)
    return ReplInst;
  }
  
  
  Instruction *
  lowerConstExprOperand(ConstantExpr *CE, Instruction *InsPoint) {
    auto It = ConstExprMap.find(CE);
    if (It != ConstExprMap.end())
      return It->second;
    std::unordered_map<unsigned, Instruction *> OpMap;
    for (unsigned I = 0, E = CE->getNumOperands(); I != E; ++I) {
      if (ConstantExpr *CSE = dyn_cast_or_null<ConstantExpr>(CE->getOperand(I))) {
        OpMap[I] = lowerConstExprOperand(CSE, InsPoint);
      }
    }
    Instruction *NewInst = lowerOp(CE, InsPoint);
    for (auto &It : OpMap) {
      NewInst->setOperand(It.first, It.second);
    }
    ConstExprMap[CE] = NewInst;
    return NewInst;
  }
  
  Value * lowerConstMetadataOperand(Metadata *MD, Instruction &InsertPoint) {
    if (auto ConstMD = dyn_cast<ConstantAsMetadata>(MD)) {
      Constant *C = ConstMD->getValue();
      if (auto CE = dyn_cast<ConstantExpr>(C)) {
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
                   [&](Value *V) { return lowerConstExprOperand(static_cast<ConstantExpr*>(V), &InsertPoint); });
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
      //Function *F = I.getParent()->getParent();
      BasicBlock *EntryBB = &*F->begin();
      Instruction &InsertPoint = (I.getParent() == EntryBB) ? I : EntryBB->back();
      for (unsigned It = 0, E = I.getNumOperands(); It != E; ++It) {
        Value *Op = I.getOperand(It);
        if (auto *CE = dyn_cast<ConstantExpr>(Op)) {
          I.setOperand(It, lowerConstExprOperand(CE, &InsertPoint));
        } else if (auto *MDAsVal = dyn_cast<MetadataAsValue>(Op)) {
          if (Value *ReplInst =  lowerConstMetadataOperand(MDAsVal->getMetadata(), InsertPoint))
            I.setOperand(It, ReplInst);
        } else if (auto *Vec = dyn_cast<ConstantVector>(Op)) {
          if (Value *ReplInst = lowerConstVectorOperand(Vec, I, InsertPoint, It))
            I.setOperand(It, ReplInst);
        }
      }
    }
};

//void SPIRVLowerConstExpr::visit(Function &F) {
//  processFunction(F);
//}
//void SPIRVLowerConstExpr::visit(Module *M) {
//  for (Function &F : M->functions()) {
//    if (!F.empty())
//      processFunction(F);
//  }
//
//  return;
//
//  for (auto &I : M->functions()) {
//    std::list<Instruction *> WorkList;
//    std::unordered_map<ConstantExpr *, Instruction *> ConstExprMap;
//    for (auto &BI : I) {
//      for (auto &II : BI) {
//        SPIRVDBG(dbgs() << "Adding " << II << '\n';)
//        WorkList.push_back(&II);
//      }
//    }
//    auto FBegin = I.begin();
//    while (!WorkList.empty()) {
//      auto II = WorkList.front();
//
//      auto LowerOp = [&II, &FBegin, &I, &ConstExprMap](Value *V) -> Value * {
//        if (isa<Function>(V))
//          return V;
//        auto *CE = cast<ConstantExpr>(V);
//        SPIRVDBG(dbgs() << "[lowerConstantExpressions] " << *CE;)
//        auto it = ConstExprMap.find(CE);
//        Instruction *ReplInst = nullptr;
//        if (it != ConstExprMap.end()) {
//          ReplInst = it->second;
//        } else {
//          ReplInst = CE->getAsInstruction();
//          ConstExprMap[CE] = ReplInst;
//          auto InsPoint = II->getParent() == &*FBegin ? II : &FBegin->back();
//          ReplInst->insertBefore(InsPoint);
//        }
//        SPIRVDBG(dbgs() << " -> " << *ReplInst << '\n';)
//        std::vector<Instruction *> Users;
//        // Do not replace use during iteration of use. Do it in another loop
//        for (auto U : CE->users()) {
//          SPIRVDBG(dbgs() << "[lowerConstantExpressions] Use: " << *U << '\n';)
//          if (auto InstUser = dyn_cast<Instruction>(U)) {
//            // Only replace users in scope of current function
//            if (InstUser->getParent()->getParent() == &I)
//              Users.push_back(InstUser);
//          }
//        }
//        for (auto &User : Users)
//          User->replaceUsesOfWith(CE, ReplInst);
//        return ReplInst;
//      };
//
//      WorkList.pop_front();
//      for (unsigned OI = 0, OE = II->getNumOperands(); OI != OE; ++OI) {
//        auto Op = II->getOperand(OI);
//        auto *Vec = dyn_cast<ConstantVector>(Op);
//        if (Vec && std::all_of(Vec->op_begin(), Vec->op_end(), [](Value *V) {
//              return isa<ConstantExpr>(V) || isa<Function>(V);
//            })) {
//          // Expand a vector of constexprs and construct it back with series of
//          // insertelement instructions
//          std::list<Value *> OpList;
//          std::transform(Vec->op_begin(), Vec->op_end(),
//                         std::back_inserter(OpList),
//                         [LowerOp](Value *V) { return LowerOp(V); });
//          Value *Repl = nullptr;
//          unsigned Idx = 0;
//          auto *PhiII = dyn_cast<PHINode>(II);
//          auto *InsPoint = PhiII ? &PhiII->getIncomingBlock(OI)->back() : II;
//          std::list<Instruction *> ReplList;
//          for (auto V : OpList) {
//            if (auto *Inst = dyn_cast<Instruction>(V))
//              ReplList.push_back(Inst);
//            Repl = InsertElementInst::Create(
//                (Repl ? Repl : UndefValue::get(Vec->getType())), V,
//                ConstantInt::get(Type::getInt32Ty(M->getContext()), Idx++), "",
//                InsPoint);
//          }
//          II->replaceUsesOfWith(Op, Repl);
//          WorkList.splice(WorkList.begin(), ReplList);
//        } else if (auto CE = dyn_cast<ConstantExpr>(Op)) {
//          WorkList.push_front(cast<Instruction>(LowerOp(CE)));
//        } else if (auto MDAsVal = dyn_cast<MetadataAsValue>(Op)) {
//          Metadata *MD = MDAsVal->getMetadata();
//          if (auto ConstMD = dyn_cast<ConstantAsMetadata>(MD)) {
//            Constant *C = ConstMD->getValue();
//            if (auto CE = dyn_cast<ConstantExpr>(C)) {
//              Value *RepInst = LowerOp(CE);
//              Metadata *RepMD = ValueAsMetadata::get(RepInst);
//              Value *RepMDVal = MetadataAsValue::get(M->getContext(), RepMD);
//              II->setOperand(OI, RepMDVal);
//              WorkList.push_front(cast<Instruction>(RepInst));
//            }
//          }
//        }
//      }
//    }
//  }
//}

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
