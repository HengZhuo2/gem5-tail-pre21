/*
 * Copyright (c) 2017 ARM Limited
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
 * Copyright (c) 2008 The Hewlett-Packard Development Company
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

#ifndef __SIM_INIT_HH__
#define __SIM_INIT_HH__

#include "pybind11/pybind11.h"

#include <list>
#include <map>
#include <string>

#include <inttypes.h>

namespace gem5
{

/*
 * Data structure describing an embedded python file.
 */
struct EmbeddedPython
{
    const char *abspath;
    const char *modpath;
    const uint8_t *code;
    int zlen;
    int len;

    EmbeddedPython(const char *abspath, const char *modpath,
            const uint8_t *code, int zlen, int len);

    pybind11::object getCode() const;
    bool addModule() const;

    static std::list<EmbeddedPython *> &getList();
    static int initAll();
};

class EmbeddedPyBind
{
  public:
    EmbeddedPyBind(const char *_name,
                   void (*init_func)(pybind11::module_ &),
                   const char *_base);

    EmbeddedPyBind(const char *_name,
                   void (*init_func)(pybind11::module_ &));

    static void initAll(pybind11::module_ &_m5);

  private:
    void (*initFunc)(pybind11::module_ &);

    bool depsReady() const;
    void init(pybind11::module_ &m);

    bool registered;
    const std::string name;
    const std::string base;

    static std::map<std::string, EmbeddedPyBind *> &getMap();
};

int m5Main(int argc, char **argv);

} // namespace gem5

#endif // __SIM_INIT_HH__
