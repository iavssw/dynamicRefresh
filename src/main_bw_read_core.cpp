// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2020, Intel Corporation
// written by Patrick Lu
// increased max sockets to 256 - Thomas Willhalm

// Based on Code by IntelPCM
// Modified from Intel by Gregory Jun: 12-10-2022

#include "cpucounters.h"
#include "utils.h"
#include <assert.h>
#include <iomanip>
#include <iostream>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/time.h> // for gettimeofday()
#include <unistd.h>

#define PCM_DELAY_DEFAULT 1.0 // in seconds
#define PCM_DELAY_MIN 0.015   // 15 milliseconds is practical on most modern CPUs

#define DEFAULT_DISPLAY_COLUMNS 2

using namespace std;
using namespace pcm;

constexpr uint32 max_sockets = 256;
uint32 max_imc_channels = ServerUncoreCounterState::maxChannels;
const uint32 max_edc_channels = ServerUncoreCounterState::maxChannels;
const uint32 max_imc_controllers = ServerUncoreCounterState::maxControllers;

bool skipInactiveChannels = true;
bool enforceFlush = false;

typedef struct memdata {
    float iMC_Rd_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels]{};
    float iMC_Wr_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels]{};
    float iMC_PMM_Rd_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels]{};
    float iMC_PMM_Wr_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels]{};
    float iMC_PMM_MemoryMode_Miss_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels]{};
    float iMC_Rd_socket[max_sockets]{};
    float iMC_Wr_socket[max_sockets]{};
    float iMC_PMM_Rd_socket[max_sockets]{};
    float iMC_PMM_Wr_socket[max_sockets]{};
    float iMC_PMM_MemoryMode_Miss_socket[max_sockets]{};
    bool iMC_NM_hit_rate_supported{};
    float iMC_PMM_MemoryMode_Hit_socket[max_sockets]{};
    bool M2M_NM_read_hit_rate_supported{};
    float iMC_NM_hit_rate[max_sockets]{};
    float M2M_NM_read_hit_rate[max_sockets][max_imc_controllers]{};
    float EDC_Rd_socket_chan[max_sockets][max_edc_channels]{};
    float EDC_Wr_socket_chan[max_sockets][max_edc_channels]{};
    float EDC_Rd_socket[max_sockets]{};
    float EDC_Wr_socket[max_sockets]{};
    uint64 partial_write[max_sockets]{};
    ServerUncoreMemoryMetrics metrics{};
} memdata_t;

bool anyPmem(const ServerUncoreMemoryMetrics &metrics) { return (metrics == Pmem) || (metrics == PmemMixedMode) || (metrics == PmemMemoryMode); }

