/*
 * Copyright 2020 Google Inc.
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

#ifndef __ARCH_MIPS_SE_WORKLOAD_HH__
#define __ARCH_MIPS_SE_WORKLOAD_HH__

#include "arch/mips/registers.hh"
#include "params/MipsSEWorkload.hh"
#include "sim/se_workload.hh"
#include "sim/syscall_abi.hh"
#include "sim/syscall_desc.hh"

namespace MipsISA
{

class SEWorkload : public ::SEWorkload
{
  public:
    PARAMS(MipsSEWorkload);

    SEWorkload(const Params &p) : ::SEWorkload(p) {}

    ::Loader::Arch getArch() const override { return ::Loader::Mips; }

    struct SyscallABI : public GenericSyscallABI64
    {
        static const std::vector<int> ArgumentRegs;
    };
};

} // namespace MipsISA

namespace GuestABI
{

template <>
struct Result<MipsISA::SEWorkload::SyscallABI, SyscallReturn>
{
    static void
    store(ThreadContext *tc, const SyscallReturn &ret)
    {
        if (ret.suppressed() || ret.needsRetry())
            return;

        if (ret.successful()) {
            // no error
            tc->setIntReg(MipsISA::SyscallSuccessReg, 0);
            tc->setIntReg(MipsISA::ReturnValueReg, ret.returnValue());
        } else {
            // got an error, return details
            tc->setIntReg(MipsISA::SyscallSuccessReg, (uint32_t)(-1));
            tc->setIntReg(MipsISA::ReturnValueReg, ret.errnoValue());
        }
        if (ret.count() > 1)
            tc->setIntReg(MipsISA::SyscallPseudoReturnReg, ret.value2());
    }
};

} // namespace GuestABI

#endif // __ARCH_MIPS_SE_WORKLOAD_HH__
