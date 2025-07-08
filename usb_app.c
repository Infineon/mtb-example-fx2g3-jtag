/***************************************************************************//**
* \file usb_app.c
* \version 1.0
*
* \brief Implements the USB data handling part of the USB JTAG adapter application.
*
*******************************************************************************
* \copyright
* (c) (2025), Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.
*
* SPDX-License-Identifier: Apache-2.0
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "cy_pdl.h"
#include "cy_device.h"
#include "cy_usbhs_dw_wrapper.h"
#include "cy_usb_common.h"
#include "cy_hbdma.h"
#include "cy_hbdma_mgr.h"
#include "cy_usb_usbd.h"
#include "usb_app.h"
#include "cy_debug.h"
#include "usb_jtag.h"

/* Whether SET_CONFIG is complete or not. */
static volatile bool glIsDevConfigured = false;
static volatile bool cy_IsApplnActive = false;

/**
 * \name Cy_Jtag_AppStop
 * \brief Stop the data stream channels
 * \param pAppCtxt application layer context pointer
 * \param pUsbdCtxt USBD context
 * \retval None
 */
static void Cy_Jtag_AppStop(cy_stc_usb_app_ctxt_t *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt)
{
    cy_stc_app_endp_dma_set_t *pDmaSet;

    if(cy_IsApplnActive){
        pDmaSet = &(pAppCtxt->endpInDma[APP_RESPONSE_IN_EP]);

        /* Update the flag so that the application thread is notified of this. */
        cy_IsApplnActive = false;

        Cy_USB_USBD_EndpSetClearNakNrdy(pUsbdCtxt, APP_RESPONSE_IN_EP, CY_USB_ENDP_DIR_IN, true);
        Cy_SysLib_DelayUs(100);
        Cy_USBHS_App_ResetEpDma(pDmaSet);
        /* Flush the endpoint memory */
        Cy_USBD_FlushEndp(pUsbdCtxt, APP_RESPONSE_IN_EP, CY_USB_ENDP_DIR_IN);
        Cy_USBD_ResetEndp(pUsbdCtxt, APP_RESPONSE_IN_EP, CY_USB_ENDP_DIR_IN, true);
        Cy_USB_USBD_EndpSetClearNakNrdy(pUsbdCtxt, APP_RESPONSE_IN_EP, CY_USB_ENDP_DIR_IN, false);

        DBG_APP_INFO("Cy_App Stop Done\r\n");
    } else{
        DBG_APP_INFO("Cy_App already stopped\r\n");
    }
}

/**
 * \name Cy_Jtag_AppStart
 * \brief Starts the data stream channels
 * \param pAppCtxt application layer context pointer.
 * \retval None
 */
static void Cy_Jtag_AppStart(cy_stc_usb_app_ctxt_t *pAppCtxt)
{

    DBG_APP_INFO("App Start \r\n");

    if(!cy_IsApplnActive){

        cy_IsApplnActive = true;
        DBG_APP_INFO("App started..\r\n");
    } else{
        DBG_APP_INFO("App already started..\r\n");
    }
    return;


}

/**
 * \name Cy_Jtag_AppTaskHandler
 * \brief JTAG Task Handler
 * \param pTaskParam Task param
 * \retval None
 */
void Cy_Jtag_AppTaskHandler(void *pTaskParam)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt = (cy_stc_usb_app_ctxt_t *)pTaskParam;
    cy_stc_usbd_app_msg_t queueMsg;
    BaseType_t xStatus;
    uint32_t lpEntryTime = 0;
    uint8_t i ;

    vTaskDelay(250);

    Cy_Jtag_GpioInit();
    Cy_GPIO_Write(GPIO_PORT_JTAG_TRST, GPIO_JTAG_TRST, true);

    vTaskDelay(500);

    /* If VBus is present, enable the USB connection. */
    pAppCtxt->vbusPresent = (Cy_GPIO_Read(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN) == VBUS_DETECT_STATE);
    if (pAppCtxt->vbusPresent) {
        Cy_USB_EnableUsbHSConnection(pAppCtxt);
    }

    DBG_APP_INFO("AppThreadCreated\r\n");

    for (;;)
    {
        /*
         * If the link has been in USB2-L1 for more than 0.5 seconds, initiate LPM exit so that
         * transfers do not get delayed significantly.
         */
        if ((MXS40USBHSDEV_USBHSDEV->DEV_PWR_CS & USBHSDEV_DEV_PWR_CS_L1_SLEEP) != 0)
        {
            if ((Cy_USBD_GetTimerTick() - lpEntryTime) >= 500UL) {
                lpEntryTime = Cy_USBD_GetTimerTick();
                Cy_USBD_GetUSBLinkActive(pAppCtxt->pUsbdCtxt);
            }
        } else {
            lpEntryTime = Cy_USBD_GetTimerTick();
        }

        /*
         * Wait until some data is received from the queue.
         * Timeout after 100 ms.
         */
        xStatus = xQueueReceive(pAppCtxt->usbMsgQueue, &queueMsg, 100);
        if (xStatus != pdPASS) {
            continue;
        }

        switch (queueMsg.type) {

            case CY_USB_UVC_VBUS_CHANGE_INTR:
                /* Start the debounce timer. */
                xTimerStart(pAppCtxt->vbusDebounceTimer, 0);
                break;

            case CY_USB_UVC_VBUS_CHANGE_DEBOUNCED:
                /* Check whether VBus state has changed. */
                pAppCtxt->vbusPresent = (Cy_GPIO_Read(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN) == VBUS_DETECT_STATE);

                if (pAppCtxt->vbusPresent) {
                    if (!pAppCtxt->usbConnected) {
                        DBG_APP_INFO("Enabling USB connection due to VBus detect\r\n");
                        Cy_USB_EnableUsbHSConnection(pAppCtxt);
                    }
                } else {
                    if (pAppCtxt->usbConnected) {
                        /* On USB 2.x connections, make sure the DataWire channels are disabled and reset. */
 
                        for (i = 1; i < CY_USB_MAX_ENDP_NUMBER; i++) {
                            if (pAppCtxt->endpInDma[i].valid) {
                                /* DeInit the DMA channel and disconnect the triggers. */
                                Cy_USBHS_App_DisableEpDmaSet(&(pAppCtxt->endpInDma[i]));
                            }

                            if (pAppCtxt->endpOutDma[i].valid) {
                                /* DeInit the DMA channel and disconnect the triggers. */
                                Cy_USBHS_App_DisableEpDmaSet(&(pAppCtxt->endpOutDma[i]));
                            }
                        }
                    }
                    DBG_APP_INFO("Disabling USB connection due to VBus removal\r\n");
                    Cy_USB_DisableUsbHSConnection(pAppCtxt);
                }
                
                break;
            case CY_APP_QUERY_RCVD_EVT_FLAG:
            	    pAppCtxt->glWriteBufferIdx += pAppCtxt->glHsRcvdDataCount;
					Cy_USB_AppQueueRead(pAppCtxt, (uint8_t)APP_CMD_OUT_EP, (uint8_t*)pAppCtxt->glWriteBuffer, CY_APP_MAX_BUFFER_SIZE);

					if(pAppCtxt->glJtagEnabled == true){
						if ((pAppCtxt->glWriteDataCount != 0x00) && (pAppCtxt->glWriteDataCount == pAppCtxt->glWriteBufferIdx)){
							memset((uint8_t *)pAppCtxt->glReadBuffer, 0x00, CY_APP_MAX_BUFFER_SIZE);
							Cy_Jtag_ParseData ((uint8_t *)pAppCtxt->glWriteBuffer, pAppCtxt->glWriteDataCount, (uint8_t *)pAppCtxt->glReadBuffer);
							memset((uint8_t *)pAppCtxt->glWriteBuffer, 0x00, CY_APP_MAX_BUFFER_SIZE);
							pAppCtxt->glWriteDataCount = 0;
						}
					}
                   break;
			case CY_APP_SEND_RSP_EVT_FLAG:
					if(pAppCtxt->glReadDataCount){
						uint16_t count = CY_APP_MAX_BUFFER_SIZE;
						if (pAppCtxt->glReadDataCount < CY_APP_MAX_BUFFER_SIZE){
							count = pAppCtxt->glReadDataCount;
						}
						Cy_USB_AppQueueWrite(pAppCtxt, APP_RESPONSE_IN_EP, pAppCtxt->glReadBuffer, count);
						pAppCtxt->glReadDataCount -= count;
						if (pAppCtxt->glReadDataCount != 0x00){
							DBG_APP_ERR("CY_FX_EP_DEV_TO_HOST stall\r\n");
							Cy_USB_USBD_EndpSetClearStall(pAppCtxt->pUsbdCtxt, APP_RESPONSE_IN_EP, CY_USB_ENDP_DIR_IN, true);
						}
					}
				break;
			case CY_APP_RSP_SENT_EVT_FLAG:
					DBG_APP_TRACE(" CY_APP_RSP_SENT_EVT_FLAG\r\n");
				break;
            default:
            break;
        }
       
    } /* End of for(;;) */
}

