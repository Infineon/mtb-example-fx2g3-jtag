/***************************************************************************//**
* \file jtag.c
* \version 1.0
*
* \brief Implements the JTAG protocol using GPIO bit-banging.
*
*******************************************************************************
* \copyright
* (c) (2026), Cypress Semiconductor Corporation (an Infineon company) or
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
#include "usb_app.h"
#include "usb_jtag.h"


cy_en_jtag_sm_t jtagStateMachine = JTAG_TAP_STOP;
/* Globals of the JTAG Module. */
cy_en_jtag_tapstate_t glJtagTapState;
cy_en_jtag_shiftmode_t glJtagShiftMode;

/* JTAG state transition state machine */
cy_en_jtag_tapstate_trans_t glJtagStateTrans [16][16] =
{
//    0x00       0x01       0x02       0x03       0x04       0x05       0x06       0x07       0x08       0x09       0x0A       0x0B       0x0C       0x0D       0x0E       0x0F
    {{0x01, 1}, {0x00, 1}, {0x02, 2}, {0x02, 3}, {0x02, 4}, {0x0A, 4}, {0x0A, 5}, {0x2A, 6}, {0x1A, 5}, {0x06, 3}, {0x06, 4}, {0x06, 5}, {0x16, 5}, {0x16, 6}, {0x56, 7}, {0x36, 6}}, //0x00
    {{0x1F, 5}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3}, {0x05, 4}, {0x15, 5}, {0x0D, 4}, {0x03, 2}, {0x03, 3}, {0x03, 4}, {0x0B, 4}, {0x0B, 5}, {0x2B, 6}, {0x1B, 5}}, //0x01
    {{0x1F, 5}, {0x06, 4}, {0x00, 0}, {0x00, 1}, {0x00, 2}, {0x02, 2}, {0x02, 3}, {0x0A, 4}, {0x06, 3}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3}, {0x05, 4}, {0x15, 5}, {0x0D, 4}}, //0x02
    {{0x1F, 5}, {0x03, 3}, {0x07, 3}, {0x00, 0}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2}, {0x0F, 4}, {0x0F, 5}, {0x0F, 6}, {0x2F, 6}, {0x2F, 7}, {0xAF, 8}, {0x6F, 7}}, //0x03
    {{0x1F, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2}, {0x0F, 4}, {0x0F, 5}, {0x0F, 6}, {0x2F, 6}, {0x2F, 7}, {0xAF, 8}, {0x6F, 7}}, //0x04
    {{0x1F, 5}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x02, 3}, {0x00, 0}, {0x00, 1}, {0x02, 2}, {0x01, 1}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6}}, //0x05
    {{0x1F, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x01, 2}, {0x05, 3}, {0x00, 1}, {0x01, 1}, {0x03, 2}, {0x0F, 4}, {0x0F, 5}, {0x0F, 6}, {0x2F, 6}, {0x2F, 7}, {0xAF, 8}, {0x6F, 7}}, //0x06
    {{0x1F, 5}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x00, 1}, {0x02, 2}, {0x02, 3}, {0x00, 0}, {0x01, 1}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6}}, //0x07
    {{0x1F, 5}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3}, {0x05, 4}, {0x15, 5}, {0x00, 0}, {0x03, 2}, {0x03, 3}, {0x03, 4}, {0x0B, 4}, {0x0B, 5}, {0x2B, 6}, {0x1B, 5}}, //0x08
    {{0x01, 1}, {0x06, 4}, {0x0E, 4}, {0x0E, 5}, {0x0E, 6}, {0x2E, 6}, {0x2E, 7}, {0xAE, 8}, {0x6E, 7}, {0x00, 0}, {0x00, 1}, {0x00, 2}, {0x02, 2}, {0x02, 3}, {0x0A, 4}, {0x06, 3}}, //0x09
    {{0x1F, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0F, 4}, {0x00, 0}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2}}, //0x0A
    {{0x1F, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0F, 4}, {0x0F, 5}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2}}, //0x0B
    {{0x1F, 5}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x03, 4}, {0x0B, 4}, {0x0B, 5}, {0x2B, 6}, {0x1B, 5}, {0x07, 3}, {0x07, 4}, {0x02, 3}, {0x00, 0}, {0x00, 1}, {0x02, 2}, {0x01, 1}}, //0x0C
    {{0x1F, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0F, 4}, {0x0F, 5}, {0x01, 2}, {0x05, 3}, {0x00, 1}, {0x01, 1}, {0x03, 2}}, //0x0D
    {{0x1F, 5}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x03, 4}, {0x0B, 4}, {0x0B, 5}, {0x2B, 6}, {0x1B, 5}, {0x07, 3}, {0x07, 4}, {0x00, 1}, {0x02, 2}, {0x02, 3}, {0x00, 0}, {0x01, 1}}, //0x0E
    {{0x1F, 5}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3}, {0x05, 4}, {0x15, 5}, {0x0D, 4}, {0x03, 2}, {0x03, 3}, {0x03, 4}, {0x0B, 4}, {0x0B, 5}, {0x2B, 6}, {0x00, 0}}  //0x0F
};

