// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2020, Intel Corporation

// Based on Code by IntelPCM
// Modified from Intel by Chihun Song: 12-09-2022
// Updated by Gregory Jun: 12-10-2022

#define PCM_USE_PCI_MM_LINUX

#include "cpucounters.h"

#include <iomanip>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>
#include <unistd.h>

#include "address.h"

using namespace std;
using namespace pcm;

// BW related functions and variables
#define PCM_DELAY_DEFAULT 1.0 // in seconds
#define PCM_DELAY_MIN 0.015   // 15 milliseconds is practical on most modern CPUs

#define DEFAULT_DISPLAY_COLUMNS 2

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

// BW related functions and variables

int main(int argc, char *argv[]) {
    std::cout << "\n Processor Counter Monitor " << PCM_VERSION << "\n";
    std::cout << "\n PCICFG read/write utility\n\n";

    uint32 value = 0;
    uint32 ch_temp_reg = 0;
    uint32 ch_temp_val = 0;
    uint32 ch_tref_reg = 0;
    uint32 ch_tref_const = 0;
    uint32 ch_trefi_val = 0;
    uint32 ch_err_reg = 0;
    uint32 ch_err_r1_val = 0;
    uint32 ch_err_r0_val = 0;
    uint32 ch_r1_ovrflw = 0;
    uint32 ch_r0_ovrflw = 0;
    uint32 pre_err_r1_val = 0, pre_err_r0_val = 0;
    uint32 pre_a_err_r1_val = 0, pre_b_err_r1_val = 0, pre_c_err_r1_val = 0, pre_d_err_r1_val = 0;
    uint32 pre_a_err_r0_val = 0, pre_b_err_r0_val = 0, pre_c_err_r0_val = 0, pre_d_err_r0_val = 0;

    bool write = false;
    bool dec = false;
    bool err_det_r1 = false, err_det_r0 = false;
    bool pre_a_err_det_r1 = false, pre_a_err_det_r0 = false, pre_b_err_det_r1 = false, pre_b_err_det_r0 = false, pre_c_err_det_r1 = false,
         pre_c_err_det_r0 = false, pre_d_err_det_r1 = false, pre_d_err_det_r0 = false;

    int channel = 0; // Channel A=0, B=1, C=2, D=3

    int ch_a_group = CH_A_Group, ch_b_group = CH_B_Group, ch_c_group = CH_C_Group, ch_d_group = CH_D_Group;
    int ch_a_bus = CH_A_Bus, ch_b_bus = CH_B_Bus, ch_c_bus = CH_C_Bus, ch_d_bus = CH_D_Bus;
    int ch_a_device = CH_A_Device, ch_b_device = CH_B_Device, ch_c_device = CH_C_Device, ch_d_device = CH_D_Device;
    int ch_a_func = CH_A_Func, ch_b_func = CH_B_Func, ch_c_func = CH_C_Func, ch_d_func = CH_D_Func;
    int ch_a_err_func = CH_A_Err_Func, ch_b_err_func = CH_B_Err_Func, ch_c_err_func = CH_C_Err_Func, ch_d_err_func = CH_D_Err_Func;
    int temp_off = Temp_Off;
    int tREFI_off = tREFI_Off;
    int err_cnt_off = Err_cnt_Off;

    try {
        // PciHandleType h(group, bus, device, function);

        // Set IMC register address (IMC Thermal Control)
        PciHandleType ch_a_thermal(ch_a_group, ch_a_bus, ch_a_device, ch_a_func);
        PciHandleType ch_b_thermal(ch_b_group, ch_b_bus, ch_b_device, ch_b_func);
        PciHandleType ch_c_thermal(ch_c_group, ch_c_bus, ch_c_device, ch_c_func);
        PciHandleType ch_d_thermal(ch_d_group, ch_d_bus, ch_d_device, ch_d_func);
        // Set IMC register address (IMC Error Registers)
        PciHandleType ch_a_err(ch_a_group, ch_a_bus, ch_a_device, ch_a_err_func);
        PciHandleType ch_b_err(ch_b_group, ch_b_bus, ch_b_device, ch_b_err_func);
        PciHandleType ch_c_err(ch_c_group, ch_c_bus, ch_c_device, ch_c_err_func);
        PciHandleType ch_d_err(ch_d_group, ch_d_bus, ch_d_device, ch_d_err_func);
        // Register Variable
        PciHandleType ch_thermal = ch_a_thermal;
        PciHandleType ch_err = ch_a_err;

        if (!dec)
            std::cout << std::hex << std::showbase;
        ch_a_thermal.read32(tREFI_off, &ch_tref_reg);
        ch_tref_const = ch_tref_reg & 0xffff8000;
        ch_trefi_val = base_tREFI & 0x7fff;
        ch_a_thermal.write32(tREFI_off, ch_tref_const + ch_trefi_val);
        ch_b_thermal.write32(tREFI_off, ch_tref_const + ch_trefi_val);
        ch_c_thermal.write32(tREFI_off, ch_tref_const + ch_trefi_val);
        ch_d_thermal.write32(tREFI_off, ch_tref_const + ch_trefi_val);

        ///////////////////////////////////////////////////////////////////////////////////////////////// BW related Vars
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

        ServerUncoreCounterState *BeforeState = new ServerUncoreCounterState[m->getNumSockets()];
        ServerUncoreCounterState *AfterState = new ServerUncoreCounterState[m->getNumSockets()];
        uint64 BeforeTime = 0, AfterTime = 0;

        for (uint32 i = 0; i < m->getNumSockets(); ++i)
            BeforeState[i] = m->getServerUncoreCounterState(i);
        BeforeTime = m->getTickCount();

        float BW[4][2] = {0}; // channel - read/write in MB/s //hard coded for a four channel system
        float BW_average[2][2] = {0}; // pahse - read/write
        int count = 0;
        int phase = 0;
        ///////////////////////////////////////////////////////////////////////////////////////////////// BW related Vars

        while (1) {
            std::cout << " Channel variable : " << channel << "\n\n";

            // bw read
            AfterTime = m->getTickCount();
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                AfterState[i] = m->getServerUncoreCounterState(i);

            calculate_bandwidth(m, BeforeState, AfterState, AfterTime - BeforeTime, metrics, BW);
            swap(BeforeTime, AfterTime);
            swap(BeforeState, AfterState);
            for (int i = 0; i < 4; i++) {
                cout << BW[i][0] << " " << BW[i][1] << endl;
            }

            // bw average -> steady state after 10 cycles
            if (count == 10) {
                // change phase
                if (phase == 0){
                    phase = 1;
                    BW_average[1][0] = 0;
                    BW_average[1][1] = 0;
                } else {
                    phase = 0;
                    BW_average[0][0] = 0;
                    BW_average[0][1] = 0;
                }
            } else {
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 2; j++) {
                        BW_average[phase][j] += BW[i][j];
                    }
                }
                BW_average[phase][0] = BW_average[phase][0] / 4;
                BW_average[phase][1] = BW_average[phase][1] / 4;
            }

            if (channel == 0) { // Channel A
                ch_thermal = ch_a_thermal;
                ch_err = ch_a_err;
                pre_d_err_r1_val = ch_err_r1_val;
                pre_d_err_r0_val = ch_err_r0_val;
                pre_err_r1_val = pre_a_err_r1_val;
                pre_err_r0_val = pre_a_err_r0_val;
                pre_d_err_det_r1 = err_det_r1;
                pre_d_err_det_r0 = err_det_r0;
                err_det_r1 = pre_a_err_det_r1;
                err_det_r0 = pre_a_err_det_r0;
                std::cout << " Channel A Register Value."
                          << "\n\n";
            } else if (channel == 1) { // Channel B
                ch_thermal = ch_b_thermal;
                ch_err = ch_b_err;
                pre_a_err_r1_val = ch_err_r1_val;
                pre_a_err_r0_val = ch_err_r0_val;
                pre_err_r1_val = pre_b_err_r1_val;
                pre_err_r0_val = pre_b_err_r0_val;
                pre_a_err_det_r1 = err_det_r1;
                pre_a_err_det_r0 = err_det_r0;
                err_det_r1 = pre_b_err_det_r1;
                err_det_r0 = pre_b_err_det_r0;
                std::cout << " Channel B Register Value."
                          << "\n\n";
            } else if (channel == 2) { // Channel C
                ch_thermal = ch_c_thermal;
                ch_err = ch_c_err;
                pre_b_err_r1_val = ch_err_r1_val;
                pre_b_err_r0_val = ch_err_r0_val;
                pre_err_r1_val = pre_c_err_r1_val;
                pre_err_r0_val = pre_c_err_r0_val;
                pre_b_err_det_r1 = err_det_r1;
                pre_b_err_det_r0 = err_det_r0;
                err_det_r1 = pre_c_err_det_r1;
                err_det_r0 = pre_c_err_det_r0;
                std::cout << " Channel C Register Value."
                          << "\n\n";
            } else if (channel == 3) { // Channel D
                ch_thermal = ch_d_thermal;
                ch_err = ch_d_err;
                pre_c_err_r1_val = ch_err_r1_val;
                pre_c_err_r0_val = ch_err_r0_val;
                pre_err_r1_val = pre_d_err_r1_val;
                pre_err_r0_val = pre_d_err_r0_val;
                pre_c_err_det_r1 = err_det_r1;
                pre_c_err_det_r0 = err_det_r0;
                err_det_r1 = pre_d_err_det_r1;
                err_det_r0 = pre_d_err_det_r0;
                std::cout << " Channel D Register Value."
                          << "\n\n";
            } else {
                channel = 0;
            }

            ch_thermal.read32(temp_off, &ch_temp_reg);
            ch_temp_val = ch_temp_reg & 0xff;

            std::cout << std::dec << std::showbase;
            std::cout << " Channel temp. : " << ch_temp_val << "\n\n";

            ch_err.read32(err_cnt_off, &ch_err_reg);

            ch_r1_ovrflw = (ch_err_reg >> 31) & 0x1;
            ch_err_r1_val = (ch_err_reg >> 16) & 0x7fff;
            ch_r0_ovrflw = (ch_err_reg >> 15) & 0x1;
            ch_err_r0_val = ch_err_reg & 0x00007fff;

            std::cout << " Rank 1 overflow : " << ch_r1_ovrflw << " , Rank 0 overflow : " << ch_r0_ovrflw << "\n";
            std::cout << " Rank 1 err count : " << ch_err_r1_val << " , Rank 0 err count : " << ch_err_r1_val << "\n\n";

            ch_thermal.read32(tREFI_off, &ch_tref_reg);
            ch_trefi_val = ch_tref_reg & 0x7fff;

            std::cout << " 1866 => tck = 1.072ns"
                      << "\n";
            std::cout << " Previous Channel tREFI(ck) : " << ch_trefi_val << ", ";

            if (((ch_r1_ovrflw + ch_r0_ovrflw) == 0) & (pre_err_r1_val >= ch_err_r1_val) & (pre_err_r0_val >= ch_err_r0_val)) { // if no error
                if ((err_det_r1 == false) & (err_det_r0 == false)) {                                                            // if no error, increase trefI
                    if (ch_trefi_val < 0x71C0) {                                                                                //  tREFI max
                        ch_trefi_val = ch_trefi_val + step_tREFI_inc;
                    } else
                        ch_trefi_val = ch_trefi_val;
                } else if ((pre_err_r1_val > ch_err_r1_val) & err_det_r1) {
                    err_det_r1 = false;
                    ch_trefi_val = ch_trefi_val - step_tREFI_dec << 1;
                } else if ((pre_err_r0_val > ch_err_r0_val) & err_det_r0) {
                    err_det_r0 = false;
                    ch_trefi_val = ch_trefi_val - step_tREFI_dec << 1;
                } else
                    ch_trefi_val = ch_trefi_val - step_tREFI_dec << 1;

                ch_thermal.write32(tREFI_off, ch_tref_const + ch_trefi_val);
            } else { // if error
                if (ch_r1_ovrflw || (pre_err_r1_val < ch_err_r1_val))
                    err_det_r1 = true;
                if (ch_r0_ovrflw || (pre_err_r0_val < ch_err_r0_val))
                    err_det_r0 = true;
                ch_trefi_val = ch_trefi_val - step_tREFI_dec << 1;
                ch_thermal.write32(tREFI_off, ch_tref_const + ch_trefi_val);
            }

            std::cout << " Present Channel tREFI(ck) : " << ch_trefi_val << "\n\n ";

            if (channel < 3)
                channel++;
            else
                channel = 0;

            usleep(100000);
        }
    } catch (std::exception &e) {
        std::cerr << "Error accessing registers: " << e.what() << "\n";
        std::cerr << "Please check if the program can access MSR/PCICFG drivers.\n";
    }
    return 0;
}
