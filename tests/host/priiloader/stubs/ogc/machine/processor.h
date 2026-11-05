/* stand-in for libogc's ogc/machine/processor.h: the interrupt calls are no-ops on the host */
#ifndef STUB_OGC_PROCESSOR_H
#define STUB_OGC_PROCESSOR_H
#define _CPU_ISR_Disable(level) do { (level) = 0; } while (0)
#define _CPU_ISR_Restore(level) do { (void)(level); } while (0)
#endif