/*******************************************************************************
 * Function name: Cy_Jtag_GpioInit
 ****************************************************************************//**
 *
 * JTAG GPIO Init function.
 * 
 * Parameters:
 *  None
 *
 * Return:
 *  None
 *
 *******************************************************************************/
void Cy_Jtag_GpioInit (void)
{
    cy_stc_gpio_pin_config_t pinCfg;
    cy_en_gpio_status_t status = 0;

    memset((void *)&pinCfg, 0, sizeof(pinCfg));
    /* Configure VBus detect GPIO. */
    pinCfg.driveMode = CY_GPIO_DM_HIGHZ;
    pinCfg.hsiom = HSIOM_SEL_GPIO;
    status = Cy_GPIO_Pin_Init(GPIO_PORT_JTAG_TDO, GPIO_JTAG_TDO, &pinCfg);
    if(status != 0)
    {
        DBG_APP_ERR("GPIO inti failed %x \n\r",status);
    }

    pinCfg.driveMode = CY_GPIO_DM_STRONG_IN_OFF;
    status = Cy_GPIO_Pin_Init(GPIO_PORT_JTAG_TDI, GPIO_JTAG_TDI, &pinCfg);
    if(status != 0)
    {
        DBG_APP_ERR("GPIO inti failed %x \n\r",status);
    }
    status = Cy_GPIO_Pin_Init(GPIO_PORT_JTAG_TMS, GPIO_JTAG_TMS, &pinCfg);
    if(status != 0)
    {
        DBG_APP_ERR("GPIO inti failed %x \n\r",status);
    }
    status = Cy_GPIO_Pin_Init(GPIO_PORT_JTAG_TCK, GPIO_JTAG_TCK, &pinCfg);
    if(status != 0)
    {
        DBG_APP_ERR("GPIO inti failed %x \n\r",status);
    }
    status = Cy_GPIO_Pin_Init(GPIO_PORT_JTAG_TRST, GPIO_JTAG_TRST, &pinCfg);
    if(status != 0)
    {
        DBG_APP_ERR("GPIO inti failed %x \n\r",status);
    }

    DBG_APP_INFO("Cy_Jtag_GpioInit \n\r");
}

/*******************************************************************************
 * Function name: Cy_Jtag_SetTck
 ****************************************************************************//**
 *
 * Function to Set the TCK GPIO.
 * 
 * \param tckVal
 * TCK value to set
 *
 * Return:
 *  None
 *
 *******************************************************************************/
static void Cy_Jtag_SetTck(bool tckVal)
{
    Cy_GPIO_Write(GPIO_PORT_JTAG_TCK, GPIO_JTAG_TCK, tckVal);
    Cy_SysLib_DelayUs(1);
}

/*******************************************************************************
 * Function name: Cy_Jtag_SetTms
 ****************************************************************************//**
 *
 * Function to Set the TMS GPIO.
 * 
 * \param tmsVal
 * TMS value to set
 *
 * Return:
 *  None
 *
 *******************************************************************************/
static void Cy_Jtag_SetTms(bool tmsVal)
{
    bool tmsLastVal;
    tmsLastVal = Cy_GPIO_ReadOut(GPIO_PORT_JTAG_TMS, GPIO_JTAG_TMS);
    if(tmsVal != tmsLastVal){
        Cy_GPIO_Write(GPIO_PORT_JTAG_TMS, GPIO_JTAG_TMS, tmsVal);
    }
}

/*******************************************************************************
 * Function name: Cy_Jtag_SetTdi
 ****************************************************************************//**
 *
 * Function to Set the TDI GPIO.
 * 
 * \param TDI
 * TDI value to set
 *
 * Return:
 *  None
 *
 *******************************************************************************/