/**
 * \name Cy_USB_VbusDebounceTimerCallback
 * \brief Timer used to do debounce on VBus changed interrupt notification.
 * \param xTimer Timer Handle
 * \retval None
 */
void
Cy_USB_VbusDebounceTimerCallback (TimerHandle_t xTimer)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt = (cy_stc_usb_app_ctxt_t *)pvTimerGetTimerID(xTimer);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    cy_stc_usbd_app_msg_t xMsg;

    DBG_APP_INFO("VbusDebounce_CB\r\n");
    if (pAppCtxt->vbusChangeIntr) {
        /* Notify the VCOM task that VBus debounce is complete. */
        xMsg.type = CY_USB_UVC_VBUS_CHANGE_DEBOUNCED;
        xQueueSendFromISR(pAppCtxt->usbMsgQueue, &(xMsg), &(xHigherPriorityTaskWoken));

        /* Clear and re-enable the interrupt. */
        pAppCtxt->vbusChangeIntr = false;
        Cy_GPIO_ClearInterrupt(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN);
        Cy_GPIO_SetInterruptMask(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN, 1);
    }
}   /* end of function  */

/**
 * \name Cy_USB_AppInit
 * \brief   This function Initializes application related data structures, register callback
 *          creates task for device function.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer Context pointer
 * \param pCpuDmacBase DMAC base address
 * \param pCpuDw0Base DataWire 0 base address
 * \param pCpuDw1Base DataWire 1 base address
 * \param pHbDmaMgrCtxt HBDMA Manager Context
 * \retval None
 */
void Cy_USB_AppInit(cy_stc_usb_app_ctxt_t *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, 
                    DMAC_Type *pCpuDmacBase, DW_Type *pCpuDw0Base, DW_Type *pCpuDw1Base,
                    cy_stc_hbdma_mgr_context_t *pHbDmaMgrCtxt)
{
    uint32_t index;
    BaseType_t status = pdFALSE;
    cy_stc_app_endp_dma_set_t *pEndpInDma;
    cy_stc_app_endp_dma_set_t *pEndpOutDma;

    pAppCtxt->devState = CY_USB_DEVICE_STATE_DISABLE;
    pAppCtxt->prevDevState = CY_USB_DEVICE_STATE_DISABLE;
    pAppCtxt->devSpeed = CY_USBD_USB_DEV_FS;
    pAppCtxt->devAddr = 0x00;
    pAppCtxt->activeCfgNum = 0x00;
    pAppCtxt->prevAltSetting = 0x00;
    pAppCtxt->enumMethod = CY_USB_ENUM_METHOD_FAST;
    pAppCtxt->pHbDmaMgrCtxt = pHbDmaMgrCtxt;
    pAppCtxt->pCpuDmacBase = pCpuDmacBase;
    pAppCtxt->pCpuDw0Base = pCpuDw0Base;
    pAppCtxt->pCpuDw1Base = pCpuDw1Base;
    pAppCtxt->pUsbdCtxt = pUsbdCtxt;
    pAppCtxt->glHsRcvdDataCount = 0;
    pAppCtxt->glWriteBufferIdx = 0;

    for (index = 0x00; index < CY_USB_MAX_ENDP_NUMBER; index++)
    {
        pEndpInDma = &(pAppCtxt->endpInDma[index]);
        memset((void *)pEndpInDma, 0, sizeof(cy_stc_app_endp_dma_set_t));

        pEndpOutDma = &(pAppCtxt->endpOutDma[index]);
        memset((void *)pEndpOutDma, 0, sizeof(cy_stc_app_endp_dma_set_t));
    }

    /*
     * Callbacks registered with USBD layer. These callbacks will be called
     * based on appropriate event.
     */
    Cy_USB_AppRegisterCallback(pAppCtxt);

    if (!(pAppCtxt->firstInitDone))
    {

        /* Create the message queue and register it with the kernel. */
        pAppCtxt->usbMsgQueue = xQueueCreate(CY_USB_DEVICE_MSG_QUEUE_SIZE,
                CY_USB_DEVICE_MSG_SIZE);
        if (pAppCtxt->usbMsgQueue == NULL) {
            DBG_APP_ERR("QueuecreateFail\r\n");
            return;
        }

        vQueueAddToRegistry(pAppCtxt->usbMsgQueue, "DeviceMsgQueue");
        /* Create task and check status to confirm task created properly. */
        status = xTaskCreate(Cy_Jtag_AppTaskHandler, "USBDeviceTask", 2048,
                             (void *)pAppCtxt, 5, &(pAppCtxt->usbTaskHandle));

        if (status != pdPASS)
        {
            DBG_APP_ERR("TaskcreateFail\r\n");
            return;
        }

        pAppCtxt->vbusDebounceTimer = xTimerCreate("VbusDebounceTimer", 200, pdFALSE,
                (void *)pAppCtxt, Cy_USB_VbusDebounceTimerCallback);
        if (pAppCtxt->vbusDebounceTimer == NULL) {
            DBG_APP_ERR("TimerCreateFail\r\n");
            return;
        }

        pAppCtxt->glWriteBuffer = (uint8_t *) Cy_HBDma_BufMgr_Alloc(pHbDmaMgrCtxt->pBufMgr, CY_APP_MAX_BUFFER_SIZE);
        pAppCtxt->glReadBuffer = (uint8_t *) Cy_HBDma_BufMgr_Alloc(pHbDmaMgrCtxt->pBufMgr, CY_APP_MAX_BUFFER_SIZE);
        pAppCtxt->firstInitDone = 0x01;
    }
}

