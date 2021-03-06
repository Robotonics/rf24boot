/*
 *  Universal rf24boot bootloader : uISP App code
 *  Copyright (C) 2014  Andrew 'Necromant' Andrianov <andrew@ncrmnt.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <arch/antares.h>
#include <avr/boot.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <generated/usbconfig.h>
#include <arch/vusb/usbportability.h>
#include <arch/vusb/usbdrv.h>
#include <lib/RF24.h>
#include <string.h>
#include <arch/delay.h>

#include "requests.h"

char msg[128];
#define dbg(fmt, ...) sprintf(msg, fmt, ##__VA_ARGS__)

extern struct rf24 *g_radio;
static uchar last_rq;
static int rq_len;

uint8_t local_addr[5];
uint8_t remote_addr[5];
static uint16_t pos; /* Position in the buffer */
static uint8_t have_moar = 0;

uchar   usbFunctionSetup(uchar data[8])
{
	usbRequest_t    *rq = (void *)data;
	switch (rq->bRequest) 
	{
	case RQ_SET_CONFIG:
		rf24_init(g_radio);
		rf24_enable_dynamic_payloads(g_radio);
		rf24_set_channel(g_radio,   rq->wValue.bytes[0]);
		rf24_set_data_rate(g_radio, rq->wValue.bytes[1]);
		rf24_set_pa_level(g_radio,  rq->wIndex.bytes[0]);
		break;
	case RQ_OPEN_PIPES:
		
		break;
	case RQ_LISTEN:
		if (rq->wValue.bytes[0]) {
			rf24_open_reading_pipe(g_radio, 1, local_addr);
			rf24_start_listening(g_radio);
		} else {
			rf24_stop_listening(g_radio);
			rf24_open_writing_pipe(g_radio, remote_addr);
		}
		break;
	case RQ_READ:
	{
		uint8_t pipe;
		uint8_t retries=50;
		uint8_t len =0; 
		do { 
			if (rf24_available(g_radio, &pipe)) {
				PORTC ^= 1<<2;
				len = rf24_get_dynamic_payload_size(g_radio);
				have_moar = !rf24_read(g_radio, msg, len);
				usbMsgPtr = msg;
				return len;
			}
			delay_ms(10);
		} while (retries--);
		
	}
		return 0;	
		break;
	case RQ_SWEEP: 
	{
		rf24_init(g_radio);
		struct rf24_sweeper s;
		rf24_set_data_rate(g_radio, RF24_2MBPS);
		rf24_set_pa_level(g_radio,  RF24_PA_MAX);
		rf24_sweeper_init(&s, g_radio);
		rf24_sweep(&s, rq->wValue.bytes[0]);
		memcpy(msg, s.values, 128);
		usbMsgPtr = msg;
		return 128;
		break;
	}
	case RQ_NOP:
		/* Shit out dbg buffer */
		usbMsgPtr = msg;
		return strlen(msg)+1;
		break;
	}
	last_rq = rq->bRequest;
	rq_len = rq->wLength.word;
	pos = 0;
	return USB_NO_MSG;		

}

uchar usbFunctionWrite(uchar *data, uchar len)
{
	memcpy(&msg[pos], data, len);
	pos+=len;
	if (pos != rq_len) 
		return 0; /* Need moar data */
	uint8_t ret = 0;
	uint8_t retries = 15;
	switch (last_rq) 
	{
	case RQ_SET_REMOTE_ADDR:
		memcpy(remote_addr, msg, 5);
		break;
	case RQ_SET_LOCAL_ADDR:
		memcpy(local_addr, msg, 5);
		break;
	case RQ_WRITE:
		do { 
			rf24_power_up(g_radio);
			rf24_open_writing_pipe(g_radio, remote_addr);	       
			ret = rf24_write(g_radio, msg, pos);
			PORTC ^= 1<<2;
			delay_ms(5);
		} while( ret && retries--);
		break;
	}
	strcpy(msg, ((ret==0) && retries) ? "OK" : "FAIL");
	return 1;
}

inline void usbReconnect()
{
	DDRD=0xff;
	_delay_ms(250);
	DDRD=0;
}

ANTARES_INIT_LOW(io_init)
{
	DDRC=1<<2;
	PORTC=0xff;
 	usbReconnect();
}

ANTARES_INIT_HIGH(usb_init) 
{
	rf24_init(g_radio);
	rf24_enable_dynamic_payloads(g_radio);
	rf24_set_retries(g_radio, 15, 15);
  	usbInit();
}


ANTARES_APP(usb_app)
{
	usbPoll();
}