static void Cy_Jtag_SetTdi(bool tdiVal)
{
    bool tdiLastVal;
    tdiLastVal = Cy_GPIO_ReadOut(GPIO_PORT_JTAG_TDI, GPIO_JTAG_TDI);
    if(tdiVal != tdiLastVal){
        Cy_GPIO_Write(GPIO_PORT_JTAG_TDI, GPIO_JTAG_TDI, tdiVal);
    }
}

/*******************************************************************************
 * Function name: Cy_Jtag_SetTdi
 ****************************************************************************//**
 *
 * Function to Set the TDI GPIO.
 * 
 * \param tmsVal
 * TMS GPIO value to set
 * 
 * \param tdiVal
 * TDI GPIO value to set
 *
 * Return:
 *  None
 *
 *******************************************************************************/
static void Cy_Jtag_UpdateTmsTdi(bool tmsVal, bool tdiVal)
{
    Cy_Jtag_SetTdi(tdiVal);
    Cy_Jtag_SetTms(tmsVal);
}

/*******************************************************************************
 * Function name: Cy_Jtag_ResetTapState(void)
 ****************************************************************************//**
 *
 * Function to Reset the TAP state.
 * 
 * Parameters:
 * None
 *
 * Return:
 *  None
 *
 *******************************************************************************/
static void Cy_Jtag_ResetTapState(void)
{
    uint8_t i = 0;
    for (i = 0; i < 5; i++){
        Cy_Jtag_SetTck(false);
        Cy_Jtag_UpdateTmsTdi (CY_U3P_JTAG_TMS_HIGH, CY_U3P_JTAG_TDI_LOW);
        Cy_Jtag_SetTck(true);
    }
    /* Set the Initial JTAG TAP State */
    glJtagTapState = CY_U3P_JTAG_TAP_TEST_LOGIC_RESET;
}

/*******************************************************************************
 * Function name: Cy_Jtag_ChangeTapState(cy_en_jtag_tapstate_t state
 ****************************************************************************//**
 *
 * This function changes the TAP state from the current state to the requested state.
 * 
 * Parameters:
 * \param state
 * Requested TAP state
 *
 * Return:
 *  None
 *
 *******************************************************************************/
static void Cy_Jtag_ChangeTapState (cy_en_jtag_tapstate_t state)
{
    uint8_t i = 0;
    uint8_t numTrans;
    uint8_t bitMap;

    numTrans = glJtagStateTrans[glJtagTapState][state].numBits;
    bitMap   = glJtagStateTrans[glJtagTapState][state].bitMap;

    for (i = 0; i < numTrans; i++){
        /* Set/Clear the TMS Line */
        Cy_Jtag_SetTck(false);
        Cy_Jtag_UpdateTmsTdi(((bitMap >> i) & 0x01), 0);
        Cy_Jtag_SetTck(true);
    }

    /* Update the TAP State Variable */
    glJtagTapState = state;
}

/*******************************************************************************
 * Function name: Cy_Jtag_SetTapState (cy_en_jtag_tapstate_t state)
 ****************************************************************************//**
 *
 * This function sets the TAP state from the current state to the requested state.
 * 
 * Parameters:
 * \param state
 * Requested TAP state
 *
 * Return:
 *  status
 *
 *******************************************************************************/
unsigned int Cy_Jtag_SetTapState (cy_en_jtag_tapstate_t state)
{
    if (state == glJtagTapState){
        return CY_USB_APP_STATUS_SUCCESS;
    }

    if (state <= CY_U3P_JTAG_TAP_UPDATE_IR){
        Cy_Jtag_ChangeTapState (state);
        return CY_USB_APP_STATUS_SUCCESS;
    }

    /* Invalid State */
    return CY_USB_APP_STATUS_FAILURE;
}

/*******************************************************************************
 * Function name: Cy_Jtag_ShiftBit(uint8_t tmsVal, uint8_t tdiVal)
 ****************************************************************************//**
 *
 * This function shifts 1 bit data onto TMS & TDI GPIOs.
 * 
 * Parameters:
 * \param tmsVal
 * TMS Value
 * \param  tdiVal
 * TDI Value
 * 
 * Return:
 *  TDO read value
 *
 *******************************************************************************/