/**
 * \name Cy_USB_AppRegisterCallback
 * \brief This function will register all calback with USBD layer.
 * \param pAppCtxt application layer context pointer.
 * \retval None
 */
void Cy_USB_AppRegisterCallback(cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    cy_stc_usb_usbd_ctxt_t *pUsbdCtxt = pAppCtxt->pUsbdCtxt;
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_RESET, Cy_USB_AppBusResetCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_RESET_DONE, Cy_USB_AppBusResetDoneCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_BUS_SPEED, Cy_USB_AppBusSpeedCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SETUP, Cy_USB_AppSetupCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SUSPEND, Cy_USB_AppSuspendCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_RESUME, Cy_USB_AppResumeCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SET_CONFIG, Cy_USB_AppSetCfgCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SET_INTF, Cy_USB_AppSetIntfCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_ZLP, Cy_USB_AppZlpCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SLP, Cy_USB_AppSlpCallback);
}

/**
 * \name Cy_USB_AppSetupEndpDmaParamsHs
 * \brief Configure and enable HBW DMA channels.
 * \param pAppCtxt application layer context pointer.
 * \param pEndpDscr Endpoint descriptor pointer
 * \retval None
 */
static void Cy_USB_AppSetupEndpDmaParamsHs(cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t *pEndpDscr)
{
    uint32_t endpNumber, dir;
    uint16_t maxPktSize;
    cy_stc_app_endp_dma_set_t *pEndpDmaSet;
    DW_Type *pDW;

    Cy_USBD_GetEndpNumMaxPktDir(pEndpDscr, &endpNumber, &maxPktSize, &dir);

    if (*(pEndpDscr + CY_USB_ENDP_DSCR_OFFSET_ADDRESS) & 0x80)
    {
        dir = CY_USB_ENDP_DIR_IN;
        pEndpDmaSet = &(pAppCtxt->endpInDma[endpNumber]);
        pDW = pAppCtxt->pCpuDw1Base;
    }
    else
    {
        dir = CY_USB_ENDP_DIR_OUT;
        pEndpDmaSet = &(pAppCtxt->endpOutDma[endpNumber]);
        pDW = pAppCtxt->pCpuDw0Base;
    }

    Cy_USBHS_App_EnableEpDmaSet(pEndpDmaSet, pDW, endpNumber, endpNumber,(cy_en_usb_endp_dir_t)dir, maxPktSize);
    DBG_APP_INFO("Enable EPDmaSet: endp=%x dir=%x\r\n", endpNumber, dir);

    return;
} /* end of function  */


/**
 * \name Cy_USB_AppConfigureEndp
 * \brief Configure all endpoints used by application (except EP0)
 * \param pUsbdCtxt USBD layer context pointer
 * \param pEndpDscr Endpoint descriptor pointer
 * \retval None
 */
void Cy_USB_AppConfigureEndp(cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, uint8_t *pEndpDscr)
{
    cy_stc_usb_endp_config_t endpConfig;
    cy_en_usb_endp_dir_t endpDirection;
    bool valid;
    uint32_t endpType;
    uint32_t endpNumber, dir;
    uint16_t maxPktSize;
    uint32_t isoPkts = 0x00;
    uint8_t burstSize = 0x00;
    uint8_t maxStream = 0x00;
    uint8_t interval = 0x00;

    /* If it is not endpoint descriptor then return */
    if (!Cy_USBD_EndpDscrValid(pEndpDscr))
    {
        return;
    }
    Cy_USBD_GetEndpNumMaxPktDir(pEndpDscr, &endpNumber, &maxPktSize, &dir);

    if (dir)
    {
        endpDirection = CY_USB_ENDP_DIR_IN;
    }
    else
    {
        endpDirection = CY_USB_ENDP_DIR_OUT;
    }
    Cy_USBD_GetEndpType(pEndpDscr, &endpType);

    if ((CY_USB_ENDP_TYPE_ISO == endpType) || (CY_USB_ENDP_TYPE_INTR == endpType))
    {
        /* The ISOINPKS setting in the USBHS register is the actual packets per microframe value. */
        isoPkts = ((*((uint8_t *)(pEndpDscr + CY_USB_ENDP_DSCR_OFFSET_MAX_PKT + 1)) & CY_USB_ENDP_ADDL_XN_MASK) >> CY_USB_ENDP_ADDL_XN_POS) + 1;
    }

    valid = 0x01;
    Cy_USBD_GetEndpInterval(pEndpDscr, &interval);

    /* Prepare endpointConfig parameter. */
    endpConfig.endpType = (cy_en_usb_endp_type_t)endpType;
    endpConfig.endpDirection = endpDirection;
    endpConfig.valid = valid;
    endpConfig.endpNumber = endpNumber;
    endpConfig.maxPktSize = (uint32_t)maxPktSize;
    endpConfig.isoPkts = isoPkts;
    endpConfig.burstSize = burstSize;
    endpConfig.streamID = maxStream;
    endpConfig.interval = interval;
    /*
     * allowNakTillDmaRdy = true means device will send NAK
     * till DMA setup is ready. This field is applicable to only
     * ingress direction ie OUT transfer/OUT endpoint.
     * For Egress ie IN transfer, this field is ignored.
     */
    endpConfig.allowNakTillDmaRdy = TRUE;
    Cy_USB_USBD_EndpConfig(pUsbdCtxt, endpConfig);

    /* Print status of the endpoint configuration to help debug. */
    DBG_APP_INFO("#ENDPCFG: %d\r\n", endpNumber);
    return;
} /* end of function */

