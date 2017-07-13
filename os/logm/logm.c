/****************************************************************************
 *
 * Copyright 2016-2017 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <arch/irq.h>
#include <tinyara/config.h>
#include <tinyara/logm.h>
#include <tinyara/streams.h>
#ifdef CONFIG_LOGM_TIMESTAMP
#include <tinyara/clock.h>
#endif
#include "logm.h"

int g_logm_head;
int g_logm_tail;
int g_logm_dropmsg_count;
int g_logm_overflow_offset;

static void logm_putc(FAR struct lib_outstream_s *this, int ch)
{
	if ((g_logm_tail + this->nput + 1) % logm_bufsize != g_logm_head) {
		g_logm_rsvbuf[(g_logm_tail + this->nput++) % logm_bufsize] = ch;
	}
}

static void logm_outstream(FAR struct lib_outstream_s *outstream)
{
	outstream->put = logm_putc;
#ifdef CONFIG_STDIO_LINEBUFFER
	outstream->flush = lib_noflush;
#endif
	outstream->nput = 0;
}

/* logm_internal hook for syslog & printfs */
int logm_internal(int priority, const char *fmt, va_list ap)
{
	irqstate_t flags;
	int ret = 0;
	struct lib_outstream_s strm;
#ifdef CONFIG_LOGM_TIMESTAMP
	struct timespec ts;
#endif

	if (LOGM_STATUS(LOGM_READY) && !LOGM_STATUS(LOGM_BUFFER_RESIZE_REQ) && !up_interrupt_context()) {
		flags = irqsave();

		if (LOGM_STATUS(LOGM_BUFFER_OVERFLOW)) {
			g_logm_dropmsg_count++;
			irqrestore(flags);
			return 0;
		}

		/*  Initializes a stream for use with logm buffer */
		logm_outstream(&strm);

#ifdef CONFIG_LOGM_TIMESTAMP
		/* Get the current time and prepend timestamp to message */
		if (clock_systimespec(&ts) == OK) {
			(void)lib_sprintf((FAR struct lib_outstream_s *)&strm, "[%4d.%4d] ", ts.tv_sec, ts.tv_nsec / 100000);
		}
#endif
		ret = lib_vsprintf(&strm, fmt, ap);
		g_logm_tail = (g_logm_tail + ret) % logm_bufsize;

		if ((g_logm_tail + 1) % logm_bufsize == g_logm_head) {
			LOGM_STATUS_SET(LOGM_BUFFER_OVERFLOW);
			g_logm_dropmsg_count = 1;
			g_logm_overflow_offset = g_logm_tail;
		}

		irqrestore(flags);
	} else {
		/* Low Output: Sytem is not yet completely ready or this is called from interrupt handler */
#ifdef CONFIG_ARCH_LOWPUTC
		lib_lowoutstream(&strm);
		ret = lib_vsprintf(&strm, fmt, ap);
#endif
	}

	return ret;
}

/* logm api */
int logm(int flag, int indx, int priority, const char *fmt, ...)
{
	va_list ap;
	int ret;

	/* LOGIC for initial test here */

	va_start(ap, fmt);
	ret = logm_internal(priority, fmt, ap);
	va_end(ap);

	return ret;
}
