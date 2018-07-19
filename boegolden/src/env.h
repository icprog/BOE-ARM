/*
 * env.h
 *
 *  Created on: Jun 4, 2018
 *      Author: luxq
 */

#ifndef SRC_ENV_H_
#define SRC_ENV_H_

#include <stdio.h>
#include "xil_types.h"
#pragma pack(push)
#pragma pack(1)

typedef struct EnvContent {
    u8  start_magic; 	// 0xA8
    u8  update_flag; 	// update flag.
    u8  reversed_1[2];

    u64 seq_id;			// env id, inc one per time.


    u32 bootaddr;		// for multiboot.
    u32 boeid;			// BOEID for the board.
    u8  bindAccount[32];// bind account address for the board.
    u8  reversed_3[32]; // old bind id.

    u8  reversed_2[1956];

    u8  reversed_0[3];
    u8  end_magic; 		// 0xC5;

    u32 envChecksum;  	// checksum above data.
}EnvContent;


typedef struct EnvHandle {
    u32 start_addr;
    u32 partation_size;
    u32 end_addr;
    u32 current_addr;
    u32 flash_sector_size;	// erase unit.
    u32 flash_sector_mask;
    u32 bInit;
    XQspiPsu flashInstance;
    FPartation fp;
    EnvContent genv;
}EnvHandle;

#pragma pack(pop)

int env_init(void);
int env_get(EnvContent *env);
int env_update(EnvContent *env);
EnvHandle* env_get_handle();

#endif /* SRC_ENV_H_ */