/**
 * \name Cy_USB_AppSetCfgCallback
 * \brief Callback function will be invoked by USBD when set configuration is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer.
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSetCfgCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                              cy_stc_usb_cal_msg_t *pMsg)
{
    glIsDevConfigured = true;
    cy_stc_usb_app_ctxt_t *pUsbApp;
    uint8_t *pActiveCfg, *pIntfDscr, *pEndpDscr;
    uint8_t index, numOfIntf, numOfEndp;

    DBG_APP_INFO("AppSetCfgCbStart\r\n");

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    pUsbApp->devSpeed = Cy_USBD_GetDeviceSpeed(pUsbdCtxt);
    
    /*
    * Based on type of application as well as how data flows,
    * data wire can be used so initialize datawire.
    */
    Cy_DMA_Enable(pUsbApp->pCpuDw0Base);
    Cy_DMA_Enable(pUsbApp->pCpuDw1Base);

    pActiveCfg = Cy_USB_USBD_GetActiveCfgDscr(pUsbdCtxt);
    if (!pActiveCfg)
    {
        /* Set config should be called when active config value > 0x00. */
        return;
    }
    numOfIntf = Cy_USBD_FindNumOfIntf(pActiveCfg);
    if (numOfIntf == 0x00)
    {
        return;
    }

    for (index = 0x00; index < numOfIntf; index++)
    {
        /* During Set Config command always altSetting 0 will be active. */
        pIntfDscr = Cy_USBD_GetIntfDscr(pUsbdCtxt, index, 0x00);
        if (pIntfDscr == NULL)
        {
            DBG_APP_INFO("pIntfDscrNull\r\n");
            return;
        }

        numOfEndp = Cy_USBD_FindNumOfEndp(pIntfDscr);
        if (numOfEndp == 0x00)
        {
            DBG_APP_INFO("numOfEndp 0\r\n");
            continue;
        }

        pEndpDscr = Cy_USBD_GetEndpDscr(pUsbdCtxt, pIntfDscr);
        while (numOfEndp != 0x00)
        {
            Cy_USB_AppConfigureEndp(pUsbdCtxt, pEndpDscr);
            Cy_USB_AppSetupEndpDmaParamsHs(pAppCtxt, pEndpDscr);
            numOfEndp--;
            pEndpDscr = (pEndpDscr + (*(pEndpDscr + CY_USB_DSCR_OFFSET_LEN)));
            
        }
    }

    /* Enable the interrupt for the DataWire channel used for Cmd/Resp endpoints. */
    Cy_USB_AppInitDmaIntr(APP_CMD_OUT_EP, CY_USB_ENDP_DIR_OUT, Cy_Jtag_CmdChannelDataWire_ISR);
    Cy_USB_AppInitDmaIntr(APP_RESPONSE_IN_EP, CY_USB_ENDP_DIR_IN, Cy_Jtag_RespChannelDataWire_ISR);

    pUsbApp->prevDevState = CY_USB_DEVICE_STATE_CONFIGURED;
    pUsbApp->devState = CY_USB_DEVICE_STATE_CONFIGURED;

    Cy_USBD_LpmDisable(pUsbdCtxt);

    glIsDevConfigured = true;
    Cy_Jtag_AppStart(pAppCtxt);

    DBG_APP_INFO("AppSetCfgCbEnd Done\r\n");
    return;
} /* end of function */

/**
 * \name Cy_USB_AppBusResetCallback
 * \brief Callback function will be invoked by USBD when bus detects RESET
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppBusResetCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                                cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;

    DBG_APP_INFO("AppBusResetCallback\r\n");

    /*
     * USBD layer takes care of reseting its own data structure as well as
     * takes care of calling CAL reset APIs. Application needs to take care
     * of reseting its own data structure as well as "device function".
     */
    Cy_USB_AppInit(pUsbApp, pUsbdCtxt, pUsbApp->pCpuDmacBase, pUsbApp->pCpuDw0Base, pUsbApp->pCpuDw1Base, pUsbApp->pHbDmaMgrCtxt);

    if(cy_IsApplnActive){
        Cy_Jtag_AppStop(pAppCtxt, pUsbdCtxt);
    }
    pUsbApp->devState = CY_USB_DEVICE_STATE_RESET;
    pUsbApp->prevDevState = CY_USB_DEVICE_STATE_RESET;

    return;
} /* end of function. */

/**
 * \name Cy_USB_AppBusResetDoneCallback
 * \brief Callback function will be invoked by USBD when RESET is completed
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppBusResetDoneCallback(void *pAppCtxt,
                                    cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                                    cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;

    DBG_APP_INFO("ppBusResetDoneCallback\r\n");

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    pUsbApp->devState = CY_USB_DEVICE_STATE_DEFAULT;
    pUsbApp->prevDevState = pUsbApp->devState;
    return;
} /* end of function. */