void calculate_bandwidth(PCM *m, const ServerUncoreCounterState uncState1[], const ServerUncoreCounterState uncState2[], const uint64 elapsedTime,
                         const ServerUncoreMemoryMetrics &metrics, float BW[4][2]) {
    memdata_t md;
    md.metrics = metrics;
    md.M2M_NM_read_hit_rate_supported = (m->getCPUModel() == PCM::SKX);
    md.iMC_NM_hit_rate_supported = (m->getCPUModel() == PCM::ICX);

    for (uint32 skt = 0; skt < max_sockets; ++skt) {
        md.iMC_Rd_socket[skt] = 0.0;
        md.iMC_Wr_socket[skt] = 0.0;
        md.iMC_PMM_Rd_socket[skt] = 0.0;
        md.iMC_PMM_Wr_socket[skt] = 0.0;
        md.iMC_PMM_MemoryMode_Miss_socket[skt] = 0.0;
        md.iMC_PMM_MemoryMode_Hit_socket[skt] = 0.0;
        md.iMC_NM_hit_rate[skt] = 0.0;
        md.EDC_Rd_socket[skt] = 0.0;
        md.EDC_Wr_socket[skt] = 0.0;
        md.partial_write[skt] = 0;
        for (uint32 i = 0; i < max_imc_controllers; ++i) {
            md.M2M_NM_read_hit_rate[skt][i] = 0.;
        }
    }

    for (uint32 skt = 0; skt < m->getNumSockets(); ++skt) {
        const uint32 numChannels1 = (uint32)m->getMCChannels(skt, 0); // number of channels in the first controller

        auto toBW = [&elapsedTime](const uint64 nEvents) { return (float)(nEvents * 64 / 1000000.0 / (elapsedTime / 1000.0)); };

        if (m->MCDRAMmemoryTrafficMetricsAvailable()) {
            for (uint32 channel = 0; channel < max_edc_channels; ++channel) {
                if (skipInactiveChannels && getEDCCounter(channel, ServerPCICFGUncore::EventPosition::READ, uncState1[skt], uncState2[skt]) == 0.0 &&
                    getEDCCounter(channel, ServerPCICFGUncore::EventPosition::WRITE, uncState1[skt], uncState2[skt]) == 0.0) {
                    md.EDC_Rd_socket_chan[skt][channel] = -1.0;
                    md.EDC_Wr_socket_chan[skt][channel] = -1.0;
                    continue;
                }

                md.EDC_Rd_socket_chan[skt][channel] = toBW(getEDCCounter(channel, ServerPCICFGUncore::EventPosition::READ, uncState1[skt], uncState2[skt]));
                md.EDC_Wr_socket_chan[skt][channel] = toBW(getEDCCounter(channel, ServerPCICFGUncore::EventPosition::WRITE, uncState1[skt], uncState2[skt]));

                md.EDC_Rd_socket[skt] += md.EDC_Rd_socket_chan[skt][channel];
                md.EDC_Wr_socket[skt] += md.EDC_Wr_socket_chan[skt][channel];
            }
        }

        for (uint32 channel = 0; channel < max_imc_channels; ++channel) {
            uint64 reads = 0, writes = 0, pmmReads = 0, pmmWrites = 0, pmmMemoryModeCleanMisses = 0, pmmMemoryModeDirtyMisses = 0;
            uint64 pmmMemoryModeHits = 0;
            reads = getMCCounter(channel, ServerPCICFGUncore::EventPosition::READ, uncState1[skt], uncState2[skt]);
            writes = getMCCounter(channel, ServerPCICFGUncore::EventPosition::WRITE, uncState1[skt], uncState2[skt]);
            if (metrics == Pmem) {
                pmmReads = getMCCounter(channel, ServerPCICFGUncore::EventPosition::PMM_READ, uncState1[skt], uncState2[skt]);
                pmmWrites = getMCCounter(channel, ServerPCICFGUncore::EventPosition::PMM_WRITE, uncState1[skt], uncState2[skt]);
            } else if (metrics == PmemMixedMode || metrics == PmemMemoryMode) {
                pmmMemoryModeCleanMisses = getMCCounter(channel, ServerPCICFGUncore::EventPosition::PMM_MM_MISS_CLEAN, uncState1[skt], uncState2[skt]);
                pmmMemoryModeDirtyMisses = getMCCounter(channel, ServerPCICFGUncore::EventPosition::PMM_MM_MISS_DIRTY, uncState1[skt], uncState2[skt]);
            }
            if (metrics == PmemMemoryMode) {
                pmmMemoryModeHits = getMCCounter(channel, ServerPCICFGUncore::EventPosition::NM_HIT, uncState1[skt], uncState2[skt]);
            }
            if (skipInactiveChannels && (reads + writes == 0)) {
                if ((metrics != Pmem) || (pmmReads + pmmWrites == 0)) {
                    if ((metrics != PmemMixedMode) || (pmmMemoryModeCleanMisses + pmmMemoryModeDirtyMisses == 0)) {

                        md.iMC_Rd_socket_chan[skt][channel] = -1.0;
                        md.iMC_Wr_socket_chan[skt][channel] = -1.0;
                        continue;
                    }
                }
            }

            if (metrics != PmemMemoryMode) {
                md.iMC_Rd_socket_chan[skt][channel] = toBW(reads);
                md.iMC_Wr_socket_chan[skt][channel] = toBW(writes);

                md.iMC_Rd_socket[skt] += md.iMC_Rd_socket_chan[skt][channel];
                md.iMC_Wr_socket[skt] += md.iMC_Wr_socket_chan[skt][channel];
            }

            if (metrics == Pmem) {
                md.iMC_PMM_Rd_socket_chan[skt][channel] = toBW(pmmReads);
                md.iMC_PMM_Wr_socket_chan[skt][channel] = toBW(pmmWrites);

                md.iMC_PMM_Rd_socket[skt] += md.iMC_PMM_Rd_socket_chan[skt][channel];
                md.iMC_PMM_Wr_socket[skt] += md.iMC_PMM_Wr_socket_chan[skt][channel];

                md.M2M_NM_read_hit_rate[skt][(channel < numChannels1) ? 0 : 1] += (float)reads;
            } else if (metrics == PmemMixedMode) {
                md.iMC_PMM_MemoryMode_Miss_socket_chan[skt][channel] = toBW(pmmMemoryModeCleanMisses + 2 * pmmMemoryModeDirtyMisses);
                md.iMC_PMM_MemoryMode_Miss_socket[skt] += md.iMC_PMM_MemoryMode_Miss_socket_chan[skt][channel];
            } else if (metrics == PmemMemoryMode) {
                md.iMC_PMM_MemoryMode_Miss_socket[skt] += (float)((pmmMemoryModeCleanMisses + pmmMemoryModeDirtyMisses) / (elapsedTime / 1000.0));
                md.iMC_PMM_MemoryMode_Hit_socket[skt] += (float)((pmmMemoryModeHits) / (elapsedTime / 1000.0));
            } else {
                md.partial_write[skt] +=
                    (uint64)(getMCCounter(channel, ServerPCICFGUncore::EventPosition::PARTIAL, uncState1[skt], uncState2[skt]) / (elapsedTime / 1000.0));
            }
        }
    }

    // socket 0 channel 0 -> change to for loop
    BW[0][0] = md.iMC_Rd_socket_chan[0][0]; // socket - channel
    BW[0][1] = md.iMC_Wr_socket_chan[0][0];

    // socket 0 channel 1
    BW[1][0] = md.iMC_Rd_socket_chan[0][1]; // socket - channel
    BW[1][1] = md.iMC_Wr_socket_chan[0][1];

    // socket 0 channel 2
    BW[2][0] = md.iMC_Rd_socket_chan[0][2]; // socket - channel
    BW[2][1] = md.iMC_Wr_socket_chan[0][2];

    // socket 0 channel 3
    BW[3][0] = md.iMC_Rd_socket_chan[0][3]; // socket - channel
    BW[3][1] = md.iMC_Wr_socket_chan[0][3];
}

