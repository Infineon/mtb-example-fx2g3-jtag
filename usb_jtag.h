/***************************************************************************//**
* \file jtag.h
* \version 1.0
*
* \brief Header file providing interface definitions for the USB JTAG application.
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

#ifndef USB_JTAG_H_
#define USB_JTAG_H_

extern volatile uint16_t    glHsRcvdDataCount;
extern volatile uint16_t    glWriteBufferIdx;


#define GPIO_PORT_JTAG_TDO                      (P7_0_PORT)
#define GPIO_JTAG_TDO                           (P7_0_PIN) 

#define GPIO_PORT_JTAG_TDI                      (P7_1_PORT)
#define GPIO_JTAG_TDI                           (P7_1_PIN)

#define GPIO_PORT_JTAG_TMS                      (P7_3_PORT)
#define GPIO_JTAG_TMS                           (P7_3_PIN)

#define GPIO_PORT_JTAG_TCK                      (P7_2_PORT)
#define GPIO_JTAG_TCK                           (P7_2_PIN)

#define GPIO_PORT_JTAG_TRST                     (P7_4_PORT)
#define GPIO_JTAG_TRST                          (P7_4_PIN)


#define CY_U3P_JTAG_TMS_HIGH                    (1)
#define CY_U3P_JTAG_TMS_LOW                     (0)
#define CY_U3P_JTAG_TDI_HIGH                    (1)
#define CY_U3P_JTAG_TDI_LOW                     (0)


typedef enum cy_en_usb_vendorCmd_t
{
    CY_U3P_VENDOR_JTAG_ENABLE       = 0xD0, /* This command initializes and enables the JTAG module. */
    CY_U3P_VENDOR_JTAG_DISABLE      = 0xD1, /* This command disables the JTAG module. */
    CY_U3P_VENDOR_JTAG_READ         = 0xD2, /* This command indicates the amount of data that will be read by the USB Host. This is indicated in the wValue field of the setup packet. */
    CY_U3P_VENDOR_JTAG_WRITE        = 0xD3, /* This command indicates the amount of data that will be written by the USB Host. This is indicated in the wValue field of the setup packet. */
} cy_en_usb_vendorCmd_t;

typedef enum cy_en_jtag_sm_t
{
    JTAG_TAP_START              = 0,    
    JTAG_TAP_INT_EP_WRITE_D3    = 1,    
    JTAG_TAP_BULK_PKT_RECEIVED  = 2,    
    JTAG_TAP_PARSE_COMPLETE     = 3,    
    JTAG_TAP_INT_EP_READ_D2     = 4,    
    JTAG_TAP_BULK_PKT_SENT      = 5,    
    JTAG_TAP_STOP               = 0xFF
} cy_en_jtag_sm_t;

typedef enum cy_en_jtag_tapstate_t
{
    CY_U3P_JTAG_TAP_TEST_LOGIC_RESET = 0,   
    CY_U3P_JTAG_TAP_RUN_TEST_IDLE,          
    CY_U3P_JTAG_TAP_SELECT_DR,              
    CY_U3P_JTAG_TAP_CAPTURE_DR,             
    CY_U3P_JTAG_TAP_SHIFT_DR,               
    CY_U3P_JTAG_TAP_EXIT1_DR,               
    CY_U3P_JTAG_TAP_PAUSE_DR,               
    CY_U3P_JTAG_TAP_EXIT2_DR,               
    CY_U3P_JTAG_TAP_UPDATE_DR,              
    CY_U3P_JTAG_TAP_SELECT_IR,              
    CY_U3P_JTAG_TAP_CAPTURE_IR,             
    CY_U3P_JTAG_TAP_SHIFT_IR,               
    CY_U3P_JTAG_TAP_EXIT1_IR,               
    CY_U3P_JTAG_TAP_PAUSE_IR,               
    CY_U3P_JTAG_TAP_EXIT2_IR,               
    CY_U3P_JTAG_TAP_UPDATE_IR               
} cy_en_jtag_tapstate_t;

typedef struct cy_en_jtag_tapstate_trans_t
{
    uint8_t bitMap;
    uint8_t numBits;
} cy_en_jtag_tapstate_trans_t;

/* Enumeration of the JTAG commands */
typedef enum cy_en_jtag_cmds_t
{
    CY_U3P_JTAG_SET_CLK_DIV = 0,    
    CY_U3P_JTAG_SET_TAP_STATE,      
    CY_U3P_JTAG_GET_TAP_STATE,      
    CY_U3P_JTAG_TAP_SW_RESET,       
    CY_U3P_JTAG_TAP_HW_RESET,       
    CY_U3P_JTAG_SET_SHIFT_MODE,     
    CY_U3P_JTAG_SHIFT_READ_N_BITS,  
    CY_U3P_JTAG_RTI_LOOP,       
    CY_U3P_JTAG_SHIFT_READ_N_BYTES  
} cy_en_jtag_cmds_t;

typedef enum cy_en_jtag_shiftmode_t
{
    CY_U3P_JTAG_SHIFT_MSB_FIRST = 0,
    CY_U3P_JTAG_SHIFT_LSB_FIRST
} cy_en_jtag_shiftmode_t;

/**
 * \name Cy_Jtag_ParseData
 * \brief This function parse the data received on Command channel.
 * \param data Pointer to Data from the host
 * \param length Length of the data received 
 * \param readData Data read from TDO 
 * \retval None
 */
void Cy_Jtag_ParseData(uint8_t *data, uint16_t length, uint8_t *readData);

/**
 * \name Cy_Jtag_Init
 * \brief This function initializes the JTAG.
 * \param cy_stc_usb_app_ctxt_t Application context
 * \retval None
 */
void Cy_Jtag_GpioInit (void);

/**
 * \name Cy_Jtag_AppHandleVendorCmds
 * \brief This function handles the USB Vendor commands
 * \param cy_stc_usb_app_ctxt_t Application context
 * \param bRequest bRequest field 
 * \param wValue wValue field 
 * \retval None
 */
bool Cy_Jtag_AppHandleVendorCmds(cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t bRequest, uint16_t wValue);

#endif /* USB_JTAG_H_ */