static uint8_t Cy_Jtag_ShiftBit(uint8_t tmsVal, uint8_t tdiVal)
{
    bool temp = 0;
/*
 * 1. Clear the TCK line
 * 2. Set or clear the TDI/TMS lines as required
 * 3. Read TDO line
 * 4. Set the TCK line
 */
    Cy_Jtag_SetTck(false);
    Cy_Jtag_UpdateTmsTdi (tmsVal, tdiVal);
    temp = Cy_GPIO_Read(GPIO_PORT_JTAG_TDO, GPIO_JTAG_TDO);
    Cy_Jtag_SetTck(true);
    return temp;
}

/*******************************************************************************
 * Function name: Cy_Jtag_ShiftNBits (uint8_t numBits, bool setTms, uint8_t tdiVal, uint8_t *tdoVal)
 ****************************************************************************//**
 *
 * This function sets the TAP state from the current state to the requested state.
 * 
 * Parameters:
 * \param tmsVal
 * TMS Value
 * \param tdiVal
 * TDI Value
 * \param tdoVal
 * Data on TDO
 * 
 * Return:
 * None
 *
 *******************************************************************************/
static void Cy_Jtag_ShiftNBits(uint8_t numBits, bool setTms, uint8_t tdiVal, uint8_t *tdoVal)
{
    uint8_t i = 0;
    uint8_t temp = 0;
    *tdoVal = 0;

    if (glJtagShiftMode == CY_U3P_JTAG_SHIFT_LSB_FIRST){
        for (i = 0; i < (numBits - 1); i++){
            temp = Cy_Jtag_ShiftBit (0, ((tdiVal >> i) & 0x01));
            /* Store the TDO value */
            *tdoVal |= (temp << i);
        }
    } else{
        for (i = (numBits - 1); i > 0; i--){
            temp = Cy_Jtag_ShiftBit (0, ((tdiVal >> i) & 0x01));
            /* Store the TDO value */
            *tdoVal |= (temp << i);
        }
    }

    /* Shift the last bit */
    temp = Cy_Jtag_ShiftBit (setTms, ((tdiVal >> i) & 0x01));
    *tdoVal |= (temp << i);

    if (setTms){
        glJtagTapState++;
    }
}

/*******************************************************************************
 * Function name: Cy_Jtag_ParseData(uint8_t *data, uint16_t length, uint8_t *readData)
 ****************************************************************************//**
 *
 * This function parse the data received on Command channel.
 * 
 * Parameters:
 * \param data
 * Pointer to Data from the host
 *
 * \param length
 * Length of the data received
 *
 * \param readData
 * Data read from TDO
 * 
 * Return:
 * None
 *
 *******************************************************************************/
void Cy_Jtag_ParseData(uint8_t *data, uint16_t length, uint8_t *readData)
{
    uint8_t temp = 0, i = 0, outData = 0;
    uint16_t readIndex = 0, index = 0, val = 0;

    while (index < length){
        switch (data[index] & 0xF) {
            case CY_U3P_JTAG_SET_CLK_DIV:
                /* We support only one clock frequency ie., 48 MHz */
                break;

            case CY_U3P_JTAG_SET_TAP_STATE:
                Cy_Jtag_SetTapState ((cy_en_jtag_tapstate_t)((data[index] >> 4) & 0xF));
                break;

            case CY_U3P_JTAG_GET_TAP_STATE:
                readData[readIndex++] = glJtagTapState | (glJtagShiftMode << 4) | (1 << 5);
                break;

            case CY_U3P_JTAG_TAP_SW_RESET:
                Cy_Jtag_ResetTapState ();
                break;

            case CY_U3P_JTAG_TAP_HW_RESET:
                Cy_Jtag_ResetTapState ();
                break;

            case CY_U3P_JTAG_SET_SHIFT_MODE:
                if (data[index] & 0x10){
                    glJtagShiftMode = CY_U3P_JTAG_SHIFT_LSB_FIRST;
                }else{
                    glJtagShiftMode = CY_U3P_JTAG_SHIFT_MSB_FIRST;
                }
                break;

            case CY_U3P_JTAG_SHIFT_READ_N_BITS:
                temp = ((data[index] >> 5) & 0x07) + 1;
                val = data[index] & 0x10 ? 1 : 0;
                Cy_Jtag_ShiftNBits (temp, val, data[index + 1], &outData);
                readData[readIndex++] = outData;
                index++;
                break;

            case CY_U3P_JTAG_RTI_LOOP:
                if (glJtagTapState == CY_U3P_JTAG_TAP_RUN_TEST_IDLE){
                    temp = (data[index] >> 4) + 1;
                    for (i = 0; i < temp; i++){
                        Cy_Jtag_SetTck(false);
                        Cy_Jtag_UpdateTmsTdi(CY_U3P_JTAG_TMS_LOW, CY_U3P_JTAG_TDI_LOW);
                        Cy_Jtag_SetTck(true);
                    }
                }
                break;

            case CY_U3P_JTAG_SHIFT_READ_N_BYTES:
                break;
            default:
                break;
        }
        index++;
    }
}

