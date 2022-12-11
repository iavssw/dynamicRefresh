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

using namespace pcm;

int main(int argc, char *argv[]) {
    std::cout << "\n Processor Counter Monitor " << PCM_VERSION << "\n";
    std::cout << "\n PCICFG read/write utility\n\n";

#ifdef __linux__
#ifndef PCM_USE_PCI_MM_LINUX
    std::cout << "\n To access *extended* configuration space recompile with "
                 "-DPCM_USE_PCI_MM_LINUX option.\n";
#endif
#endif

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

        while (1) {
            // std::cout << " Channel variable : " << channel << "\n\n";

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
