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
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Attributes.h"
#include <vector>
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
  bool processMachineBasicBlock(MachineBasicBlock &MBB,
                                const TargetInstrInfo* TII,
                                bool isFirstBlock);
  void insertBlockStartDITSet(MachineBasicBlock &MBB,
                              MachineInstr &insertBefore,
                              const TargetInstrInfo* TII);
  void insertBlockEndDITUnset(MachineBasicBlock &MBB,
                              MachineInstr &insertBefore,
                              const TargetInstrInfo* TII);
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
                AARCH64_DIT_NAME, false, false)

bool AArch64DIT::processMachineBasicBlock(MachineBasicBlock &MBB,
                                          const TargetInstrInfo* TII,
                                          bool isFirstBlock) {
  bool blockChanged = false;
  bool inFrameSetup = false;

  std::vector<MachineInstr*> setTargets;
  std::vector<MachineInstr*> unsetTargets;

  for (auto It = MBB.begin(); It != MBB.end(); ++It) {
    auto &MI = *It;

    bool curFS = MI.getFlag(MachineInstr::MIFlag::FrameSetup);
    if (!curFS && inFrameSetup) {
      setTargets.push_back(&MI);
      blockChanged = true;
    }
    inFrameSetup = curFS;

    if (unsetTargets.empty() && MI.getFlag(MachineInstr::MIFlag::FrameDestroy)) {
      unsetTargets.push_back(&MI);
      blockChanged = true;
    }
  }

  if (isFirstBlock && setTargets.empty()) {
    setTargets.push_back(&(*MBB.begin()));
  }

  for (auto MI : setTargets) {
    errs() << *MI;
    insertBlockStartDITSet(MBB, *MI, TII);
  }

  for (auto MI : unsetTargets) {
    errs() << *MI;
    insertBlockEndDITUnset(MBB, *MI, TII);
  }

  return blockChanged;
}

void AArch64DIT::insertBlockStartDITSet(MachineBasicBlock &MBB,
                                        MachineInstr &insertBefore,
                                        const TargetInstrInfo* TII) {
  // BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::SUBXri))
  //   .addDef(AArch64::SP)
  //   .addUse(AArch64::SP)
  //   .addImm(16)
  //   .addImm(0);

  // BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::STRXui))
  //   .addReg(AArch64::X14)
  //   .addReg(AArch64::SP)
  //   .addImm(0);

  BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::MRS))
    .addReg(AArch64::X14)
    .addImm(AArch64SysReg::DIT);

  // BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::STRXui))
  //   .addReg(AArch64::X14)
  //   .addReg(AArch64::SP)
  //   .addImm(1);

  BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::MSR))
    .addImm(AArch64SysReg::DIT)
    .addImm(1);

  BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::DSB))
    .addImm(0xf);
  BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::ISB))
    .addImm(0xf);

  // BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::LDRXui))
  //   .addReg(AArch64::X14)
  //   .addReg(AArch64::SP)
  //   .addImm(0);
}

void AArch64DIT::insertBlockEndDITUnset(MachineBasicBlock &MBB,
                                        MachineInstr &insertBefore,
                                        const TargetInstrInfo* TII) {
  // BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::SUBXri))
  //   .addDef(AArch64::SP)
  //   .addUse(AArch64::SP)
  //   .addImm(16)
  //   .addImm(0);

  // BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::STRXui))
  //   .addDef(AArch64::X14)
  //   .addUse(AArch64::SP)
  //   .addImm(0);

  // BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::LDRXui))
  //   .addReg(AArch64::X14)
  //   .addReg(AArch64::SP)
  //   .addImm(3);

  BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::MSR))
    .addImm(AArch64SysReg::DIT)
    .addReg(AArch64::X14);

  // BuildMI(MBB, insertBefore, insertBefore.getDebugLoc(), TII->get(AArch64::ADDXri))
  //   .addDef(AArch64::SP)
  //   .addUse(AArch64::SP)
  //   .addImm(16)
  //   .addImm(0);
}

bool AArch64DIT::runOnMachineFunction(MachineFunction &MF) {
  if (!MF.getFunction().hasFnAttribute(llvm::Attribute::AttrKind::DITProtected))
    return false;

  TRI = MF.getSubtarget().getRegisterInfo();
  TII = MF.getSubtarget().getInstrInfo();
  MRI = &MF.getRegInfo();

  LLVM_DEBUG(dbgs() << "***** AArch64DIT ****\n");

  bool changed = false;
  for (auto It = MF.begin(); It != MF.end(); ++It) {
    auto &MBB = *It;
    bool isFirst = It == MF.begin();

    changed = processMachineBasicBlock(MBB, TII, isFirst) || changed;
  }

  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      errs() << MI;
    }
  }

  return changed;
}

FunctionPass *llvm::createAArch64DITPass() {
  return new AArch64DIT();
}
