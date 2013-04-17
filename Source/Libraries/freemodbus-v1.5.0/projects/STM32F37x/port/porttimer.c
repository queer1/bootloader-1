/*
 * FreeModbus Libary: STM32F37x Port
 * Copyright (C) 2013 Jerome Gobuyan <jerome.gobuyan@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File: $Id: porttimer.c,v 1.1 2006/08/22 21:35:13 wolti Exp $
 */

/* ----------------------- Platform includes --------------------------------*/
#include "port.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"
#include "stm32f37x.h"
extern void USART_RxTimeoutInterruptCmd( FunctionalState newstate );

/* ----------------------- static functions ---------------------------------*/

/* ----------------------- Start implementation -----------------------------*/
/**
 * Timer initialization
 * The STM32F37x has an idle timeout feature that can be used instead of
 * a timer. This function should not be called.
 *
 * @param usTim1Timerout50us
 * @return
 */
BOOL
xMBPortTimersInit( USHORT usTim1Timerout50us )
{
    (void)usTim1Timerout50us;
    return FALSE;
}


inline void
vMBPortTimersEnable(  )
{
    /* Enable the timer with the timeout passed to xMBPortTimersInit( ) */
    USART_RxTimeoutInterruptCmd(ENABLE);
}

inline void
vMBPortTimersDisable(  )
{
    /* Disable any pending timers. */
    USART_RxTimeoutInterruptCmd(DISABLE);
}

/* Create an ISR which is called whenever the timer has expired. This function
 * must then call pxMBPortCBTimerExpired( ) to notify the protocol stack that
 * the timer has expired.
 */
void prvvTIMERExpiredISR( void )
{
    ( void )pxMBPortCBTimerExpired(  );
}


