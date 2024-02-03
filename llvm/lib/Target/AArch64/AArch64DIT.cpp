//==-- AArch64DIT.cpp - Pass for enabling constant time DIT via attribute --==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file When marked via attribute, set DIT and barrier to ensure constant
/// time execution of function contents.
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64RegisterInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "aarch64-dit"

#define AARCH64_DIT_NAME "AArch64 DIT"

namespace {
class AArch64DIT : public MachineFunctionPass {
private:
  const TargetRegisterInfo *TRI;
  const MachineRegisterInfo *MRI;
  const TargetInstrInfo *TII;
  bool Changed;
  void processMachineBasicBlock(MachineBasicBlock &MBB);
public:
  static char ID; // Pass identification, replacement for typeid.
  AArch64DIT() : MachineFunctionPass(ID) {
    initializeAArch64DITPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  StringRef getPassName() const override { return AARCH64_DIT_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
char AArch64DIT::ID = 0;
} // end anonymous namespace

INITIALIZE_PASS(AArch64DIT, "aarch64-dit",
                AARCH64_DEAD_REG_DEF_NAME, false, false)

static bool usesFrameIndex(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.uses())
    if (MO.isFI())
      return true;
  return false;
}
