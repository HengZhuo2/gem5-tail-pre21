/*
 * Copyright (c) 2006-2007 The Regents of The University of Michigan
 * All rights reserved.
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

#ifndef __ARCH_SPARC_INSTS_MEM_HH__
#define __ARCH_SPARC_INSTS_MEM_HH__

#include "arch/sparc/insts/static_inst.hh"

namespace SparcISA
{

////////////////////////////////////////////////////////////////////
//
// Mem utility templates and functions
//

/**
 * Base class for memory operations.
 */
class Mem : public SparcStaticInst
{
  protected:
    using SparcStaticInst::SparcStaticInst;

    std::string generateDisassembly(
        Addr pc, const Loader::SymbolTable *symtab) const override;
};

/**
 * Class for memory operations which use an immediate offset.
 */
class MemImm : public Mem
{
  protected:

    // Constructor
    MemImm(const char *mnem, ExtMachInst _machInst, OpClass __opClass) :
        Mem(mnem, _machInst, __opClass), imm(szext<13>(_machInst))
    {}

    std::string generateDisassembly(
        Addr pc, const Loader::SymbolTable *symtab) const override;

    const int32_t imm;
};

}

#endif // __ARCH_SPARC_INSTS_MEM_HH__