int main(int argc, char *argv[]) {

    double delay = PCM_DELAY_DEFAULT;
    bool csv = false, csvheader = false, show_channel_output = true, print_update = false;
    uint32 no_columns = DEFAULT_DISPLAY_COLUMNS; // Default number of columns is 2
    char *sysCmd = NULL;
    int rankA = -1, rankB = -1;
    MainLoop mainLoop;

    PCM *m = PCM::getInstance();
    ServerUncoreMemoryMetrics metrics;
    metrics = m->PMMTrafficMetricsAvailable() ? Pmem : PartialWrites;
    max_imc_channels = (pcm::uint32)m->getMCChannelsPerSocket();

    m->disableJKTWorkaround();
    m->setBlocked(false);
    // print_cpu_details();
    // PCM::ErrorCode status = m->programServerUncoreMemoryMetrics(metrics, rankA, rankB);
    // m->checkError(status);

    ServerUncoreCounterState *BeforeState = new ServerUncoreCounterState[m->getNumSockets()];
    ServerUncoreCounterState *AfterState = new ServerUncoreCounterState[m->getNumSockets()];
    uint64 BeforeTime = 0, AfterTime = 0;

    cerr << "Update every " << delay << " seconds\n";

    for (uint32 i = 0; i < m->getNumSockets(); ++i)
        BeforeState[i] = m->getServerUncoreCounterState(i);
    BeforeTime = m->getTickCount();

    mainLoop([&]() {
        cout << "Updated BW" << endl;
        calibratedSleep(delay, sysCmd, mainLoop, m);

        AfterTime = m->getTickCount();
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
            AfterState[i] = m->getServerUncoreCounterState(i);

        // calculate_bandwidth(m, BeforeState, AfterState, AfterTime - BeforeTime, csv, csvheader, no_columns, metrics, show_channel_output, print_update);

        float BW[4][2] = {0}; // channel - read/write in MB/s //hard coded for a four channel system
        calculate_bandwidth(m, BeforeState, AfterState, AfterTime - BeforeTime, metrics, BW);
        for (int i = 0; i < 4; i++) {
            cout << BW[i][0] << " " << BW[i][1] << endl;
        }

        swap(BeforeTime, AfterTime);
        swap(BeforeState, AfterState);

        if (m->isBlocked()) {
            // in case PCM was blocked after spawning child application: break monitoring loop here
            return false;
        }
        return true;
    });

    delete[] BeforeState;
    delete[] AfterState;

    exit(EXIT_SUCCESS);
}