/*******************************************************************************
 * Function name: Cy_Jtag_Init(cy_stc_usb_app_ctxt_t *pAppCtxt)
 ****************************************************************************//**
 *
 * This function initializes the JTAG.
 * 
 * Parameters:
 * \param pAppCtxt
 * application layer context pointer
 * 
 * \return
 * application status
 *
 *******************************************************************************/
static uint8_t Cy_Jtag_Init(cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    uint8_t status = CY_USB_APP_STATUS_SUCCESS;

    Cy_Jtag_ResetTapState ();
    glJtagShiftMode = CY_U3P_JTAG_SHIFT_LSB_FIRST;
    pAppCtxt->glJtagEnabled = true;

    return status ;
}

/*******************************************************************************
 * Function name: Cy_Jtag_AppHandleVendorCmds(cy_stc_usb_app_ctxt_t *pAppCtxt,
 *                                          uint8_t bRequest, uint16_t wValue)
 ****************************************************************************//**
 *
 * This function handles the USB Vendor commands
 * 
 * Parameters:
 * \param pAppCtxt
 * application layer context pointer
 *
 * \param bRequest
 *  bRequest control request field
 * 
 * \param wValue
 *  wValue control request field
 *
 * \return
 * control request handled
 *
 *******************************************************************************/
bool Cy_Jtag_AppHandleVendorCmds(cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t bRequest, uint16_t wValue)
{
    bool isReqHandled = false;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    cy_stc_usbd_app_msg_t xMsg;

    if (bRequest == CY_U3P_VENDOR_JTAG_ENABLE)
    {
    DBG_APP_TRACE("JTAG:CY_U3P_VENDOR_JTAG_ENABLE  \n\r");
    Cy_Jtag_Init (pAppCtxt);
    jtagStateMachine = JTAG_TAP_START;
    Cy_USBD_SendACkSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
    isReqHandled = true;

    if ((pAppCtxt->devSpeed <= CY_USBD_USB_DEV_HS)){
        /* Queue read on USB OUT endpoint */
        Cy_USB_AppQueueRead(pAppCtxt, APP_CMD_OUT_EP, pAppCtxt->glWriteBuffer, CY_APP_MAX_BUFFER_SIZE);
    }

    }
     else if(bRequest == CY_U3P_VENDOR_JTAG_DISABLE)
     {
        DBG_APP_TRACE("JTAG:CY_U3P_VENDOR_JTAG_DISABLE  \n\r");
        jtagStateMachine = JTAG_TAP_STOP;
        Cy_USBD_SendACkSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
        isReqHandled = true;
     }
     else if(bRequest == CY_U3P_VENDOR_JTAG_READ)
     {
         DBG_APP_TRACE("CY_U3P_VENDOR_JTAG_READ \n\r");
        pAppCtxt->glReadDataCount = wValue;
        jtagStateMachine = JTAG_TAP_INT_EP_READ_D2;
        Cy_USBD_SendACkSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
        isReqHandled = true;

         xMsg.type = CY_APP_SEND_RSP_EVT_FLAG;
         xQueueSendFromISR(pAppCtxt->usbMsgQueue, &(xMsg), &(xHigherPriorityTaskWoken));

     }
     else if(bRequest == CY_U3P_VENDOR_JTAG_WRITE)
     {
        DBG_APP_TRACE("CY_U3P_VENDOR_JTAG_WRITE \n\r");
        pAppCtxt->glWriteDataCount = wValue;
        pAppCtxt->glWriteBufferIdx = 0;
        jtagStateMachine = JTAG_TAP_INT_EP_WRITE_D3;
        Cy_USBD_SendACkSetupDataStatusStage(pAppCtxt->pUsbdCtxt);
        isReqHandled = true;
     }

     return isReqHandled;
}



