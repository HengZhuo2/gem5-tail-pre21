/*
 * Copyright (c) 2019 Metempsy Technology LSC
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "arch/arm/self_debug.hh"

#include "arch/arm/faults.hh"
#include "arch/arm/miscregs_types.hh"
#include "base/bitfield.hh"

using namespace ArmISA;
using namespace std;

Fault
SelfDebug::testDebug(ThreadContext *tc, const RequestPtr &req,
                     BaseTLB::Mode mode)
{
    Fault fault = NoFault;

    if (mode == BaseTLB::Execute) {
        const bool d_step = softStep->advanceSS(tc);
        if (!d_step) {
            fault = testVectorCatch(tc, req->getVaddr(), nullptr);
            if (fault == NoFault)
                fault = testBreakPoints(tc, req->getVaddr());
        }
    } else if (!req->isCacheMaintenance() ||
             (req->isCacheInvalidate() && !req->isCacheClean())) {
        bool md = mode == BaseTLB::Write ? true: false;
        fault = testWatchPoints(tc, req->getVaddr(), md,
                                req->isAtomic(),
                                req->getSize(),
                                req->isCacheMaintenance());
    }

    return fault;
}

Fault
SelfDebug::testBreakPoints(ThreadContext *tc, Addr vaddr)
{
    if (!enableFlag)
        return NoFault;

    setAArch32(tc);

    to32 = targetAArch32(tc);

    init(tc);

    if (!isDebugEnabled(tc))
        return NoFault;

    ExceptionLevel el = (ExceptionLevel) currEL(tc);
    for (auto &p: arBrkPoints){
        PCState pcst = tc->pcState();
        Addr pc = vaddr;
        if (pcst.itstate() != 0x0)
            pc = pcst.pc();
        if (p.enable && p.isActive(pc) &&(!to32 || !p.onUse)) {
            const DBGBCR ctr = p.getControlReg(tc);
            if (p.isEnabled(tc, el, ctr.hmc, ctr.ssc, ctr.pmc)) {
                bool debug = p.test(tc, pc, el, ctr, false);
                if (debug){
                    if (to32)
                        p.onUse = true;
                    return triggerException(tc, pc);
                }
            }
        }
    }
    return NoFault;
}


Fault
SelfDebug::triggerException(ThreadContext *tc, Addr vaddr)
{
    if (to32) {
        return std::make_shared<PrefetchAbort>(vaddr,
                                   ArmFault::DebugEvent, false,
                                   ArmFault::UnknownTran,
                                   ArmFault::BRKPOINT);
    } else {
        return std::make_shared<HardwareBreakpoint>(vaddr, 0x22);
    }
}

Fault
SelfDebug::testWatchPoints(ThreadContext *tc, Addr vaddr, bool write,
                           bool atomic, unsigned size, bool cm)
{
    setAArch32(tc);
    to32 = targetAArch32(tc);
    if (!initialized)
        init(tc);
    if (!isDebugEnabled(tc) || !enableFlag)
        return NoFault;

    ExceptionLevel el = (ExceptionLevel) currEL(tc);
    int idxtmp = -1;
    for (auto &p: arWatchPoints){
        idxtmp ++;
        if (p.enable) {
            bool debug = p.test(tc, vaddr, el, write, atomic, size);
            if (debug){
                return triggerWatchpointException(tc, vaddr, write, cm);
            }
        }
    }
    return NoFault;
}

Fault
SelfDebug::triggerWatchpointException(ThreadContext *tc, Addr vaddr,
                                      bool write, bool cm)
{
    if (to32) {
        ArmFault::DebugType d = cm? ArmFault::WPOINT_CM:
                                    ArmFault::WPOINT_NOCM;
        return std::make_shared<DataAbort>(vaddr,
                                           TlbEntry::DomainType::NoAccess,
                                           write, ArmFault::DebugEvent, cm,
                                           ArmFault::UnknownTran, d);
    } else {
        return std::make_shared<Watchpoint>(0, vaddr, write, cm);
    }
}

bool
SelfDebug::isDebugEnabledForEL64(ThreadContext *tc, ExceptionLevel el,
                         bool secure, bool mask)
{
    bool route_to_el2 =  ArmSystem::haveEL(tc, EL2) &&
        !secure && enableTdeTge;

    ExceptionLevel target_el = route_to_el2 ? EL2 : EL1;
    if (oslk || (bSDD && secure && ArmSystem::haveEL(tc, EL3))) {
        return false;
    }

    if (el == target_el) {
        return bKDE  && !mask;
    } else {
        return target_el > el;
    }
}

bool
SelfDebug::isDebugEnabledForEL32(ThreadContext *tc, ExceptionLevel el,
                         bool secure, bool mask)
{
    if (el == EL0 && !ELStateUsingAArch32(tc, EL1, secure)) {
        return isDebugEnabledForEL64(tc, el, secure, mask);
    }

    if (oslk) {
        return false;
    }

    bool enabled;
    if (secure && ArmSystem::haveEL(tc, EL3)) {
        // We ignore the check for invasive External debug checking SPIDEN
        // and DBGEN signals. They are not implemented
        bool spd32 = bits(tc->readMiscReg(MISCREG_MDCR_EL3), 14);
        enabled = spd32;

        bool suiden = bits(tc->readMiscReg(MISCREG_SDER), 0);
        enabled  = el == EL0 ? (enabled || suiden) : enabled;
    } else {
        enabled = el != EL2;
    }
    return enabled;
}

bool
BrkPoint::testLinkedBk(ThreadContext *tc, Addr vaddr, ExceptionLevel el)
{
    bool debug = false;
    const DBGBCR ctr = getControlReg(tc);
    if ((ctr.bt & 0x1) && enable) {
        debug = test(tc, vaddr, el, ctr, true);
    }
    return debug;
}

bool
BrkPoint::test(ThreadContext *tc, Addr pc, ExceptionLevel el, DBGBCR ctr,
               bool from_link)
{
    bool v = false;
    switch (ctr.bt) {
      case 0x0:
        v = testAddrMatch(tc, pc, ctr.bas);
        break;

      case 0x1:
        v = testAddrMatch(tc, pc, ctr.bas); // linked
        if (v) {
            v = (conf->getBrkPoint(ctr.lbn))->testLinkedBk(tc, pc, el);
        }
        break;

      case 0x2:
        {
            bool host = ELIsInHost(tc, el);
            v = testContextMatch(tc, !host, true);
        }
        break;

      case 0x3:
        if (from_link){
            bool host = ELIsInHost(tc, el);
            v = testContextMatch(tc, !host, true);
        }
        break;

      case 0x4:
        v = testAddrMissMatch(tc, pc, ctr.bas);
        break;

      case 0x5:
        v = testAddrMissMatch(tc, pc, ctr.bas); // linked
        if (v && !from_link)
            v = v && (conf->getBrkPoint(ctr.lbn))->testLinkedBk(tc, pc, el);
        break;

      case 0x6:
        if (HaveVirtHostExt(tc) && !ELIsInHost(tc, el))
             v = testContextMatch(tc, true);
        break;

      case 0x7:
        if (HaveVirtHostExt(tc) && !ELIsInHost(tc, el) && from_link)
            v = testContextMatch(tc, true);
        break;

      case 0x8:
        if (ArmSystem::haveEL(tc, EL2) && !ELIsInHost(tc, el)) {
            v = testVMIDMatch(tc);
        }
        break;

      case 0x9:
        if (from_link && ArmSystem::haveEL(tc, EL2) &&
            !ELIsInHost(tc, el)) {
            v = testVMIDMatch(tc);
        }
        break;

      case 0xa:
        if (ArmSystem::haveEL(tc, EL2) && !ELIsInHost(tc, el)) {
            v = testContextMatch(tc, true);
            if (v && !from_link)
                 v = v && testVMIDMatch(tc);
        }
        break;
      case 0xb:
        if (from_link && ArmSystem::haveEL(tc, EL2) &&
            !ELIsInHost(tc, el)) {
            v = testContextMatch(tc, true);
            v = v && testVMIDMatch(tc);
        }
        break;

      case 0xc:
        if (HaveVirtHostExt(tc) && !inSecureState(tc))
            v = testContextMatch(tc, false);
        break;

      case 0xd:
        if (HaveVirtHostExt(tc) && from_link && !inSecureState(tc))
            v = testContextMatch(tc, false);
        break;

      case 0xe:
        if (HaveVirtHostExt(tc) && !ELIsInHost(tc, el)
                && !inSecureState(tc) ) {
            v = testContextMatch(tc, true); // CONTEXTIDR_EL1
            v = v && testContextMatch(tc, false); // CONTEXTIDR_EL2
        }
        break;
      case 0xf:
        if (HaveVirtHostExt(tc) && !ELIsInHost(tc, el) && from_link
                && !inSecureState(tc) ) {
            v = testContextMatch(tc, true); // CONTEXTIDR_EL1
            v = v && testContextMatch(tc, false); // CONTEXTIDR_EL2
        }
        break;
      default:
        break;
    }
    return v;
}

void
SelfDebug::init(ThreadContext *tc)
{
    if (initialized)
        return;
    CPSR cpsr = tc->readMiscReg(MISCREG_CPSR);
    aarch32 = cpsr.width == 1;

    const AA64DFR0 dfr = tc->readMiscReg(MISCREG_ID_AA64DFR0_EL1);
    const AA64MMFR2 mm_fr2 = tc->readMiscReg(MISCREG_ID_AA64MMFR2_EL1);
    const AA64MMFR1 mm_fr1 = tc->readMiscReg(MISCREG_ID_AA64MMFR1_EL1);

    for (int i = 0; i <= dfr.brps; i++) {
        const bool isctxaw = i >= (dfr.brps - dfr.ctx_cmps);

        BrkPoint bkp = BrkPoint((MiscRegIndex)(MISCREG_DBGBCR0_EL1 + i),
                                (MiscRegIndex)(MISCREG_DBGBVR0_EL1 + i),
                                this, isctxaw, (bool)mm_fr2.varange,
                                mm_fr1.vmidbits, aarch32);
        const DBGBCR ctr = tc->readMiscReg(MISCREG_DBGBCR0_EL1 + i);

        bkp.updateControl(ctr);
        arBrkPoints.push_back(bkp);
    }

    for (int i = 0; i <= dfr.wrps; i++) {
        WatchPoint wtp = WatchPoint((MiscRegIndex)(MISCREG_DBGWCR0 + i),
                                    (MiscRegIndex)(MISCREG_DBGWVR0 + i),
                                    this, (bool)mm_fr2.varange, aarch32);
        const DBGWCR ctr = tc->readMiscReg(MISCREG_DBGWCR0 + i);

        wtp.updateControl(ctr);
        arWatchPoints.push_back(wtp);
    }

    initialized = true;

    RegVal oslar_el1 = tc->readMiscReg(MISCREG_OSLAR_EL1);
    updateOSLock(oslar_el1);
    // Initialize preloaded control booleans
    uint64_t mdscr_el1 = tc->readMiscReg(MISCREG_MDSCR_EL1);
    setMDSCRvals(mdscr_el1);

    const uint64_t mdcr_el3 = tc->readMiscReg(MISCREG_MDCR_EL3);
    setbSDD(mdcr_el3);

    const HCR hcr = tc->readMiscReg(MISCREG_HCR_EL2);
    const HDCR mdcr  = tc->readMiscRegNoEffect(MISCREG_MDCR_EL2);
    setenableTDETGE(hcr, mdcr);

    // Enable Vector Catch Exceptions
    const DEVID dvid = tc->readMiscReg(MISCREG_DBGDEVID0);
    vcExcpt = new VectorCatch(dvid.vectorcatch==0x0, this);
}

bool
BrkPoint::testAddrMatch(ThreadContext *tc, Addr in_pc, uint8_t bas)
{
    Addr pc_tocmp = getAddrfromReg(tc);
    Addr pc = bits(in_pc, maxAddrSize, 2);

    bool prs = true;
    CPSR cpsr = tc->readMiscReg(MISCREG_CPSR);
    bool thumb = cpsr.t;

    if (thumb) {
        if (bas == 0xc)
            prs = bits(in_pc, 1, 0) == 0x2;
        else if (bas == 0x3)
            prs = bits(in_pc, 1, 0) == 0x0;
    }
    return (pc == pc_tocmp) && prs;
}

bool
BrkPoint::testAddrMissMatch(ThreadContext *tc, Addr in_pc, uint8_t bas)
{
    if (bas == 0x0)
        return true;
    Addr pc_tocmp = getAddrfromReg(tc);
    Addr pc = bits(in_pc, maxAddrSize, 2);
    bool prs = false;
    CPSR cpsr = tc->readMiscReg(MISCREG_CPSR);
    bool thumb = cpsr.t;

    if (thumb) {
        if (bas == 0xc)
            prs = bits(in_pc, 1, 0) == 0x2;
        else if (bas == 0x3)
            prs = bits(in_pc, 1, 0) == 0x0;
    }
    return (pc != pc_tocmp) && !prs;
}

bool
BrkPoint::testContextMatch(ThreadContext *tc, bool ctx1)
{
    return testContextMatch(tc, ctx1, ctx1);
}

bool
BrkPoint::testContextMatch(ThreadContext *tc, bool ctx1, bool low_ctx)
{
    if (!isCntxtAware)
        return false;
    MiscRegIndex miscridx;
    ExceptionLevel el = currEL(tc);
    bool a32 = conf->isAArch32();

    if (ctx1) {
        miscridx = a32? MISCREG_CONTEXTIDR : MISCREG_CONTEXTIDR_EL1;
        if ((el == EL3 && !a32) || el == EL2)
            return false;
    } else {
        miscridx = MISCREG_CONTEXTIDR_EL2;
        if (el == EL2 && a32)
            return false;
    }

    RegVal ctxid = bits(tc->readMiscReg(miscridx), 31, 0);
    RegVal v = getContextfromReg(tc, low_ctx);
    return (v == ctxid);
}

bool
BrkPoint::testVMIDMatch(ThreadContext *tc)
{
    uint32_t vmid_index = 55;
    if (VMID16enabled)
        vmid_index = 63;
    ExceptionLevel el = currEL(tc);
    if (el == EL2)
        return false;

    uint32_t vmid = bits(tc->readMiscReg(MISCREG_VTTBR_EL2), vmid_index, 48);
    uint32_t v = getVMIDfromReg(tc);
    return (v == vmid);
}


bool
BrkPoint::isEnabled(ThreadContext *tc, ExceptionLevel el,
                    uint8_t hmc, uint8_t ssc, uint8_t pmc)
{
    bool v;
    bool aarch32 = conf->isAArch32();
    bool no_el2 = !ArmSystem::haveEL(tc, EL2);
    bool no_el3 = !ArmSystem::haveEL(tc, EL3);

    if (no_el3 && !no_el2 && (ssc == 0x1 || ssc == 0x2) &&
        !(hmc && ssc == 0x1 && pmc == 0x0)) {
        return false;
    } else if (no_el3 && no_el2 && (hmc != 0x0 || ssc != 0x0) &&
        !(!aarch32 && ((hmc && ssc == 0x1  && pmc == 0x0) || ssc == 0x3))) {
        return false;
    } else if (no_el2 && hmc && ssc == 0x3 && pmc == 0x0) {
        return false;
    } else if (ssc == 0x11 && pmc == 0x1 &&
        !(!aarch32 && hmc && ssc == 0x3 && pmc == 0x0)) {
        // AND secureEL2 not implemented
        return false;
    } else if (hmc && ssc == 0x1 && pmc == 0x0) {
        //AND secureEL2 not implemented
        return false;
    }
    switch (el) {
        case EL0:
            v = (pmc == 0x3) || (pmc == 0x2 && hmc == 0x0);
            if (aarch32)
                v = v || (pmc == 0x0 && ssc != 0x3 && hmc == 0x0);
            if (v && ssc == 0x3)
                panic("Unexpected EL in SelfDebug::isDebugEnabled.\n");
            break;
        case EL1:
            v = (pmc == 0x3) || (pmc == 0x1);
            if (aarch32)
                v = v || (pmc == 0x0 && hmc == 0x0 && ssc !=0x3);
            break;
        case EL2:
            v = (ssc == 0x3) ||
                ((hmc == 0x1) && !((ssc == 0x2) && (pmc == 0x0)));
            if (v && pmc == 0x2)
                panic("Unexpected EL in SelfDebug::isDebugEnabled.\n");
            break;
        case EL3:
            if (ssc == 0x1)
                panic("Unexpected EL in SelfDebug::isDebugEnabled.\n");
            v = (hmc == 0x1) & (ssc != 0x3);
            break;
        default:
            panic("Unexpected EL %d in BrkPoint::isEnabled.\n", el);
    }
    return v && SelfDebug::securityStateMatch(tc, ssc, hmc || !aarch32);
}

uint32_t
BrkPoint::getVMIDfromReg(ThreadContext *tc)
{
    uint32_t vmid_index = 39;
    if (VMID16enabled)
        vmid_index = 47;
    return bits(tc->readMiscReg(valRegIndex), vmid_index, 32);
}


bool
WatchPoint::isEnabled(ThreadContext* tc, ExceptionLevel el,
                      bool hmc, uint8_t ssc, uint8_t pac)
{

    bool v;
    bool aarch32 = conf->isAArch32();
    bool no_el2 = !ArmSystem::haveEL(tc, EL2);
    bool no_el3 = !ArmSystem::haveEL(tc, EL3);

    if (aarch32) {
        // WatchPoint PL2 using aarch32 is disabled except for
        // debug state. Check G2-5395 table G2-15.
        if (el == EL2)
            return false;
        if (no_el3) {
            if (ssc == 0x01 || ssc == 0x02 ){
                return false;
            } else if (no_el2 &&
                      ((!hmc && ssc == 0x3) || (hmc && ssc == 0x0))) {
                return false;
            }
        }
        if (no_el2 && hmc && ssc == 0x03 && pac == 0)
            return false;
    }
    switch (el) {
      case EL0:
        v = (pac == 0x3 || (pac == 0x2 && !hmc && ssc != 0x3));
        break;
      case EL1:
        v = (pac == 0x1 || pac == 0x3);
        break;
      case EL2:
        v = (hmc && (ssc != 0x2 || pac != 0x0));
        break;
      case EL3:
        v = (hmc && (ssc == 0x2 ||
            (ssc == 0x1 && (pac == 0x1 || pac == 0x3))));
        break;
      default:
        panic("Unexpected EL in WatchPoint::isEnabled.\n");
    }
    return v && SelfDebug::securityStateMatch(tc, ssc, hmc);
}

bool
WatchPoint::test(ThreadContext *tc, Addr addr, ExceptionLevel el, bool& wrt,
                 bool atomic, unsigned size)
{

    bool v = false;
    const DBGWCR ctr = tc->readMiscReg(ctrlRegIndex);
    if (isEnabled(tc, el, ctr.hmc, ctr.ssc, ctr.pac) &&
        ((wrt && (ctr.lsv & 0x2)) || (!wrt && (ctr.lsv & 0x1)) || atomic)) {
        v = compareAddress(tc, addr, ctr.bas, ctr.mask, size);
        if (ctr.wt) {
            v = v && (conf->getBrkPoint(ctr.lbn))->testLinkedBk(tc, addr, el);
        }
    }
    if (atomic && (ctr.lsv & 0x1)) {
        wrt = false;
    }
    return v;
}

bool
WatchPoint::compareAddress(ThreadContext *tc, Addr in_addr, uint8_t bas,
        uint8_t mask, unsigned size)
{
    Addr addr_tocmp = getAddrfromReg(tc);
    int maxbits = isDoubleAligned(addr_tocmp) ? 4: 8;
    int bottom = isDoubleAligned(addr_tocmp) ? 2: 3;
    Addr addr = bits(in_addr, maxAddrSize, 0);

    if (bas == 0x0)
        return false;

    if (mask == 0x0) {
        for (int i = 0; i < maxbits; i++) {
            uint8_t bas_m = 0x1 << i;
            uint8_t masked_bas = bas & bas_m;
            if (masked_bas == bas_m) {
                uint8_t off = log2(masked_bas);
                Addr cmpaddr = addr_tocmp | off;
                for (int j = 0; j < size; j++) {
                    if ((addr + j) == cmpaddr) {
                        return true;
                    }
                }
            }
        }
        return false;
    } else {
        bool v = false;
        for (int j = 0; j < size; j++) {
            Addr compaddr;
            if (mask > bottom) {
                addr = bits((in_addr+j), maxAddrSize, mask);
                compaddr = bits(addr_tocmp, maxAddrSize, mask);
            } else {
                addr = bits((in_addr+j), maxAddrSize, bottom);
                compaddr = bits(addr_tocmp, maxAddrSize, bottom);
            }
            v = v || (addr == compaddr);
        }
        return v;
    }
}

bool
SoftwareStep::debugExceptionReturnSS(ThreadContext *tc, CPSR spsr,
                                     ExceptionLevel dest, bool aarch32)
{
    bool SS_bit = false;
    bool enabled_src = false;
    if (bSS) {
        enabled_src = conf->isDebugEnabled(tc);

        bool enabled_dst = false;
        bool secure = isSecureBelowEL3(tc) || dest == EL3;
        CPSR cpsr = tc->readMiscReg(MISCREG_CPSR);
        if (cpsr.width) {
            enabled_dst = conf->isDebugEnabledForEL32(tc, dest, secure,
                                                      spsr.d == 1);
        } else {
            enabled_dst = conf->isDebugEnabledForEL64(tc, dest, secure,
                                                      spsr.d == 1);
        }
        ExceptionLevel ELd = debugTargetFrom(tc, secure);

        if (!ELIs32(tc, ELd) && !enabled_src && enabled_dst) {
            SS_bit = spsr.ss;
            if (SS_bit == 0x0) {
                stateSS = ACTIVE_PENDING_STATE;
            } else {
                stateSS = ACTIVE_NOT_PENDING_STATE;
            }
        }
    }
    return SS_bit;
}

bool
SoftwareStep::advanceSS(ThreadContext * tc)
{

    PCState pc = tc->pcState();
    bool res = false;
    switch (stateSS) {
      case INACTIVE_STATE:
        pc.debugStep(false);
        break;

      case ACTIVE_NOT_PENDING_STATE:
        pc.debugStep(false);
        if (cpsrD == 1 || !bSS) {
            stateSS = INACTIVE_STATE;
        } else {
            pc.stepped(true);
            stateSS = ACTIVE_PENDING_STATE;
            tc->pcState(pc);
        }
        break;

      case ACTIVE_PENDING_STATE:
        if (!cpsrD && bSS) {
            pc.debugStep(true);
            res = true;
            tc->pcState(pc);
        }
        stateSS = INACTIVE_STATE;
        clearLdx();
        break;

      default:
        break;
    }
    return res;
}

Fault
SelfDebug::testVectorCatch(ThreadContext *tc, Addr addr,
                           ArmFault *fault)
{

    setAArch32(tc);
    to32 = targetAArch32(tc);
    if (!initialized)
        init(tc);
    if (!isDebugEnabled(tc) || !enableFlag || !aarch32)
        return NoFault;

    ExceptionLevel el = (ExceptionLevel) currEL(tc);
    bool debug;
    if (fault == nullptr)
        debug = vcExcpt->addressMatching(tc, addr, el);
    else
        debug = vcExcpt->exceptionTrapping(tc, el, fault);
    if (debug) {
        if (enableTdeTge) {
            return std::make_shared<HypervisorTrap>(0, 0x22,
                                        EC_PREFETCH_ABORT_TO_HYP);
        } else {
            return std::make_shared<PrefetchAbort>(addr,
                                       ArmFault::DebugEvent, false,
                                       ArmFault::UnknownTran,
                                       ArmFault::VECTORCATCH);
        }
    }

    return NoFault;
}

bool
VectorCatch::addressMatching(ThreadContext *tc, Addr addr, ExceptionLevel el)
{
    // Each bit position in this string corresponds to a bit in DBGVCR
    // and an exception vector.
    bool enabled;
    if (conf->isAArch32() && ELIs32(tc, EL1) &&
        (addr & 0x3) == 0 && el != EL2 ) {

        DBGVCR match_word = 0x0;

        Addr vbase = getVectorBase(tc, false);
        Addr vaddress = addr & ~ 0x1f;
        Addr low_addr = bits(addr, 5, 2);
        if (vaddress == vbase) {
            if (ArmSystem::haveEL(tc, EL3) && !inSecureState(tc)) {
                uint32_t bmask = 1UL << (low_addr + 24);
                match_word = match_word | (DBGVCR) bmask;
                // Non-secure vectors
            } else {
                uint32_t bmask = 1UL << (low_addr);
                match_word = match_word | (DBGVCR) bmask;
                // Secure vectors (or no EL3)
            }
        }
        uint32_t mvbase = getVectorBase(tc, true);
        if (ArmSystem::haveEL(tc, EL3) && ELIs32(tc, EL3) &&
            inSecureState(tc) && (vaddress == mvbase)) {
            uint32_t bmask = 1UL << (low_addr + 8);
            match_word = match_word | (DBGVCR) bmask;
            // Monitor vectors
        }

        DBGVCR mask;

        // Mask out bits not corresponding to vectors.
        if (!ArmSystem::haveEL(tc, EL3)) {
            mask = (DBGVCR) 0xDE;
        } else if (!ELIs32(tc, EL3)) {
            mask = (DBGVCR) 0xDE0000DE;
        } else {
            mask = (DBGVCR) 0xDE00DEDE;
        }
        DBGVCR dbgvcr = tc->readMiscReg(MISCREG_DBGVCR);
        match_word = match_word & dbgvcr & mask;
        enabled = match_word != 0x0;
        // Check for UNPREDICTABLE case - match on Prefetch Abort and
        // Data Abort vectors
        ExceptionLevel ELd = debugTargetFrom(tc, inSecureState(tc));
        if (((match_word & 0x18001818) != 0x0) && ELd == el) {
            enabled = false;
        }
    } else {
        enabled = false;
    }
    return enabled;
}

bool
VectorCatch::exceptionTrapping(ThreadContext *tc, ExceptionLevel el,
                               ArmFault* fault)
{
    if (conf->isAArch32() && ELIs32(tc, EL1) && el != EL2) {

        DBGVCR dbgvcr = tc->readMiscReg(MISCREG_DBGVCR);
        DBGVCR match_type = fault->vectorCatchFlag();
        DBGVCR mask;

        if (!ArmSystem::haveEL(tc, EL3)) {
            mask = (DBGVCR) 0xDE;
        } else if (ELIs32(tc, EL3) && fault->getToMode() == MODE_MON) {
            mask = (DBGVCR) 0x0000DE00;
        } else {
            if (inSecureState(tc))
                mask = (DBGVCR) 0x000000DE;
            else
                mask = (DBGVCR) 0xDE000000;
        }
        match_type = match_type & mask & dbgvcr;

        if (match_type != 0x0) {
            return true;
        }
    }
    return false;
}

