// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, 2018-2022 Intel Corporation

// written by Roman Dementiev
#include "cpucounters.h"


#ifdef _MSC_VER
#include <windows.h>
#include "windows/windriver.h"
#else
#include <unistd.h>
#endif
#include <iostream>
#include <stdlib.h>
#include <iomanip>
#include <string.h>
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif

using namespace pcm;

void print_usage(const char * progname)
{
    std::cout << "Usage " << progname << " [-w value] [-d] group bus device function offset\n\n";
    std::cout << "  Reads/writes 32-bit PCICFG register \n";
    std::cout << "   -w value  : write the value before reading \n";
    std::cout << "   -d        : output all numbers in dec (default is hex)\n";
    std::cout << "   --version : print application version\n";
    std::cout << "\n";
}

int main(int argc, char * argv[])
{
std::cout << " testing\n";
}
