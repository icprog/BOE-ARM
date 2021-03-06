/******************************************************************************
 *
 * Copyright (C) 2009 - 2014 Xilinx, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Use of the Software is limited solely to applications:
 * (a) running on a Xilinx device, or
 * (b) that interact with a Xilinx device through a bus or interconnect.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 *
 ******************************************************************************/

/*
 * main.c: program entry.
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
 *
 * ------------------------------------------------
 * | UART TYPE   BAUD RATE                        |
 * ------------------------------------------------
 *   uartns550   9600
 *   uartlite    Configurable only in HW design
 *   ps7_uart    115200 (configured by bootrom/bsp)
 */

#include <stdio.h>
#include <stdlib.h>
#include "platform.h"
#include "xplatform_info.h"
#include "xil_printf.h"
#include "xil_io.h"
#include "xparameters.h"
#include "doCommand.h"
#include "common.h"
#include "flash_oper.h"
#include "version.h"
#include "atimer.h"
#include "at508.h"
#include <time.h>

#include "emac_oper.h"
#include "led.h"
#include "watchdog.h"



/*
 * Firmware main work
 * 1. upgrade
 * 2. set boeid
 * 3. set bindid
 * 4. set bindaccount
 * 5. generate random
 * 6. hwsign.
 *
 *
 * N. some function not support when not bindid or bindaccount or checkbind error.
*/

/*
 * Tmp variable.
 */

extern GlobalHandle gHandle;

/*
 * return value: 0: correct data.
 *               1: magic error.
 *               2: checksum error.
 */
static int package_check(A_Package *pack)
{
    static u64 err_num;
    int ret = 0;

    if(pack->header.magic_aacc == 0xaacc && pack->header.magic_ccaa == 0xccaa){
        // compare checksum.
        u32 cks = checksum(pack->data, pack->header.body_length);
        if(cks != pack->checksum){
            ret = 2;
            err_num++;
        }
    }else {
        ret = 1;
        err_num++;
        xil_printf("header.magic_aacc = 0x%x, header.magic_ccaa = 0x%x.\r\n", pack->header.magic_aacc, pack->header.magic_ccaa);
    }

    return ret;
}

int mainloop(void)
{
	A_Package *send = axu_package_new(PACKAGE_MIN_SIZE);
    char *errmsg = NULL;
    char *msgbuf = (char*)malloc(PACKAGE_MIN_SIZE);
    int ret = 0;
    xil_printf("Enter loop \r\n");

    while(gHandle.bRun){
        A_Package *rcv = NULL;
        u32 timeout_ms = 0;
        memset(msgbuf, 0, PACKAGE_MIN_SIZE);

        if(0 != msg_pool_fetch(gHandle.gMsgPoolIns, &rcv, timeout_ms)) {
            // have no msg.
        	usleep(1000);
        }else {
            ret = package_check(rcv);
            if(0 == ret)
            {
                ACmd cmd = rcv->header.acmd;
                //xil_printf("main: get cmd = %d.\r\n", cmd);
                Processor *pcs = processor_get(cmd);
                if(pcs == NULL){
                	errmsg = axu_get_error_msg(A_UNKNOWN_CMD);
                	strncat(msgbuf, errmsg, PACKAGE_MIN_SIZE);
                	snprintf(msgbuf+strlen(errmsg), PACKAGE_MIN_SIZE - strlen(errmsg), ": command is %d.", cmd);
                	make_response_error(rcv, ACMD_BP_RES_ERR, A_UNKNOWN_CMD, msgbuf, strlen(msgbuf), send);
                }else{
                	if(pcs->pre_check == NULL){
                		ret = pcs->do_func(rcv, send);
                	}
                	else {
                		ret = pcs->pre_check(rcv, send);
                		if(ret == PRET_OK){
                			ret = pcs->do_func(rcv, send);
                		}
					}
                	if(ret == PRET_NORES) // don't send response.
                		continue;
                }
            }else if(2 == ret){
            	xil_printf("package checksum error.\r\n");
                // checksum error.
            	errmsg = axu_get_error_msg(A_CHECKSUM_ERROR);
            	strncat(msgbuf, errmsg, PACKAGE_MIN_SIZE);
            	snprintf(msgbuf+strlen(errmsg), PACKAGE_MIN_SIZE - strlen(errmsg), ": package body checksum not macth.");
                make_response_error(rcv, ACMD_BP_RES_ERR, A_CHECKSUM_ERROR, msgbuf, strlen(msgbuf), send);
            }else if(1 == ret){
            	xil_printf("package magic error.\r\n");
                // magic error.
            	errmsg = axu_get_error_msg(A_MAGIC_ERROR);
            	strncat(msgbuf, errmsg, PACKAGE_MIN_SIZE);
            	snprintf(msgbuf+strlen(errmsg), PACKAGE_MIN_SIZE - strlen(errmsg), ": correct magic is 0x%x and 0x%x.", AXU_MAGIC_START, AXU_MAGIC_END);
                make_response_error(rcv, ACMD_BP_RES_ERR, A_MAGIC_ERROR, msgbuf, strlen(msgbuf), send);
            }

            msg_pool_txsend(gHandle.gMsgPoolIns, send, PACKAGE_MIN_SIZE);
        }
    }
    xil_printf("Exit loop \r\n");
    free(msgbuf);
    return 0;
}

