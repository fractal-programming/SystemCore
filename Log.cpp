/*
  This file is part of the DSP-Crowd project
  https://www.dsp-crowd.com

  Author(s):
      - Johannes Natter, office@dsp-crowd.com

  File created on 19.03.2021

  Copyright (C) 2021, Johannes Natter

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "ProcConf.h"

#define DBG_LOG	0

#if DBG_LOG
#include <string.h>
#endif

#ifdef _MSC_VER
#include <BaseTsd.h>
#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif
#endif

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#if CONFIG_PROC_LOG_HAVE_CHRONO
#include <chrono>
#endif
#if CONFIG_PROC_HAVE_DRIVERS
#include <thread>
#include <mutex>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
#if CONFIG_PROC_LOG_HAVE_CHRONO
using namespace chrono;
#endif

typedef void (*FuncEntryLogCreate)(
			const int severity,
#if CONFIG_PROC_LOG_HAVE_CHRONO
			const char *pTimeAbs,
			const std::chrono::system_clock::time_point &tLogged,
#endif
			const char *pTimeCnt,
			const char *pWhere,
			const char *pSeverity,
			const char *pWhatUser);

typedef uint32_t (*FuncCntTimeCreate)();

static FuncEntryLogCreate pFctEntryLogCreate = NULL;
static FuncCntTimeCreate pFctCntTimeCreate = NULL;
static int widthCntTime = 0;

#if CONFIG_PROC_LOG_HAVE_CHRONO
static system_clock::time_point tLoggedOnConsole;
const int cLogDiffSecMax = 9;
const int cLogDiffMsMax = 999;
#endif

static const char *tabStrSev[] = { "INV", "ERR", "WRN", "INF", "DBG", "COR" };

#if CONFIG_PROC_LOG_HAVE_STDOUT
#ifdef _WIN32
static const WORD tabColors[] =
{
	7, /* default */	4, /* red */		6, /* yellow */
	7, /* default */	3, /* cyan */		5, /* purple */
};
#else
static const char *tabColors[] =
{
	"\033[39m",   /* default */	"\033[0;31m", /* red */		"\033[0;33m", /* yellow */
	"\033[39m",   /* default */	"\033[0;36m", /* cyan */		"\033[0;35m", /* purple */
};
#endif
#endif

#ifdef CONFIG_PROC_LOG_COLOR_INF
#define dColorInfo CONFIG_PROC_LOG_COLOR_INF
#else
#define dColorInfo dColorDefault
#endif
const size_t cLenWherePad = 68;
//const size_t cLogEntryBufferSize = 230;
const size_t cLogEntryBufferSize = 512;

// Example                                                  _ pBufEnd
//                                                        _/
// Allocated buffer     |xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx0|
// Block 1              |<b1>0xxxxxxxxxxxxxxxxxxxxxxxxxxxx0|
// Block 2              |<b1>0<b2>0xxxxxxxxxxxxxxxxxxxxxxx0|
// Block 3 error        |<b1>0<b2>0-                     00|
// Block 4 saturated    |<b1>0<b2>0-                     00|
// Block 5 saturated    |<b1>0<b2>0-                     00|
// Block 4 and 5 have same ptr at the end => pBufEnd

static int levelLog = 3;
#if CONFIG_PROC_HAVE_DRIVERS
static mutex mtxPrint;
#endif

void levelLogSet(int lvl)
{
	levelLog = lvl;
}

int levelLogGet()
{
	return levelLog;
}

void entryLogCreateSet(FuncEntryLogCreate pFct)
{
#if CONFIG_PROC_HAVE_DRIVERS
	lock_guard<mutex> lock(mtxPrint); // Guard not defined!
#endif
	pFctEntryLogCreate = pFct;
}

void cntTimeCreateSet(FuncCntTimeCreate pFct, int width)
{
	if (width < -20 || width > 20)
		return;
#if CONFIG_PROC_HAVE_DRIVERS
	lock_guard<mutex> lock(mtxPrint); // Guard not defined!
#endif
	pFctCntTimeCreate = pFct;
	widthCntTime = width;
}

int16_t entryLogSimpleCreate(
			const int isErr,
			const int16_t code,
			const char *msg, ...)
{
#if CONFIG_PROC_HAVE_DRIVERS
	lock_guard<mutex> lock(mtxPrint); // Guard not defined!
#endif
	FILE *pStream = isErr ? stderr : stdout;
	ssize_t len;
	va_list args;

	va_start(args, msg);
	len = vfprintf(pStream, msg, args);
	if (len < 0)
	{
		va_end(args);
		return code;
	}
	va_end(args);

	fprintf(pStream, "\n");
	fflush(pStream);

	return code;
}

