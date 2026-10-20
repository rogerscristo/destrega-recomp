/* MDEC (motion decoder) register interface, runtime/mdec.c */
#ifndef MDEC_H
#define MDEC_H
#include "psx.h"

void mdec_init(void);
void mdec_write_cmd(u32 v);      /* 0x1f801820 write / DMA0 input words */
void mdec_write_ctrl(u32 v);     /* 0x1f801824 write */
u32 mdec_read_data(void);        /* 0x1f801820 read / DMA1 output words */
u32 mdec_read_status(void);      /* 0x1f801824 read */

#endif
