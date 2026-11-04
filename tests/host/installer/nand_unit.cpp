/* builds src/Installer/source/nand.cpp unchanged for the PC (see tests/test_host_installer.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "sim_nand.h"
#define printf hc_quiet_printf
#include HC_NAND_CPP