static size_t spaceBufLeft(const char *pBuf, const char *pBufEnd)
{
	return (pBufEnd > pBuf) ? (size_t)(pBufEnd - pBuf) : 0;
}

static int pBufSaturated(ssize_t len, char * &pBuf, const char *pBufEnd)
{
	if (len > pBufEnd - pBuf)
		len = pBufEnd - pBuf;

	pBuf += len;

	int saturated = pBuf == pBufEnd;
#if DBG_LOG
	if (saturated)
		fprintf(stderr, "pBufSaturated()\n");
#endif
	return saturated;
}

static char *strErr(char *pBufStart, const char *pBufEnd)
{
	char *pBuf = pBufStart;
#if DBG_LOG
	fprintf(stderr, "strErr()\n");
#endif
	for (; pBuf < pBufEnd; ++pBuf)
		*pBuf = pBuf == pBufStart ? '-' : ' ';

	if (pBuf > pBufStart)
		*--pBuf = 0;

	return pBuf;
}

#if CONFIG_PROC_LOG_HAVE_CHRONO
static char *blockTimeAbsAdd(char *pBuf, const char *pBufEnd, system_clock::time_point &tLogged)
{
	char *pBufStart = pBuf;
	ssize_t len;
	size_t res;
#if DBG_LOG
	fprintf(stderr, "# blockTimeAbsAdd()\n");
#endif
	// build day
	time_t tTt = system_clock::to_time_t(tLogged);
	char timeBuf[32];
	tm tTm {};
#ifdef _WIN32
	::localtime_s(&tTm, &tTt);
#else
	::localtime_r(&tTt, &tTm);
#endif
	res = strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d", &tTm);
	if (!res)
		return strErr(pBufStart, pBufEnd);

	// build time
	system_clock::duration dur = tLogged.time_since_epoch();
	milliseconds durMillis(duration_cast<milliseconds>(dur).count() % 1000);

	len = snprintf(pBuf, spaceBufLeft(pBuf, pBufEnd),
					"%s  %02d:%02d:%02d.%03d ",
					timeBuf,
					tTm.tm_hour, tTm.tm_min,
					tTm.tm_sec, (int)durMillis.count());
	if (len < 0)
		return strErr(pBufStart, pBufEnd);
#if DBG_LOG
	fprintf(stderr, "len = %d\n", len);
#endif
	if (pBufSaturated(len, pBuf, pBufEnd))
		return strErr(pBufStart, pBufEnd);

	++pBuf;

	return pBuf;
}

char *blockTimeRelAdd(
		char *pBuf, char *pBufEnd,
		const system_clock::time_point &tNow,
		const system_clock::time_point &tOld)
{
	char *pBufStart = pBuf;
	milliseconds durDiffMs = duration_cast<milliseconds>(tNow - tOld);
	long long tDiff = durDiffMs.count();
	int tDiffSec = int(tDiff / 1000);
	int tDiffMs = int(tDiff % 1000);
	bool diffMaxed = false;
	ssize_t len;
#if DBG_LOG
	fprintf(stderr, "# blockTimeRelAdd()\n");
#endif
	if (tDiffSec > cLogDiffSecMax)
	{
		tDiffSec = cLogDiffSecMax;
		tDiffMs = cLogDiffMsMax;

		diffMaxed = true;
	}

	len = snprintf(pBuf, spaceBufLeft(pBuf, pBufEnd),
					"%c%d.%03d  ",
					diffMaxed ? '>' : '+', tDiffSec, tDiffMs);
	if (len < 0)
		return strErr(pBufStart, pBufEnd);

	if (pBufSaturated(len, pBuf, pBufEnd))
		return strErr(pBufStart, pBufEnd);

	++pBuf;

	return pBuf;
}
#endif
static char *blockTimeCntAdd(char *pBuf, const char *pBufEnd)
{
	char *pBufStart = pBuf;
#if DBG_LOG
	fprintf(stderr, "# blockTimeCntAdd()\n");
#endif
	if (!pFctCntTimeCreate)
	{
		if (pBuf < pBufEnd) *pBuf++ = 0;
#if 0
		return strErr(pBufStart, pBufEnd);
#else
		return pBuf;
#endif
	}

	uint32_t cntTime = pFctCntTimeCreate();
	ssize_t len;

	len = snprintf(pBuf, spaceBufLeft(pBuf, pBufEnd),
					"%*" PRIu32 "  ",
					widthCntTime, cntTime);
	if (len < 0)
		return strErr(pBufStart, pBufEnd);

	if (pBufSaturated(len, pBuf, pBufEnd))
		return strErr(pBufStart, pBufEnd);

	++pBuf;

	return pBuf;
}

