/*
  This file is part of the DSP-Crowd project
  https://www.dsp-crowd.com

  Author(s):
      - Johannes Natter, office@dsp-crowd.com

  File created on 21.05.2019

  Copyright (C) 2019, Johannes Natter

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

#ifndef TCP_LISTENING_H
#define TCP_LISTENING_H

#include <string>
#include <list>

#ifdef _WIN32
// https://learn.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt?view=msvc-170
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT_WIN10
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN10_19H1
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#include "Processing.h"
#include "Pipe.h"

/* Literature
 * - https://handsonnetworkprogramming.com/articles/differences-windows-winsock-linux-unix-bsd-sockets-compatibility/
 * - https://handsonnetworkprogramming.com/articles/socket-function-return-value-windows-linux-macos/
 */
#ifndef _WIN32
#ifndef SOCKET
#define SOCKET int
#define INVALID_SOCKET -1
#endif
#endif

class TcpListening : public Processing
{

public:

	static TcpListening *create()
	{
		return new (std::nothrow) TcpListening;
	}

	void portSet(uint16_t port, bool localOnly = false);
	void maxConnQueuedSet(size_t maxConn);

	SOCKET nextPeerFd();
	Pipe<SOCKET> ppPeerFd;

protected:

	virtual ~TcpListening() {}

private:

	TcpListening();
	TcpListening(const TcpListening &)
		: Processing("")
		, mPort(0)
		, mLocalOnly(false)
		, mMaxConn(0)
		, mInterrupted(false)
		, mCntSkip(0)
		, mFdLstIPv4(INVALID_SOCKET)
#if CONFIG_PROC_IPV6_ENABLED
		, mFdLstIPv6(INVALID_SOCKET)
#endif
		, mAddrIPv4("")
#if CONFIG_PROC_IPV6_ENABLED
		, mAddrIPv6("")
#endif
		, mConnCreated(0)
	{
		mState = 0;
	}
	TcpListening &operator=(const TcpListening &)
	{
		mPort = 0;
		mLocalOnly = false;
		mMaxConn = 0;
		mInterrupted = false;
		mCntSkip = 0;
		mFdLstIPv4 = INVALID_SOCKET;
#if CONFIG_PROC_IPV6_ENABLED
		mFdLstIPv6 = INVALID_SOCKET;
#endif
		mAddrIPv4 = "";
#if CONFIG_PROC_IPV6_ENABLED
		mAddrIPv6 = "";
#endif
		mConnCreated = 0;

		mState = 0;

		return *this;
	}

	Success process();
	Success shutdown();

	Success socketCreate(bool isIPv6, SOCKET &fdLst, std::string &strAddr);
	Success connectionsAccept(SOCKET &fdLst);
	void socketClose(SOCKET &fd);

	int errGet();
	std::string errnoToStr(int num);
	bool fileNonBlockingSet(SOCKET fd);
	void processInfo(char *pBuf, char *pBufEnd);

	uint16_t mPort;
	bool mLocalOnly;
	size_t mMaxConn;
	bool mInterrupted;
	uint32_t mCntSkip;

	SOCKET mFdLstIPv4;
#if CONFIG_PROC_IPV6_ENABLED
	SOCKET mFdLstIPv6;
#endif
	std::string mAddress;
	std::string mAddrIPv4;
#if CONFIG_PROC_IPV6_ENABLED
	std::string mAddrIPv6;
#endif

	// statistics
	uint32_t mConnCreated;

#if CONFIG_PROC_HAVE_DRIVERS
	static std::mutex mtxStrerror;
#endif
};

#endif

