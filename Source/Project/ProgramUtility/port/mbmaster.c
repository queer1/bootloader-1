/* 
 * FreeModbus Libary: A portable Modbus implementation for Modbus ASCII/RTU.
 * Copyright (c) 2006 Christian Walter <wolti@sil.at>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * File: $Id: mb.c,v 1.28 2010/06/06 13:54:40 wolti Exp $
 */

/* Modified to operate in master mode */

/* ----------------------- System includes ----------------------------------*/
#include "stdlib.h"
#include "string.h"
#include <unistd.h>
/* ----------------------- Platform includes --------------------------------*/
#include "port.h"
#include "platform.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbconfig.h"
#include "mbframe.h"
#include "mbproto.h"
#include "mbfunc.h"

#include "mbport.h"

#if MB_RTU_ENABLED == 1
#include "mbrtu.h"
#endif
#if MB_ASCII_ENABLED == 1
#include "mbascii.h"
#endif
#if MB_TCP_ENABLED == 1
#include "mbtcp.h"
#endif

#ifndef MB_PORT_HAS_CLOSE
#define MB_PORT_HAS_CLOSE 0
#endif

/* ----------------------- Static variables ---------------------------------*/

//static UCHAR    ucMBAddress;
static eMBMode  eMBCurrentMode;
//static UCHAR *pucFrameToSend = NULL;
//static USHORT usFrameLength = NULL;

/**
 * ModBus states
 */
static enum
{
    STATE_ENABLED,       //!< STATE_ENABLED
    STATE_DISABLED,      //!< STATE_DISABLED
    STATE_NOT_INITIALIZED//!< STATE_NOT_INITIALIZED
} eMBState = STATE_NOT_INITIALIZED;

/* Functions pointer which are initialized in eMBInit( ). Depending on the
 * mode (RTU or ASCII) the are set to the correct implementations.
 */
static peMBFrameSend peMBFrameSendCur;
static pvMBFrameStart pvMBFrameStartCur;
static pvMBFrameStop pvMBFrameStopCur;
static peMBFrameReceive peMBFrameReceiveCur;
static pvMBFrameClose pvMBFrameCloseCur;

/* Callback functions required by the porting layer. They are called when
 * an external event has happend which includes a timeout or the reception
 * or transmission of a character.
 */
BOOL( *pxMBFrameCBByteReceived ) ( void );
BOOL( *pxMBFrameCBTransmitterEmpty ) ( void );
BOOL( *pxMBPortCBTimerExpired ) ( void );
BOOL( *pxMBPortCBMasterTimerExpired ) ( void );

BOOL( *pxMBFrameCBReceiveFSMCur ) ( void );
BOOL( *pxMBFrameCBTransmitFSMCur ) ( void );
extern void xMBRTUMasterTimerExpired ( void );
extern void vMBPortMasterTimerEnable( ULONG ulTimeOutMSec );
extern void vMBPortMasterTimerDisable( void );

/* An array of Modbus functions handlers which associates Modbus function
 * codes with implementing functions.
 */