void
CyFxAppHaltEndpoint (
    cy_stc_usb_app_ctxt_t *pAppCtxt,
    cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
    uint8_t endpoint
    )
{
    cy_en_usb_endp_dir_t epDir;
    uint8_t endpNum = (endpoint & 0x0F);


    if (endpoint == (0x80 | APP_RESPONSE_IN_EP)) {
        epDir = CY_USB_ENDP_DIR_IN;
    } else if (endpoint == APP_CMD_OUT_EP) {
        epDir = CY_USB_ENDP_DIR_OUT;
    } else {
        return;
    }
    if (cy_IsApplnActive) {
        Cy_USB_USBD_EndpSetClearNakNrdy(pUsbdCtxt, endpNum, epDir, true);
        Cy_SysLib_DelayUs(125);

		if (pAppCtxt->devSpeed < CY_USBD_USB_DEV_SS_GEN1)
		{
			if(epDir != CY_USB_ENDP_DIR_OUT)
				Cy_USBHS_App_ResetEpDma(&(pAppCtxt->endpInDma[endpNum]));
			else
				Cy_USBHS_App_ResetEpDma(&(pAppCtxt->endpOutDma[endpNum]));
		}

        Cy_USBD_FlushEndp(pUsbdCtxt, endpNum, epDir);
        Cy_USBD_ResetEndp(pUsbdCtxt, endpNum, epDir, true);
        Cy_USB_USBD_EndpSetClearStall(pUsbdCtxt, endpNum, epDir, false);
        Cy_USB_USBD_EndpSetClearNakNrdy(pUsbdCtxt, endpNum, epDir, false);
        Cy_USBD_SendACkSetupDataStatusStage(pUsbdCtxt);
        if ((pAppCtxt->devSpeed <= CY_USBD_USB_DEV_HS) && (epDir == 0)){
            /* Queue read on USB OUT endpoint */
            Cy_USB_AppQueueRead(pAppCtxt, endpNum, pAppCtxt->glWriteBuffer, CY_APP_MAX_BUFFER_SIZE);
        }
    }

    DBG_APP_INFO(" Endpoint 0x%x Halted\r\n", endpNum);
}

/**
 * \name Cy_USB_AppBusSpeedCallback
 * \brief   Callback function will be invoked by USBD when speed is identified or
 *          speed change is detected
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppBusSpeedCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                                cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    pUsbApp->devState = CY_USB_DEVICE_STATE_DEFAULT;
    pUsbApp->devSpeed = Cy_USBD_GetDeviceSpeed(pUsbdCtxt);
    return;
} /* end of function. */

/**
 * \name Cy_USB_AppSetupCallback
 * \brief Callback function will be invoked by USBD when SETUP packet is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSetupCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                             cy_stc_usb_cal_msg_t *pMsg)
{
    uint8_t bRequest, bReqType;
    uint8_t bType, bTarget;
    uint16_t wValue, wIndex, wLength;
    bool isReqHandled = false;
    cy_en_usbd_ret_code_t retStatus = CY_USBD_STATUS_SUCCESS;
    cy_en_usb_endp_dir_t epDir = CY_USB_ENDP_DIR_INVALID;

    DBG_APP_TRACE("AppSetupCallback\r\n");

    /* Decode the fields from the setup request. */
    bReqType = pUsbdCtxt->setupReq.bmRequest;
    bType = ((bReqType & CY_USB_CTRL_REQ_TYPE_MASK) >> CY_USB_CTRL_REQ_TYPE_POS);
    bTarget = (bReqType & CY_USB_CTRL_REQ_RECIPENT_OTHERS);
    bRequest = pUsbdCtxt->setupReq.bRequest;
    wValue = pUsbdCtxt->setupReq.wValue;
    wIndex = pUsbdCtxt->setupReq.wIndex;
    wLength = pUsbdCtxt->setupReq.wLength;

    if (bType == CY_USB_CTRL_REQ_STD)
    {
        DBG_APP_INFO("CY_USB_CTRL_REQ_STD\r\n");

        if (bRequest == CY_USB_SC_SET_FEATURE)
        {
            if ((bTarget == CY_USB_CTRL_REQ_RECIPENT_INTF) && (wValue == 0))
            {
                Cy_USB_USBD_EndpSetClearStall(pUsbdCtxt, 0x00, CY_USB_ENDP_DIR_IN, TRUE);
                isReqHandled = true;
            }

            /* SET-FEATURE(EP-HALT) is only supported to facilitate Chapter 9 compliance tests. */
            if ((bTarget == CY_USB_CTRL_REQ_RECIPENT_ENDP) && (wValue == CY_USB_FEATURE_ENDP_HALT))
            {
                epDir = ((wIndex & 0x80UL) ? (CY_USB_ENDP_DIR_IN) : (CY_USB_ENDP_DIR_OUT));
                Cy_USB_USBD_EndpSetClearStall(pUsbdCtxt, ((uint32_t)wIndex & 0x7FUL),
                        epDir, true);

                Cy_USBD_SendAckSetupDataStatusStage(pUsbdCtxt);
                isReqHandled = true;
            }
        }

        if (bRequest == CY_USB_SC_CLEAR_FEATURE)
        {
            if ((bTarget == CY_USB_CTRL_REQ_RECIPENT_INTF) && (wValue == 0))
            {
                Cy_USB_USBD_EndpSetClearStall(pUsbdCtxt, 0x00, CY_USB_ENDP_DIR_IN, TRUE);

                isReqHandled = true;
            }

            if ((bTarget == CY_USB_CTRL_REQ_RECIPENT_ENDP) && (wValue == CY_USB_FEATURE_ENDP_HALT))
            {
                epDir = ((wIndex & 0x80UL) ? (CY_USB_ENDP_DIR_IN) : (CY_USB_ENDP_DIR_OUT));
                if ((wIndex == (0x80 | APP_RESPONSE_IN_EP)) || (wIndex == (APP_CMD_OUT_EP)))
                {
                    CyFxAppHaltEndpoint (pAppCtxt, pUsbdCtxt, wIndex);
                    isReqHandled = true;
                }
            }
        }

        /* Handle Microsoft OS String Descriptor request. */
        if ((bTarget == CY_USB_CTRL_REQ_RECIPENT_DEVICE) &&
                (bRequest == CY_USB_SC_GET_DESCRIPTOR) &&
                (wValue == ((CY_USB_STRING_DSCR << 8) | 0xEE))) {

            /* Make sure we do not send more data than requested. */
            if (wLength > glOsString[0]) {
                wLength = glOsString[0];
            }

            DBG_APP_INFO("OSString\r\n");
            retStatus = Cy_USB_USBD_SendEndp0Data(pUsbdCtxt, (uint8_t *)glOsString, wLength);
            if(retStatus == CY_USBD_STATUS_SUCCESS) {
                isReqHandled = true;
            }
        }

    }

    if (bType == CY_USB_CTRL_REQ_VENDOR) {
        /* If trying to bind to WinUSB driver, we need to support additional control requests. */
        /* Handle OS Compatibility and OS Feature requests */

        if (bRequest == MS_VENDOR_CODE) {
            if (wIndex == 0x04) {
                if (wLength > *((uint16_t *)glOsCompatibilityId)) {
                    wLength = *((uint16_t *)glOsCompatibilityId);
                }

                DBG_APP_INFO("OSCompat\r\n");
                retStatus = Cy_USB_USBD_SendEndp0Data(pUsbdCtxt, (uint8_t *)glOsCompatibilityId, wLength);
                if(retStatus == CY_USBD_STATUS_SUCCESS) {
                    isReqHandled = true;
                }
            }
            else if (wIndex == 0x05) {
                if (wLength > *((uint16_t *)glOsFeature)) {
                    wLength = *((uint16_t *)glOsFeature);
                }

                DBG_APP_INFO("OSFeature\r\n");
                retStatus = Cy_USB_USBD_SendEndp0Data(pUsbdCtxt, (uint8_t *)glOsFeature, wLength);
                if(retStatus == CY_USBD_STATUS_SUCCESS) {
                    isReqHandled = true;
                	}
            	}
        	}
        else
        {
        	isReqHandled = Cy_Jtag_AppHandleVendorCmds(pAppCtxt,bRequest,wValue);
    	}

        if (isReqHandled) {
            return;
        }
    }

    /* If Request is not handled by the callback, Stall the command */
    if (!isReqHandled)
    {
        Cy_USB_USBD_EndpSetClearStall(pUsbdCtxt, 0x00, CY_USB_ENDP_DIR_IN, TRUE);
    }
} /* end of function. */

