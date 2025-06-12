/*
 * Copyright (C) 2014 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>

#include "timex.h"
#include "ztimer.h"
#include "xtimer.h"

uint32_t xorNbytes(uint32_t bytes)
{
	volatile uint8_t a = 0xfb;
	uint32_t t0 = ztimer_now(ZTIMER_USEC);
	for (uint32_t i = 0; i < bytes; i++)
	{
		volatile uint8_t b = a ^ a;
	}
	uint32_t t1 = ztimer_now(ZTIMER_USEC);
	printf("xor %ld bytes in %ld usecs\n", bytes, t1-t0);
	return t1-t0;
}

uint32_t xor8bits(void)
{
	volatile uint8_t a = 0xde;
	volatile uint8_t b = 0xad;
	uint32_t t0 = ztimer_now(ZTIMER_USEC);
	volatile uint8_t c = a^b;
	uint32_t t1 = ztimer_now(ZTIMER_USEC);
	printf("xor %x ^ %x = %x in %ld usecs\n", a, b, c, t1-t0);
	return t1-t0;
}

uint32_t xor16bits(void)
{
	volatile uint16_t a = 0xdead;
	volatile uint16_t b = 0xbeef;
	uint32_t t0 = ztimer_now(ZTIMER_USEC);
	volatile uint16_t c = a^b;
	uint32_t t1 = ztimer_now(ZTIMER_USEC);
	printf("xor %x ^ %x = %x in %ld usecs\n", a, b, c, t1-t0);
	return t1-t0;
}

uint32_t xor32bits(void)
{
	volatile uint32_t a = 0xdeadbeef;
	volatile uint32_t b = 0xbeefdead;
	uint32_t t0 = ztimer_now(ZTIMER_USEC);
	volatile uint32_t c = a^b;
	uint32_t t1 = ztimer_now(ZTIMER_USEC);
	printf("xor %x ^ %x = %x in %ld usecs\n", a, b, c, t1-t0);
	return t1-t0;
}

int main(void)
{
	xor8bits();
	xor16bits();
	xor32bits();
	xorNbytes(128);
	xorNbytes(256);
	xorNbytes(512);
	xorNbytes(1024);
	xorNbytes(1024 * 8);
	xorNbytes(1024 * 16);
	printf("Done!\n");
	for (;;){}
	return 0;
}