static xMBFunctionHandler xFuncHandlers[MB_FUNC_HANDLERS_MAX] = {
#if MB_FUNC_OTHER_REP_SLAVEID_ENABLED > 0
    {MB_FUNC_OTHER_REPORT_SLAVEID, eMBFuncReportSlaveID},
#endif
#if MB_FUNC_READ_INPUT_ENABLED > 0
    {MB_FUNC_READ_INPUT_REGISTER, eMBFuncReadInputRegister},
#endif
#if MB_FUNC_READ_HOLDING_ENABLED > 0
    {MB_FUNC_READ_HOLDING_REGISTER, eMBFuncReadHoldingRegister},
#endif
#if MB_FUNC_WRITE_MULTIPLE_HOLDING_ENABLED > 0
    {MB_FUNC_WRITE_MULTIPLE_REGISTERS, eMBFuncWriteMultipleHoldingRegister},
#endif
#if MB_FUNC_WRITE_HOLDING_ENABLED > 0
    {MB_FUNC_WRITE_REGISTER, eMBFuncWriteHoldingRegister},
#endif
#if MB_FUNC_READWRITE_HOLDING_ENABLED > 0
    {MB_FUNC_READWRITE_MULTIPLE_REGISTERS, eMBFuncReadWriteMultipleHoldingRegister},
#endif
#if MB_FUNC_READ_COILS_ENABLED > 0
    {MB_FUNC_READ_COILS, eMBFuncReadCoils},
#endif
#if MB_FUNC_WRITE_COIL_ENABLED > 0
    {MB_FUNC_WRITE_SINGLE_COIL, eMBFuncWriteCoil},
#endif
#if MB_FUNC_WRITE_MULTIPLE_COILS_ENABLED > 0
    {MB_FUNC_WRITE_MULTIPLE_COILS, eMBFuncWriteMultipleCoils},
#endif
#if MB_FUNC_READ_DISCRETE_INPUTS_ENABLED > 0
    {MB_FUNC_READ_DISCRETE_INPUTS, eMBFuncReadDiscreteInputs},
#endif
};
static pxMBFunctionHandler pxIllegalFuncHandler;

/* ----------------------- Start implementation -----------------------------*/

/**
 * Initialize ModBus driver
 *
 * @param eMode
 * @param ucSlaveAddress
 * @param ucPort
 * @param ulBaudRate
 * @param eParity
 * @return
 */
eMBErrorCode eMBInit(eMBMode eMode, UCHAR ucSlaveAddress, UCHAR ucPort, ULONG ulBaudRate,
        eMBParity eParity)
{
    eMBErrorCode eStatus = MB_ENOERR;
    switch (eMode)
    {
#if MB_RTU_ENABLED > 0
    case MB_RTU:
        pvMBFrameStartCur = eMBRTUStart;
        pvMBFrameStopCur = eMBRTUStop;
        peMBFrameSendCur = eMBRTUSend;
        peMBFrameReceiveCur = eMBRTUReceive;
        pvMBFrameCloseCur = MB_PORT_HAS_CLOSE ? vMBPortClose : NULL;
        pxMBFrameCBByteReceived = xMBRTUReceiveFSM;
        pxMBFrameCBTransmitterEmpty = xMBRTUTransmitFSM;
        pxMBPortCBTimerExpired = xMBRTUTimerT35Expired;
        pxMBPortCBMasterTimerExpired = xMBRTUMasterTimerExpired;

        eStatus = eMBRTUInit(ucSlaveAddress, ucPort, ulBaudRate, eParity);
        break;
#endif
#if MB_ASCII_ENABLED > 0
    case MB_ASCII:
        pvMBFrameStartCur = eMBASCIIStart;
        pvMBFrameStopCur = eMBASCIIStop;
        peMBFrameSendCur = eMBASCIISend;
        peMBFrameReceiveCur = eMBASCIIReceive;
        pvMBFrameCloseCur = MB_PORT_HAS_CLOSE ? vMBPortClose : NULL;
        pxMBFrameCBByteReceived = xMBASCIIReceiveFSM;
        pxMBFrameCBTransmitterEmpty = xMBASCIITransmitFSM;
        pxMBPortCBTimerExpired = xMBASCIITimerT1SExpired;

        eStatus = eMBASCIIInit( ucSlaveAddress, ucPort, ulBaudRate, eParity );
        break;
#endif
    default:
        eStatus = MB_EINVAL;
        break;
    }

    if (eStatus == MB_ENOERR)
    {
        if (!xMBPortEventInit())
        {
            /* port dependent event module initalization failed. */
            eStatus = MB_EPORTERR;
        }
        else
        {
            eMBCurrentMode = eMode;
            eMBState = STATE_DISABLED;
        }
    }

    return eStatus;
}

