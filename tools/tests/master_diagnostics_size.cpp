// Compile only: symbol sizes report the real ESP32 ABI, not the host ABI.
#include "MasterScheduler.h"
unsigned char ofe_diag_entry_size[sizeof(MasterScheduler::BusModuleDiag)];
unsigned char ofe_diag_table_size[96 * sizeof(MasterScheduler::BusModuleDiag)];