int led_failed()
{
	while(1){
		ledHigh(LED_ALL);
		usleep(200000);
		ledLow(LED_ALL);
		usleep(200000);
	}
}
int led_running(void *data)
{
	static int timecnt = 0;
	timecnt ++;
	if(timecnt == 1){
		ledHigh(LED_1);
	}else{
		ledLow(LED_ALL);
	}
	timecnt = timecnt%3;
	return 1;
}

int heart(void *data)
{
	static int heart_time = 0;


	return 1;
}


int main()
{
    int status = 0;
    init_platform();
#if 0 // test flash
    xil_printf("hello  dffdjslfds\r\n");
    extern void runtest(void);
    runtest();
    //GoMultiBoot(0);
    return 0;
#endif

    // 1. some init.

    // init emac led status.
    //emac_init(); // remove by luxq at 2018-8-20.
    emac_reset();

    status = env_init();
    if(status != XST_SUCCESS){
        xil_printf("Env init failed.\n\r");
        goto failed;
    }
    status = atimer_init();
    if(status != XST_SUCCESS){
		xil_printf("timer init failed.\n\r");
		goto failed;
	}
    status = watch_dog_init();
    if(status != XST_SUCCESS){
		xil_printf("watch dog init failed.\n\r");
		//goto failed;
	}
    status = ledInit();
    status = at508_init();
    if(status != XST_SUCCESS){
		xil_printf("at508 init failed.\n\r");
		goto failed;
	}
    gHandle.gEnvHandle = env_get_handle();
    gHandle.gFlashInstancePtr = &gHandle.gEnvHandle->flashInstance;

    status = env_get(&gHandle.gEnv);
    if(status != XST_SUCCESS){
        xil_printf("env get failed.\n\r");
        goto failed;
    }

    status = msg_pool_init(&(gHandle.gMsgPoolIns));
    if(status != XST_SUCCESS){
        xil_printf("fifo init failed.\n\r");
        goto failed;
    }
    // 2. check upgrade flag.
    if(gHandle.gEnv.update_flag == UPGRADE_REBOOT){
        // Now is reboot from upgrade, so upgrade successful.
        // response upgrade success.
        gHandle.gEnv.update_flag = UPGRADE_NONE;
        env_update(&gHandle.gEnv);
    }


    xil_printf("Welcome to HPB, Version = %d.%d.%d.%d\r\n", vMajor(gVersion.H), gVersion.M, gVersion.F, gVersion.D);
    atimer_register_timer(led_running, 300, NULL);
    atimer_register_timer(heart, 30000, NULL);


    // 3. enter mainloop.
    mainloop();

failed:
	led_failed();

    cleanup_platform();
    return 0;
}