/**
 * \name Cy_USB_AppReadShortPacket
 * \brief   Function to modify an ongoing DMA read operation to take care of a short
 *          packet.
 * \param pAppCtxt application layer context pointer.
 * \param endpNum endpoint number.
 * \param pktSize Size of the short packet to be read out. Can be zero in case of ZLP.
 * \retval 0x00 or Data size.
 */
uint16_t
Cy_USB_AppReadShortPacket (cy_stc_usb_app_ctxt_t *pAppCtxt,
                           uint8_t endpNum, uint16_t pktSize)
{
    cy_stc_app_endp_dma_set_t *pEndpDmaSet;
    uint16_t dataSize = 0;

    /* Null pointer checks. */
    if ((pAppCtxt == NULL) || (pAppCtxt->pUsbdCtxt == NULL) ||
        (pAppCtxt->pCpuDw0Base == NULL)) {
        DBG_APP_ERR("ReadSLP: BadParam NULL\r\n");
        return 0;
    }

    pEndpDmaSet  = &(pAppCtxt->endpOutDma[endpNum]);
    /* Verify that the selected endpoint is valid. */
    if (pEndpDmaSet->valid == 0) {
        DBG_APP_ERR("ReadSLP: EndpSetNotValid\r\n");
        return 0;
    }

    dataSize = Cy_USBHS_App_ReadShortPacket(pEndpDmaSet, pktSize);
    return dataSize;
} /* end of function */

/**
 * \name Cy_USB_AppSlpCallback
 * \brief This Function will be called by USBD layer when SLP message comes
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void
Cy_USB_AppSlpCallback (void *pUserCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                       cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt = (cy_stc_usb_app_ctxt_t *)pUserCtxt;
    uint16_t rcvdLen = 0;
    uint8_t  endpNumber = (uint8_t)pMsg->data[0];

    if (endpNumber == APP_CMD_OUT_EP){
        /* Prepare to read the short packet of data out from EPM into the DMA buffer. */
        rcvdLen = Cy_USB_AppReadShortPacket(pAppCtxt, endpNumber, (uint16_t)pMsg->data[1]);
        /* Send a trigger to the DMA channel after it has been configured. */
        Cy_TrigMux_SwTrigger(TRIG_IN_MUX_0_USBHSDEV_TR_OUT0 + endpNumber,
                CY_TRIGGER_TWO_CYCLES);
        pAppCtxt->glHsRcvdDataCount = rcvdLen;
        DBG_APP_TRACE("rcvdLen: %d, pktsize: 0x%x\r\n",pAppCtxt->glHsRcvdDataCount, (uint16_t)pMsg->data[1]);
        (void)rcvdLen;
    }
}   /* end of function. */

/**
 * \name Cy_USB_AppZlpCallback
 * \brief This Function will be called by USBD layer when ZLP message comes
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void
Cy_USB_AppZlpCallback (void *pUserCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                       cy_stc_usb_cal_msg_t *pMsg)
{
    DBG_APP_INFO("AppZlpCb\r\n");
    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppSuspendCallback
 * \brief Callback function will be invoked by USBD when Suspend signal/message is detected
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSuspendCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                               cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    pUsbApp->prevDevState = pUsbApp->devState;
    pUsbApp->devState = CY_USB_DEVICE_STATE_SUSPEND;
} /* end of function. */

/**
 * \name Cy_USB_AppResumeCallback
 * \brief Callback function will be invoked by USBD when Resume signal/message is detected
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppResumeCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                              cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;
    cy_en_usb_device_state_t tempState;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;

    tempState = pUsbApp->devState;
    pUsbApp->devState = pUsbApp->prevDevState;
    pUsbApp->prevDevState = tempState;
    return;
} /* end of function. */