static char *blockWhereAdd(
			char *pBuf, const char *pBufEnd,
			const char *pBufPad,
			const void *pProc,
			const char *filename,
			const char *function,
			const int line)
{
	char *pBufStart = pBuf;
	ssize_t len;
#if DBG_LOG
	fprintf(stderr, "# blockWhereAdd() - a\n");
#endif
	len = snprintf(pBuf, spaceBufLeft(pBuf, pBufEnd),
				"%-20s  ", function);
	if (len < 0)
		return strErr(pBufStart, pBufEnd);
#if DBG_LOG
	fprintf(stderr, "# blockWhereAdd() - b\n");
#endif
	(void)pBufSaturated(len, pBuf, pBufEnd);

	if (pProc)
	{
		len = snprintf(pBuf, spaceBufLeft(pBuf, pBufEnd),
				"%p ", pProc);
		if (len < 0)
			return strErr(pBufStart, pBufEnd);
#if DBG_LOG
		fprintf(stderr, "# blockWhereAdd() - c\n");
#endif
		(void)pBufSaturated(len, pBuf, pBufEnd);
	}

	len = snprintf(pBuf, spaceBufLeft(pBuf, pBufEnd),
				"%s:%-4d  ", filename, line);
	if (len < 0)
		return strErr(pBufStart, pBufEnd);
#if DBG_LOG
	fprintf(stderr, "# blockWhereAdd() - d\n");
#endif
	(void)pBufSaturated(len, pBuf, pBufEnd);

	// padding
	for (; pBuf < pBufPad && pBuf < pBufEnd; ++pBuf)
		*pBuf = ' ';

	char *pBufPadded = pBuf;

	if (pBuf > pBufStart)
		*--pBuf = 0;
	if (pBuf > pBufStart)
		*--pBuf = ' ';
	if (pBuf > pBufStart)
		*--pBuf = ' ';

	return pBufPadded;
}

static char *blockSeverityAdd(
			char *pBuf, const char *pBufEnd,
			const int severity)
{
	char *pBufStart = pBuf;
	ssize_t len;
#if DBG_LOG
	fprintf(stderr, "# blockSeverityAdd()\n");
#endif
	len = snprintf(pBuf, spaceBufLeft(pBuf, pBufEnd), "%s  ", tabStrSev[severity]);
	if (len < 0)
		return strErr(pBufStart, pBufEnd);

	if (pBufSaturated(len, pBuf, pBufEnd))
		return strErr(pBufStart, pBufEnd);

	++pBuf;

	return pBuf;
}

