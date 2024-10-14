#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Type.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"

using namespace llvm;

namespace {
struct DecomposeGEPPass : public PassInfoMixin<DecomposeGEPPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    bool modified = false;
    const DataLayout &DL = F.getParent()->getDataLayout();

    for (auto &BB : F) {
      for (auto It = BB.begin(); It != BB.end();) {
        Instruction *I = &*It++;
        if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
          Value *basePtr = GEP->getPointerOperand();
          SmallVector<Value *, 8> Indices(GEP->idx_begin(), GEP->idx_end());

          APInt offset(DL.getPointerTypeSizeInBits(basePtr->getType()), 0);
          bool isConstantOffset = GEP->accumulateConstantOffset(DL, offset);

          if (isConstantOffset) {
            IRBuilder<> Builder(GEP);

            Value *baseInt = Builder.CreatePtrToInt(basePtr, Builder.getInt64Ty());

            Value *offsetInt = ConstantInt::get(Builder.getInt64Ty(), offset.getSExtValue());
            Value *addrInt = Builder.CreateAdd(baseInt, offsetInt);

            Value *newPtr = Builder.CreateIntToPtr(addrInt, GEP->getType());

            GEP->replaceAllUsesWith(newPtr);
            GEP->eraseFromParent();
            modified = true;
          } else {
            IRBuilder<> Builder(GEP);

            // currType을 가져오는 부분 수정
            Type *currType = GEP->getSourceElementType();

            Value *totalOffset = ConstantInt::get(Builder.getInt64Ty(), 0);

            for (unsigned i = 0; i < Indices.size(); ++i) {
              Value *Index = Indices[i];

              if (StructType *STy = dyn_cast<StructType>(currType)) {
                ConstantInt *CI = dyn_cast<ConstantInt>(Index);
                assert(CI && "Non-constant struct index is not supported");
                uint64_t FieldNo = CI->getZExtValue();
                const StructLayout *SL = DL.getStructLayout(STy);
                uint64_t FieldOffset = SL->getElementOffset(FieldNo);

                Value *FieldOffsetVal = ConstantInt::get(Builder.getInt64Ty(), FieldOffset);
                totalOffset = Builder.CreateAdd(totalOffset, FieldOffsetVal);

                currType = STy->getTypeAtIndex(FieldNo);
              } else {
                if (Index->getType()->isIntegerTy(32))
                  Index = Builder.CreateSExt(Index, Builder.getInt64Ty());
                else if (Index->getType()->isIntegerTy(64))
                  ;
                else
                  assert(false && "Unsupported index type");

                uint64_t TypeSize = DL.getTypeAllocSize(currType);
                Value *TypeSizeVal = ConstantInt::get(Builder.getInt64Ty(), TypeSize);

                Value *Offset = Builder.CreateMul(Index, TypeSizeVal);
                totalOffset = Builder.CreateAdd(totalOffset, Offset);

                if (currType->isArrayTy())
                  currType = currType->getArrayElementType();
                else if (currType->isPointerTy())
                  currType = GEP->getIndexedType(currType, ArrayRef<Value *>(Indices).slice(0, i+1));
                else
                  assert(false && "Unsupported type");
              }
            }

            Value *baseInt = Builder.CreatePtrToInt(basePtr, Builder.getInt64Ty());

            Value *addrInt = Builder.CreateAdd(baseInt, totalOffset);

            Value *newPtr = Builder.CreateIntToPtr(addrInt, GEP->getType());

            GEP->replaceAllUsesWith(newPtr);
            GEP->eraseFromParent();
            modified = true;
          }
        }
      }
    }

    if (modified)
      return PreservedAnalyses::none();
    else
      return PreservedAnalyses::all();
  }
};
} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DecomposeGEP", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "decompose-gep") {
                    FPM.addPass(DecomposeGEPPass());
                    return true;
                  }
                  return false;
                });
          }};
}