#if MB_TCP_ENABLED > 0
eMBErrorCode
eMBTCPInit( USHORT ucTCPPort )
{
    eMBErrorCode    eStatus = MB_ENOERR;

    if( ( eStatus = eMBTCPDoInit( ucTCPPort ) ) != MB_ENOERR )
    {
        eMBState = STATE_DISABLED;
    }
    else if( !xMBPortEventInit(  ) )
    {
        /* Port dependent event module initalization failed. */
        eStatus = MB_EPORTERR;
    }
    else
    {
        pvMBFrameStartCur = eMBTCPStart;
        pvMBFrameStopCur = eMBTCPStop;
        peMBFrameReceiveCur = eMBTCPReceive;
        peMBFrameSendCur = eMBTCPSend;
        pvMBFrameCloseCur = MB_PORT_HAS_CLOSE ? vMBTCPPortClose : NULL;
        ucMBAddress = MB_TCP_PSEUDO_ADDRESS;
        eMBCurrentMode = MB_TCP;
        eMBState = STATE_DISABLED;
    }
    return eStatus;
}
#endif

/**
 * Register a callback handler for the given function code
 * @param ucFunctionCode - Function code to register handler too
 * @param pxHandler - Pointer to callback handler
 * @return
 */
eMBErrorCode eMBRegisterCB(UCHAR ucFunctionCode, pxMBFunctionHandler pxHandler)
{
    int             i;
    eMBErrorCode    eStatus;

    if( ( 0 < ucFunctionCode ) && ( ucFunctionCode <= 127 ) )
    {
        ENTER_CRITICAL_SECTION(  );
        if( pxHandler != NULL )
        {
            for( i = 0; i < MB_FUNC_HANDLERS_MAX; i++ )
            {
                if( ( xFuncHandlers[i].pxHandler == NULL ) ||
                    ( xFuncHandlers[i].pxHandler == pxHandler ) )
                {
                    xFuncHandlers[i].ucFunctionCode = ucFunctionCode;
                    xFuncHandlers[i].pxHandler = pxHandler;
                    break;
                }
            }
            eStatus = ( i != MB_FUNC_HANDLERS_MAX ) ? MB_ENOERR : MB_ENORES;
        }
        else
        {
            for( i = 0; i < MB_FUNC_HANDLERS_MAX; i++ )
            {
                if( xFuncHandlers[i].ucFunctionCode == ucFunctionCode )
                {
                    xFuncHandlers[i].ucFunctionCode = 0;
                    xFuncHandlers[i].pxHandler = NULL;
                    break;
                }
            }
            /* Remove can't fail. */
            eStatus = MB_ENOERR;
        }
        EXIT_CRITICAL_SECTION(  );
    }
    else
    {
        eStatus = MB_EINVAL;
    }
    return eStatus;
}

/**
 * Register callback function when an illegal function number is received.
 *
 * @param pxHandler - Pointer to callback function
 * @return MB_NOERR
 */
eMBErrorCode eMBRegisterIllegalFuncCB(BOOL (*pxHandler)(void))
{
    pxIllegalFuncHandler = pxHandler;
    return MB_ENOERR;
}

/**
 * Close ModBus driver.
 * @return
 */
eMBErrorCode eMBClose(void)
{
    eMBErrorCode    eStatus = MB_ENOERR;

    if( eMBState == STATE_DISABLED )
    {
        if( pvMBFrameCloseCur != NULL )
        {
            pvMBFrameCloseCur(  );
        }
    }
    else
    {
        eStatus = MB_EILLSTATE;
    }
    return eStatus;
}

/**
 * Enable ModBus driver
 * @return
 */
eMBErrorCode eMBEnable(void)
{
    eMBErrorCode    eStatus = MB_ENOERR;

    if( eMBState == STATE_DISABLED )
    {
        /* Activate the protocol stack. */
        pvMBFrameStartCur(  );
        eMBState = STATE_ENABLED;
    }
    else
    {
        eStatus = MB_EILLSTATE;
    }
    return eStatus;
}