/**
 * \name Cy_USB_AppSetIntfCallback
 * \brief Callback function will be invoked by USBD when SET_INTERFACE is  received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSetIntfCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                               cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_setup_req_t *pSetupReq;
    uint8_t intfNum, altSetting;
    int8_t numOfEndp;
    uint8_t *pIntfDscr, *pEndpDscr;
    uint32_t endpNumber;
    cy_en_usb_endp_dir_t endpDirection;
    cy_stc_usb_app_ctxt_t *pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;

    DBG_APP_INFO("AppSetIntfCallback Start\r\n");
    pSetupReq = &(pUsbdCtxt->setupReq);
    /*
     * Get interface and alt setting info. If new setting same as previous
     * then return.
     * If new alt setting came then first Unconfigure previous settings
     * and then configure new settings.
     */
    intfNum = pSetupReq->wIndex;
    altSetting = pSetupReq->wValue;

    if (altSetting == pUsbApp->prevAltSetting)
    {
        DBG_APP_INFO("SameAltSetting\r\n");
        Cy_USB_USBD_EndpSetClearStall(pUsbdCtxt, 0x00, CY_USB_ENDP_DIR_IN, TRUE);
        return;
    }

    /* New altSetting is different than previous one so unconfigure previous. */
    pIntfDscr = Cy_USBD_GetIntfDscr(pUsbdCtxt, intfNum, pUsbApp->prevAltSetting);
    DBG_APP_INFO("unconfigPrevAltSet\r\n");
    if (pIntfDscr == NULL)
    {
        DBG_APP_INFO("pIntfDscrNull\r\n");
        return;
    }
    numOfEndp = Cy_USBD_FindNumOfEndp(pIntfDscr);
    if (numOfEndp == 0x00)
    {
        DBG_APP_INFO("SetIntf:prevNumEp 0\r\n");
    }
    else
    {
        pEndpDscr = Cy_USBD_GetEndpDscr(pUsbdCtxt, pIntfDscr);
        while (numOfEndp != 0x00)
        {
            if (*(pEndpDscr + CY_USB_ENDP_DSCR_OFFSET_ADDRESS) & 0x80)
            {
                endpDirection = CY_USB_ENDP_DIR_IN;
            }
            else
            {
                endpDirection = CY_USB_ENDP_DIR_OUT;
            }
            endpNumber =
                (uint32_t)((*(pEndpDscr + CY_USB_ENDP_DSCR_OFFSET_ADDRESS)) & 0x7F);

            /* with FALSE, unconfgure previous settings. */
            Cy_USBD_EnableEndp(pUsbdCtxt, endpNumber, endpDirection, FALSE);

            numOfEndp--;
            pEndpDscr = (pEndpDscr + (*(pEndpDscr + CY_USB_DSCR_OFFSET_LEN)));
        }
    }

    /* Now take care of different config with new alt setting. */
    pUsbApp->prevAltSetting = altSetting;
    pIntfDscr = Cy_USBD_GetIntfDscr(pUsbdCtxt, intfNum, altSetting);
    if (pIntfDscr == NULL)
    {
        DBG_APP_INFO("pIntfDscrNull\r\n");
        return;
    }

    numOfEndp = Cy_USBD_FindNumOfEndp(pIntfDscr);
    if (numOfEndp == 0x00)
    {
        DBG_APP_INFO("SetIntf:numEp 0\r\n");
    }
    else
    {
        pUsbApp->prevAltSetting = altSetting;
        pEndpDscr = Cy_USBD_GetEndpDscr(pUsbdCtxt, pIntfDscr);
        while (numOfEndp != 0x00)
        {
            Cy_USB_AppConfigureEndp(pUsbdCtxt, pEndpDscr);
            Cy_USB_AppSetupEndpDmaParamsHs(pAppCtxt, pEndpDscr);
            numOfEndp--;
            pEndpDscr = (pEndpDscr + (*(pEndpDscr + CY_USB_DSCR_OFFSET_LEN)));
        }
    }

    DBG_APP_INFO("AppSetIntfCallback done\r\n");
    return;
}

/**
 * \name Cy_USB_AppQueueWrite
 * \brief Queue USBHS Write on the USB endpoint
 * \param pAppCtxt application layer context pointer.
 * \param endpNumber Endpoint number
 * \param pBuffer Data Buffer Pointer
 * \param dataSize DataSize to send on USB bus
 * \retval None
 */
void Cy_USB_AppQueueWrite(cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t endpNumber,
                          uint8_t *pBuffer, uint16_t dataSize)
{
    cy_stc_app_endp_dma_set_t *dmaset_p=NULL;

    /* Null pointer checks. */
    if ((pAppCtxt == NULL) || (pAppCtxt->pUsbdCtxt == NULL) ||
            (pAppCtxt->pCpuDw1Base == NULL) || (pBuffer == NULL)) 
    {
        DBG_APP_ERR("QueueWrite Err0\r\n");
        return;
    }

    /*
     * Verify that the selected endpoint is valid and the dataSize
     * is non-zero.
     */
    dmaset_p = &(pAppCtxt->endpInDma[endpNumber]);
    if ((dmaset_p->valid == 0) || (dataSize == 0)) 
    {
        DBG_APP_ERR("QueueWrite Err1 %d %d\r\n",dmaset_p->valid,dataSize);
        return;
    }

    Cy_USBHS_App_QueueWrite(dmaset_p, pBuffer, dataSize);

} /* end of function */

/**
 * \name Cy_USB_AppQueueRead
 * \brief Function to queue read operation on an OUT endpoint.
 * \param pAppCtxt application layer context pointer.
 * \param endpNum endpoint number.
 * \param endpDir endpoint direction
 * \param pBuffer pointer to buffer where data will be stored.
 * \param dataSize expected data size.
 * \retval None
 */
void
Cy_USB_AppQueueRead (cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t endpNum,
                     uint8_t *pBuffer, uint16_t dataSize)
{
    cy_stc_app_endp_dma_set_t      *pEndpDmaSet;

    DBG_APP_TRACE("pBuffer:0x%x \r\n",pBuffer);

    /* Null pointer checks. */
    if ((pAppCtxt == NULL) || (pAppCtxt->pUsbdCtxt == NULL) ||
       (pAppCtxt->pCpuDw0Base == NULL) || (pBuffer == NULL) ||
       (dataSize == 0)) {

        DBG_APP_ERR("QueueRead: BadParam NULL\r\n");
        return;
    }

    pEndpDmaSet  = &(pAppCtxt->endpOutDma[endpNum]);
    /* If endpoint not valid then dont go ahead. */
    if (pEndpDmaSet->valid == 0) {
        DBG_APP_ERR("QueueRead: EndpSetNotValid\r\n");
        return;
    }

    /* USB HS-FS data recieve case */
    DBG_APP_TRACE("CALLING Cy_USBHS_App_QueueRead\r\n");
    Cy_USBHS_App_QueueRead(pEndpDmaSet, pBuffer, dataSize);
    /* Update xfer count and then disable NAK for the endpoint. */
    Cy_USBD_UpdateXferCount(pAppCtxt->pUsbdCtxt, endpNum,
                            CY_USB_ENDP_DIR_OUT, dataSize);
    /*
        * When device not ready then it will enable NAK.
        * Now device is ready to recieve data so disable NAK.
        */
    Cy_USB_USBD_EndpSetClearNakNrdy(pAppCtxt->pUsbdCtxt, endpNum,
                                    CY_USB_ENDP_DIR_OUT, false);


    pEndpDmaSet->firstRqtDone = true;
    return;

} /* end of function */

/**
 * \name Cy_USB_AppInitDmaIntr
 * \brief Initialize DMA interrupt
 * \param endpNumber Endpoint number
 * \param endpDirection Endpoint direction
 * \param userIsr User ISR
 * \retval None
 */