static char *blockWhatUserAdd(
			char *pBuf, const char *pBufEnd,
			const char *msg, va_list args)
{
	char *pBufStart = pBuf;
	ssize_t len;
#if DBG_LOG
	fprintf(stderr, "# blockWhatUserAdd()\n");
#endif
	len = vsnprintf(pBuf, spaceBufLeft(pBuf, pBufEnd), msg, args);
	if (len < 0)
		return strErr(pBufStart, pBufEnd);

	(void)pBufSaturated(len, pBuf, pBufEnd);

	++pBuf;

	return pBuf;
}
#if CONFIG_PROC_LOG_HAVE_STDOUT
static void toConsoleWrite(
			const int severity,
#if CONFIG_PROC_LOG_HAVE_CHRONO
			const char *pTimeAbs,
			const char *pTimeRel,
			const system_clock::time_point &tLogged,
#endif
			const char *pTimeCnt,
			const char *pWhere,
			const char *pSeverity,
			const char *pWhatUser)
{
	if (severity > levelLog)
		return;

	FILE *fOut = severity < 3 ? stderr : stdout;
#ifdef _WIN32
	HANDLE hConsole = GetStdHandle(severity < 3 ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO infoConsole;

	GetConsoleScreenBufferInfo(hConsole, &infoConsole);
	WORD colorBkup = infoConsole.wAttributes;

	SetConsoleTextAttribute(hConsole, 8);
	fprintf(fOut,
#if CONFIG_PROC_LOG_HAVE_CHRONO
			"%s%s"
#endif
			"%s%s",
#if CONFIG_PROC_LOG_HAVE_CHRONO
			pTimeAbs, pTimeRel,
#endif
			pTimeCnt, pWhere);

	SetConsoleTextAttribute(hConsole, tabColors[severity]);
	fprintf(fOut, "%s", pSeverity);

	SetConsoleTextAttribute(hConsole, colorBkup);
	fprintf(fOut, "%s\n", pWhatUser);
#else
	fprintf(fOut,
			"\033[90m"
#if CONFIG_PROC_LOG_HAVE_CHRONO
			"%s%s"
#endif
			"%s%s"
			"%s%s"
			"%s%s\n",
#if CONFIG_PROC_LOG_HAVE_CHRONO
			pTimeAbs, pTimeRel,
#endif
			pTimeCnt, pWhere,
			tabColors[severity], pSeverity,
			tabColors[0], pWhatUser);
#endif
	fflush(fOut);
#if CONFIG_PROC_LOG_HAVE_CHRONO
	tLoggedOnConsole = tLogged;
#endif
}
#endif

int16_t entryLogCreate(
			const int severity,
			const void *pProc,
			const char *filename,
			const char *function,
			const int line,
			const int16_t code,
			const char *msg, ...)
{
#if CONFIG_PROC_HAVE_DRIVERS
	lock_guard<mutex> lock(mtxPrint); // Guard not defined!
#endif
	if (severity < 1 || severity > 5)
		return code;

	// ## MALLOC
	char *pBufStart = (char *)malloc(cLogEntryBufferSize);
	if (!pBufStart)
		return code;

	char *pBufEnd = pBufStart + cLogEntryBufferSize - 1;
	*pBufEnd = 0;

	// WHEN
#if CONFIG_PROC_LOG_HAVE_CHRONO
	system_clock::time_point tLogged = system_clock::now();
	char *pTimeAbs = pBufStart;
	char *pTimeRel = blockTimeAbsAdd(pTimeAbs, pBufEnd, tLogged);
	char *pTimeCnt = blockTimeRelAdd(pTimeRel, pBufEnd, tLogged, tLoggedOnConsole);
#else
	char *pTimeCnt = pBufStart;
#endif
	char *pWhere = blockTimeCntAdd(pTimeCnt, pBufEnd);

	// WHERE
	char *pSeverity = blockWhereAdd(
			pWhere, pBufEnd,
			pWhere + cLenWherePad,
			pProc, filename, function, line);

	// WHAT
	char *pWhatUser = blockSeverityAdd(pSeverity, pBufEnd, severity);
	va_list args;
	va_start(args, msg);
	(void)blockWhatUserAdd(pWhatUser, pBufEnd, msg, args);
	va_end(args);

	// +++ Console
#if CONFIG_PROC_LOG_HAVE_STDOUT
	toConsoleWrite(
			severity,
#if CONFIG_PROC_LOG_HAVE_CHRONO
			pTimeAbs,
			pTimeRel,
			tLogged,
#endif
			pTimeCnt,
			pWhere,
			pSeverity,
			pWhatUser);
#endif

	// +++ Listener
	if (pFctEntryLogCreate)
	{
		pFctEntryLogCreate(
			severity,
#if CONFIG_PROC_LOG_HAVE_CHRONO
			pTimeAbs,
			tLogged,
#endif
			pTimeCnt,
			pWhere,
			pSeverity,
			pWhatUser);
	}
#if DBG_LOG
	fprintf(stderr, "pBufStart  = %p,   0,   0,   0\n", pBufStart);
	fprintf(stderr, "pTimeAbs   = %p, %3ld, %3ld, %3ld, '%s'\n", pTimeAbs, pTimeAbs - pBufStart, pTimeAbs - pBufStart, strlen(pTimeAbs), pTimeAbs);
	fprintf(stderr, "pTimeRel   = %p, %3ld, %3ld, %3ld, '%s'\n", pTimeRel, pTimeRel - pBufStart, pTimeRel - pTimeAbs, strlen(pTimeRel), pTimeRel);
	fprintf(stderr, "pTimeCnt   = %p, %3ld, %3ld, %3ld, '%s'\n", pTimeCnt, pTimeCnt - pBufStart, pTimeCnt - pTimeRel, strlen(pTimeCnt), pTimeCnt);
	fprintf(stderr, "pWhere     = %p, %3ld, %3ld, %3ld, '%s'\n", pWhere, pWhere - pBufStart, pWhere - pTimeCnt, strlen(pWhere), pWhere);
	fprintf(stderr, "pSeverity  = %p, %3ld, %3ld, %3ld, '%s'\n", pSeverity, pSeverity - pBufStart, pSeverity - pWhere, strlen(pSeverity), pSeverity);
	fprintf(stderr, "pWhatUser  = %p, %3ld, %3ld, %3ld, '%s'\n", pWhatUser, pWhatUser - pBufStart, pWhatUser - pSeverity, strlen(pWhatUser), pWhatUser);
	fprintf(stderr, "pBufEnd    = %p, %3ld, %3ld, %3ld\n", pBufEnd, pBufEnd - pBufStart, pBufEnd - pWhatUser, strlen(pBufEnd));
#endif
	// ## FREE
	free(pBufStart);
#if DBG_LOG
	exit(1);
#endif
	return code;
}