/**
 * Disable ModBus driver
 * @return
 */
eMBErrorCode eMBDisable(void)
{
    eMBErrorCode    eStatus;

    if( eMBState == STATE_ENABLED )
    {
        pvMBFrameStopCur(  );
        eMBState = STATE_DISABLED;
        eStatus = MB_ENOERR;
    }
    else if( eMBState == STATE_DISABLED )
    {
        eStatus = MB_ENOERR;
    }
    else
    {
        eStatus = MB_EILLSTATE;
    }
    return eStatus;
}

/**
 * Poll ModBus
 * @return
 */
eMBErrorCode eMBPoll(void)
{
    static UCHAR   *ucMBFrame;
    static UCHAR    ucRcvAddress;
    static UCHAR    ucFunctionCode;
    static USHORT   usLength;
    static eMBException eException;

    int             i;
    eMBErrorCode    eStatus = MB_ENOERR;
    eMBEventType    eEvent;

    /* Check if the protocol stack is ready. */
    if( eMBState != STATE_ENABLED )
    {
        return MB_EILLSTATE;
    }

    /* Check if there is a event available. If not return control to caller.
     * Otherwise we will handle the event. */
    if( xMBPortEventGet( &eEvent ) == TRUE )
    {
        switch ( eEvent )
        {
        case EV_READY:
            DEBUG_PUTSTRING("EV_READY");
            break;

        case EV_FRAME_RECEIVED:
            DEBUG_PUTSTRING("EV_FRAME_RECEIVED");
            vMBPortMasterTimerDisable();

            eStatus = peMBFrameReceiveCur( &ucRcvAddress, &ucMBFrame, &usLength );
            if( eStatus == MB_ENOERR )
            {
                DEBUG_PUTSTRING("Rx Frame");
                /* Receive all frames */
                ( void )xMBPortEventPost( EV_EXECUTE );
            }
            else
            {
                DEBUG_PUTSTRING1("Rx Error: ",eStatus);
            }
            break;

        case EV_EXECUTE:
            DEBUG_PUTSTRING("EV_EXECUTE");
            ucFunctionCode = ucMBFrame[MB_PDU_FUNC_OFF];
            DEBUG_PUTSTRING1("Rx Execute: ", ucFunctionCode);
            eException = MB_EX_ILLEGAL_FUNCTION;
            for( i = 0; i < MB_FUNC_HANDLERS_MAX; i++ )
            {
                /* No more function handlers registered. Abort. */
                if( xFuncHandlers[i].ucFunctionCode == 0 )
                {
                    DEBUG_PUTSTRING1("IllegCmd: ", ucFunctionCode);
                    eException = pxIllegalFuncHandler( ucMBFrame, &usLength );
                    break;
                }
                else if( xFuncHandlers[i].ucFunctionCode == ucFunctionCode )
                {
                    eException = xFuncHandlers[i].pxHandler( ucMBFrame, &usLength );
                    break;
                }
            }
            break;

        case EV_FRAME_SENT:
            DEBUG_PUTSTRING("EV_FRAME_SENT");
            vMBPortMasterTimerEnable( 5000 );
            break;
        }
    }
    return MB_ENOERR;
}

/**
 * Send a frame.
 * @param ucFrame - pointer to frame including address
 * @param usLength - length of frame including address but not CRC
 * @return
 */
eMBErrorCode eMBSendFrame(UCHAR *ucMBFrame, USHORT usLength)
{
    ULONG timeout = 1000;
    DEBUG_PUTSTRING1("Send Frame len=", usLength);
    while (((peMBFrameSendCur(ucMBFrame[0], &ucMBFrame[1], usLength - 1)
            != MB_ENOERR) && ((--timeout) > 0)))
    {
        usleep(5000);
    }
    DEBUG_PUTSTRING("Done");
    return MB_ENOERR;
}