void Cy_USB_AppInitDmaIntr(uint32_t endpNumber, cy_en_usb_endp_dir_t endpDirection,
                           cy_israddress userIsr)
{
    cy_stc_sysint_t intrCfg;
    if ((endpNumber > 0) && (endpNumber < CY_USB_MAX_ENDP_NUMBER))
    {
#if (!CY_CPU_CORTEX_M4)
        if (endpDirection == CY_USB_ENDP_DIR_IN)
        {
            /* DW1 channels 0 onwards are used for IN endpoints. */
            intrCfg.intrPriority = 3;
            intrCfg.intrSrc = NvicMux4_IRQn;
            intrCfg.cm0pSrc = (cy_en_intr_t)(cpuss_interrupts_dw1_0_IRQn + endpNumber);
        }
        else
        {
            /* DW0 channels 0 onwards are used for OUT endpoints. */
            intrCfg.intrPriority = 3;
            intrCfg.intrSrc = NvicMux1_IRQn;
            intrCfg.cm0pSrc = (cy_en_intr_t)(cpuss_interrupts_dw0_0_IRQn + endpNumber);
        }
#else
        intrCfg.intrPriority = 5;
        if (endpDirection == CY_USB_ENDP_DIR_IN)
        {
            /* DW1 channels 0 onwards are used for IN endpoints. */
            intrCfg.intrSrc =
                (IRQn_Type)(cpuss_interrupts_dw1_0_IRQn + endpNumber);
        }
        else
        {
            /* DW0 channels 0 onwards are used for OUT endpoints. */
            intrCfg.intrSrc =
                (IRQn_Type)(cpuss_interrupts_dw0_0_IRQn + endpNumber);
        }
#endif /* (!CY_CPU_CORTEX_M4) */

        if (userIsr != NULL)
        {
            /* If an ISR is provided, register it and enable the interrupt. */
            Cy_SysInt_Init(&intrCfg, userIsr);
            NVIC_EnableIRQ(intrCfg.intrSrc);
        }
        else
        {
            /* ISR is NULL. Disable the interrupt. */
            NVIC_DisableIRQ(intrCfg.intrSrc);
        }
    }
}

/**
 * \name Cy_USB_AppClearDmaInterrupt
 * \brief Clear DMA Interrupt
 * \param pAppCtxt application layer context pointer.
 * \param endpNumber Endpoint number
 * \param endpDirection Endpoint direction
 * \retval None
 */
void Cy_USB_AppClearDmaInterrupt(cy_stc_usb_app_ctxt_t *pAppCtxt,
                                 uint32_t endpNumber, cy_en_usb_endp_dir_t endpDirection)
{
    if ((pAppCtxt != NULL) && (endpNumber > 0) &&
            (endpNumber < CY_USB_MAX_ENDP_NUMBER)) {
        if (endpDirection == CY_USB_ENDP_DIR_IN) {
            Cy_USBHS_App_ClearDmaInterrupt(&(pAppCtxt->endpInDma[endpNumber]));
        } else  {
            Cy_USBHS_App_ClearDmaInterrupt(&(pAppCtxt->endpOutDma[endpNumber]));
        }
    }
}

/**
 * \name Cy_Jtag_AppCmdRecvCompletion
 * \brief    Function that handles DMA transfer completion on the USB-HS BULK-OUT
 *           endpoint
 * \param pAppCtxt application layer context pointer.
 * \retval None
 */
void
Cy_Jtag_AppCmdRecvCompletion(
        cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    cy_stc_usbd_app_msg_t xMsg;
    BaseType_t status;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	//Cy_USB_AppQueueRead(pAppCtxt, (uint8_t)APP_CMD_OUT_EP, (uint8_t*)pAppCtxt->glWriteBuffer, CY_APP_MAX_BUFFER_SIZE);

    xMsg.type = CY_APP_QUERY_RCVD_EVT_FLAG;
    status = xQueueSendFromISR(pAppCtxt->usbMsgQueue, &(xMsg),
                               &(xHigherPriorityTaskWoken));
    (void)status;
}

/**
 * \name Cy_Jtag_AppRespSendCompletion
 * \brief   Function that handles DMA transfer completion on the USB-HS BULK-IN
 *          endpoint
 * \param pAppCtxt application layer context pointer.
 * \retval None
 */
void
Cy_Jtag_AppRespSendCompletion (
        cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    cy_stc_usbd_app_msg_t xMsg;
    BaseType_t status;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xMsg.type = CY_APP_RSP_SENT_EVT_FLAG;
    status = xQueueSendFromISR(pAppCtxt->usbMsgQueue, &(xMsg),
                               &(xHigherPriorityTaskWoken));
    (void)status;
}

/**
 * \name Cy_CheckStatus
 * \brief Function that handles prints error log
 * \param function Pointer to function
 * \param line Line number where error is seen
 * \param condition condition of failure
 * \param value error code
 * \param isBlocking blocking function
 * \retval None
 */
void Cy_CheckStatus(const char *function, uint32_t line, uint8_t condition, uint32_t value, uint8_t isBlocking)
{
    if (!condition)
    {
        /* Application failed with the error code status */
        Cy_Debug_AddToLog(1, RED);
        Cy_Debug_AddToLog(1, "Function %s failed at line %d with status = 0x%x\r\n", function, line, value);
        Cy_Debug_AddToLog(1, COLOR_RESET);
        if (isBlocking)
        {
            /* Loop indefinitely */
            for (;;)
            {
            }
        }
    }
}

/**
 * \name Cy_CheckStatusHandleFailure
 * \brief Function that handles prints error log
 * \param function Pointer to function
 * \param line LineNumber where error is seen
 * \param condition Line number where error is seen
 * \param value error code
 * \param isBlocking blocking function
 * \param failureHandler failure handler function
 * \retval None
 */
void Cy_CheckStatusHandleFailure(const char *function, uint32_t line, uint8_t condition, uint32_t value, uint8_t isBlocking, void (*failureHandler)(void))
{
    if (!condition)
    {
        /* Application failed with the error code status */
        Cy_Debug_AddToLog(1, RED);
        Cy_Debug_AddToLog(1, "Function %s failed at line %d with status = 0x%x\r\n", function, line, value);
        Cy_Debug_AddToLog(1, COLOR_RESET);

        if(failureHandler != NULL)
        {
            (*failureHandler)();
        }
        if (isBlocking)
        {
            /* Loop indefinitely */
            for (;;)
            {
            }
        }
    }
}

/**
 * \name Cy_USB_FailHandler
 * \brief Error Handler
 * \retval None
 */
void Cy_FailHandler(void)
{
    DBG_APP_ERR("Reset Done\r\n");
}

/* [] END OF FILE */
