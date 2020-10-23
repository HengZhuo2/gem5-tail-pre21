/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
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

/*
   This file has been modified by Kevin Moore and Dan Nussbaum of the
   Scalable Systems Research Group at Sun Microsystems Laboratories
   (http://research.sun.com/scalable/) to support the Adaptive
   Transactional Memory Test Platform (ATMTP).

   Please send email to atmtp-interest@sun.com with feedback, questions, or
   to request future announcements about ATMTP.

   ----------------------------------------------------------------------

   File modification date: 2008-02-23

   ----------------------------------------------------------------------
*/

#ifndef __MEM_RUBY_PROFILER_PROFILER_HH__
#define __MEM_RUBY_PROFILER_PROFILER_HH__

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/callback.hh"
#include "base/statistics.hh"
#include "mem/ruby/common/MachineID.hh"
#include "mem/ruby/protocol/AccessType.hh"
#include "mem/ruby/protocol/PrefetchBit.hh"
#include "mem/ruby/protocol/RubyAccessMode.hh"
#include "mem/ruby/protocol/RubyRequestType.hh"
#include "params/RubySystem.hh"

class RubyRequest;
class AddressProfiler;

class Profiler
{
  public:
    Profiler(const RubySystemParams &params, RubySystem *rs);
    ~Profiler();

    RubySystem *m_ruby_system;

    void wakeup();
    void regStats();
    void collateStats();

    AddressProfiler* getAddressProfiler() { return m_address_profiler_ptr; }
    AddressProfiler* getInstructionProfiler() { return m_inst_profiler_ptr; }

    void addAddressTraceSample(const RubyRequest& msg, NodeID id);

    // added by SS
    bool getHotLines() const { return m_hot_lines; }
    bool getAllInstructions() const { return m_all_instructions; }

  private:
    // Private copy constructor and assignment operator
    Profiler(const Profiler& obj);
    Profiler& operator=(const Profiler& obj);

    AddressProfiler* m_address_profiler_ptr;
    AddressProfiler* m_inst_profiler_ptr;

    struct ProfilerStats : public Stats::Group
    {
        ProfilerStats(Stats::Group *parent, Profiler *profiler);

        struct PerRequestTypeStats : public Stats::Group
        {
            PerRequestTypeStats(Stats::Group *parent);

            // Histogram of the latency of each request type
            std::vector<Stats::Histogram *> m_typeLatencyHistSeqr;
            std::vector<Stats::Histogram *> m_typeLatencyHistCoalsr;

            // Histogram of the latency of requests that hit in the controller
            // connected to this sequencer for each type of request
            std::vector<Stats::Histogram *> m_hitTypeLatencyHistSeqr;

            // Histogram of the latency of requests that miss in the controller
            // connected to this sequencer for each type of request
            std::vector<Stats::Histogram *> m_missTypeLatencyHistSeqr;
            std::vector<Stats::Histogram *> m_missTypeLatencyHistCoalsr;
        } perRequestTypeStats;

        struct PerMachineTypeStats : public Stats::Group
        {
            PerMachineTypeStats(Stats::Group *parent);

            //! Histograms for profiling the latencies for requests that
            //! did not required external messages.
            std::vector<Stats::Histogram *> m_hitMachLatencyHistSeqr;

            //! Histograms for profiling the latencies for requests that
            //! required external messages.
            std::vector<Stats::Histogram *> m_missMachLatencyHistSeqr;
            std::vector<Stats::Histogram *> m_missMachLatencyHistCoalsr;

            //! Histograms for recording the breakdown of miss latency
            std::vector<Stats::Histogram *> m_IssueToInitialDelayHistSeqr;
            std::vector<Stats::Histogram *> m_InitialToForwardDelayHistSeqr;
            std::vector<Stats::Histogram *>
              m_ForwardToFirstResponseDelayHistSeqr;
            std::vector<Stats::Histogram *>
              m_FirstResponseToCompletionDelayHistSeqr;
            std::vector<Stats::Scalar *> m_IncompleteTimesSeqr;
            std::vector<Stats::Histogram *> m_IssueToInitialDelayHistCoalsr;
            std::vector<Stats::Histogram *> m_InitialToForwardDelayHistCoalsr;
            std::vector<Stats::Histogram *>
              m_ForwardToFirstResponseDelayHistCoalsr;
            std::vector<Stats::Histogram *>
              m_FirstResponseToCompletionDelayHistCoalsr;
        } perMachineTypeStats;

        struct PerRequestTypeMachineTypeStats : public Stats::Group
        {
            PerRequestTypeMachineTypeStats(Stats::Group *parent);

            //! Histograms for profiling the latencies for requests that
            //! did not required external messages.
            std::vector< std::vector<Stats::Histogram *> >
              m_hitTypeMachLatencyHistSeqr;

            //! Histograms for profiling the latencies for requests that
            //! required external messages.
            std::vector< std::vector<Stats::Histogram *> >
              m_missTypeMachLatencyHistSeqr;
            std::vector< std::vector<Stats::Histogram *> >
              m_missTypeMachLatencyHistCoalsr;
        } perRequestTypeMachineTypeStats;

        Stats::Histogram delayHistogram;
        std::vector<Stats::Histogram *> delayVCHistogram;

        //! Histogram for number of outstanding requests per cycle.
        Stats::Histogram m_outstandReqHistSeqr;
        Stats::Histogram m_outstandReqHistCoalsr;

        //! Histogram for holding latency profile of all requests.
        Stats::Histogram m_latencyHistSeqr;
        Stats::Histogram m_latencyHistCoalsr;

        //! Histogram for holding latency profile of all requests that
        //! hit in the controller connected to this sequencer.
        Stats::Histogram m_hitLatencyHistSeqr;

        //! Histogram for holding latency profile of all requests that
        //! miss in the controller connected to this sequencer.
        Stats::Histogram m_missLatencyHistSeqr;
        Stats::Histogram m_missLatencyHistCoalsr;
    };

    //added by SS
    const bool m_hot_lines;
    const bool m_all_instructions;
    const uint32_t m_num_vnets;

    ProfilerStats rubyProfilerStats;
};

#endif // __MEM_RUBY_PROFILER_PROFILER_HH__
