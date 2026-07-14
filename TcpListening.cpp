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

#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#if defined(__FreeBSD__)
#include <netinet/in.h>
#endif

#include "TcpListening.h"
/* Following include because of
 * - wsaInit() on Windows
 * - sockaddrInfoGet()
 */
#include "TcpTransfering.h"

#define dForEach_ProcState(gen) \
		gen(StStart) \
		gen(StMain) \
		gen(StTmp) \

#define dGenProcStateEnum(s) s,
dProcessStateEnum(ProcState);

#if 0
#define dGenProcStateString(s) #s,
dProcessStateStr(ProcState);
#endif

using namespace std;

#if !defined(_WIN32)
#if CONFIG_PROC_HAVE_DRIVERS
mutex TcpListening::mtxStrerror;
#endif
#endif

#define dCntSkipMax 30

TcpListening::TcpListening()
	: Processing("TcpListening")
	, mPort(0)
	, mLocalOnly(false)
	, mMaxConn(200)
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
	mState = StStart;
}

void TcpListening::portSet(uint16_t port, bool localOnly)
{
	mPort = port;
	mLocalOnly = localOnly;
}

void TcpListening::maxConnQueuedSet(size_t maxConn)
{
	mMaxConn = maxConn;
}

SOCKET TcpListening::nextPeerFd()
{
	PipeEntry<SOCKET> peerFdEntry;

	if (ppPeerFd.get(peerFdEntry) < 1)
		return INVALID_SOCKET;

	return peerFdEntry.particle;
}

/*
Literature socket programming:
- http://man7.org/linux/man-pages/man2/poll.2.html
- http://man7.org/linux/man-pages/man2/accept.2.html
*/
Success TcpListening::process()
{
	Success success;
#ifdef _WIN32
	bool ok;
#endif
#if 0
	dStateTrace;
#endif
	switch (mState)
	{
	case StStart:

		if (!mPort)
			return procErrLog(-1, "port not set");
#ifdef _WIN32
		ok = TcpTransfering::wsaInit();
		if (!ok)
			return procErrLog(-1, "could not init WSA");
#endif
		//procDbgLog("creating listening sockets");

		success = socketCreate(false, mFdLstIPv4, mAddrIPv4);
		if (success != Positive)
			return procErrLog(-1, "could not create IPv4 socket");

#if CONFIG_PROC_IPV6_ENABLED
		success = socketCreate(true, mFdLstIPv6, mAddrIPv6);
		if (success != Positive)
		{
			procDbgLog("could not create IPv6 socket");
			socketClose(mFdLstIPv6);
		}
#endif
		//procDbgLog("creating listening sockets: done");

		mState = StMain;

		break;
	case StMain:

		++mCntSkip;
		if (mCntSkip < dCntSkipMax)
			return Pending;
		mCntSkip = 0;

		while (1)
		{
			success = connectionsAccept(mFdLstIPv4);
			if (success != Positive)
				break;
		}

		if (success != Pending)
			return success;

#if CONFIG_PROC_IPV6_ENABLED
		while (1)
		{
			success = connectionsAccept(mFdLstIPv6);
			if (success != Positive)
				break;
		}

		if (success != Pending)
			return success;
#endif
		if (mInterrupted)
			return Positive;

		break;
	case StTmp:

		break;
	default:
		break;
	}

	return Pending;
}

/*
Literature socket programming:
- http://man7.org/linux/man-pages/man2/socket.2.html
- http://man7.org/linux/man-pages/man2/setsockopt.2.html
- http://man7.org/linux/man-pages/man2/bind.2.html
- http://man7.org/linux/man-pages/man2/listen.2.html
- https://man7.org/linux/man-pages/man7/ipv6.7.html
- https://docs.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-wsastartup
- https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ipv6-socket-options
*/
Success TcpListening::socketCreate(bool isIPv6, SOCKET &fdLst, string &strAddr)
{
	// create address structure

	struct sockaddr_storage addr;
	socklen_t addrLen;
	uint16_t port;
	bool ok;

	memset(&addr, 0, sizeof(addr));

#if CONFIG_PROC_IPV6_ENABLED
	addr.ss_family = isIPv6 ? AF_INET6 : AF_INET;
#else
	addr.ss_family = AF_INET;
#endif
	if (addr.ss_family == AF_INET)
	{
		struct sockaddr_in *pAddr4 = (struct sockaddr_in *)&addr;

		pAddr4->sin_port = htons(mPort);
		pAddr4->sin_addr.s_addr = mLocalOnly ?
					htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
	}
#if CONFIG_PROC_IPV6_ENABLED
	else
	if (addr.ss_family == AF_INET6)
	{
		struct sockaddr_in6 *pAddr6 = (struct sockaddr_in6 *)&addr;

		pAddr6->sin6_port = htons(mPort);

		memset(&pAddr6->sin6_addr, 0, sizeof(pAddr6->sin6_addr));
		if (mLocalOnly)
			pAddr6->sin6_addr.s6_addr[15] = 1; // ::1
	}
#endif
	else
		return procErrLog(-1, "unknown address family");

	ok = TcpTransfering::sockaddrInfoGet(addr, strAddr, port, isIPv6);
	if (!ok)
		return procErrLog(-1, "could not get socket address info");

	// create and configure socket

	int opt;

	// IMPORTANT
	// No need to close socket in case of error
	// This is done in function shutdown()

	fdLst = ::socket(addr.ss_family, SOCK_STREAM, 0);
	if (fdLst == INVALID_SOCKET)
		return procErrLog(-1, "socket() failed: %s", errnoToStr(errGet()).c_str());

#if CONFIG_PROC_IPV6_ENABLED
	if (addr.ss_family == AF_INET6)
	{
		opt = 1;
		if (::setsockopt(fdLst, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&opt, sizeof(opt)))
			return procErrLog(-1, "setsockopt(IPV6_V6ONLY) failed: %s", errnoToStr(errGet()).c_str());
	}
#endif
	opt = 1;
	if (::setsockopt(fdLst, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt)))
		return procErrLog(-1, "setsockopt(SO_REUSEADDR) failed: %s", errnoToStr(errGet()).c_str());

	ok = fileNonBlockingSet(fdLst);
	if (!ok)
		return procErrLog(-1, "could not set non blocking mode: %s",
							errnoToStr(errGet()).c_str());

	// bind and listen

	// Important for MacOS
	// Literature
	// - https://stackoverflow.com/questions/73707162/socket-bind-failed-with-invalid-argument-error-for-program-running-on-macos
	// Thx to Bananabaer!
#if CONFIG_PROC_IPV6_ENABLED
	if (addr.ss_family == AF_INET)
		addrLen = sizeof(sockaddr_in);
	else
		addrLen = sizeof(sockaddr_in6);
#else
	addrLen = sizeof(sockaddr_in);
#endif
	// Important for MacOS: End

	if (::bind(fdLst, (struct sockaddr *)&addr, addrLen) < 0)
	{
		if (!isIPv6)
			procErrLog(-1, "bind(%u) failed: %s", mPort, errnoToStr(errGet()).c_str());
		return -1;
	}

	if (::listen(fdLst, 8192) < 0)
		return procErrLog(-1, "listen() failed: %s", errnoToStr(errGet()).c_str());

	return Positive;
}

Success TcpListening::connectionsAccept(SOCKET &fdLst)
{
	if (fdLst == INVALID_SOCKET)
		return Pending;

	SOCKET peerSocketFd;
	struct sockaddr_storage addr;
	socklen_t addrLen;
	string strAddr;
	uint16_t numPort;
	bool isIPv6, ok;
	int numErr;
	int res;

	peerSocketFd = ::accept(fdLst, NULL, NULL);
	if (peerSocketFd == INVALID_SOCKET)
	{
		numErr = errGet();
#ifdef _WIN32
		if (numErr == WSAEWOULDBLOCK || numErr == WSAEINPROGRESS)
			return Pending;
#else
		if (numErr == EWOULDBLOCK ||
			numErr == EALREADY ||
			numErr == EINPROGRESS ||
			numErr == EAGAIN)
			return Pending;
#endif
		procWrnLog("accept() failed: %s (%d)", errnoToStr(numErr).c_str(), numErr);
		return Pending;
	}

	memset(&addr, 0, sizeof(addr));
	addrLen = sizeof(addr);

	res = ::getpeername(peerSocketFd, (struct sockaddr *)&addr, &addrLen);
	if (!res)
	{
		ok = TcpTransfering::sockaddrInfoGet(addr, strAddr, numPort, isIPv6);
		if (!ok)
			return procErrLog(-1, "could not get socket address info");

		procDbgLog("got peer %s%s%s:%u",
				isIPv6 ? "[" : "",
				strAddr.c_str(),
				isIPv6 ? "]" : "",
				numPort);
	}

	if (ppPeerFd.isFull() || ppPeerFd.size() >= mMaxConn)
	{
		procWrnLog("dropping connection. Output queue full");
#ifdef _WIN32
		::closesocket(peerSocketFd);
#else
		::close(peerSocketFd);
#endif
		// give internal side of system time
		// to consume queue -> Pending
		return Pending;
	}

	ppPeerFd.commit(peerSocketFd, nowMs());
	++mConnCreated;

	return Positive;
}

Success TcpListening::shutdown()
{
	PipeEntry<SOCKET> peerFd;

	while (ppPeerFd.get(peerFd) > 0)
		socketClose(peerFd.particle);

	socketClose(mFdLstIPv4);
#if CONFIG_PROC_IPV6_ENABLED
	socketClose(mFdLstIPv6);
#endif
	return Positive;
}

void TcpListening::socketClose(SOCKET &fd)
{
	if (fd == INVALID_SOCKET)
		return;

	//procDbgLog("closing socket %d", fd);
#ifdef _WIN32
	::closesocket(fd);
#else
	::close(fd);
#endif
	fd = INVALID_SOCKET;
	//procDbgLog("closing socket %d: done", fd);
}

int TcpListening::errGet()
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

string TcpListening::errnoToStr(int num)
{
#if defined(_WIN32)
	char buf[64];
	errno_t numErr = ::strerror_s(buf, sizeof(buf), num);
	(void)numErr;
	return string(buf);
#else
#if CONFIG_PROC_HAVE_DRIVERS
	Guard lock(mtxStrerror);
#endif
	return string(::strerror(num));
#endif
}

bool TcpListening::fileNonBlockingSet(SOCKET fd)
{
	int opt;
#ifdef _WIN32
	unsigned long nonBlockMode = 1;

	opt = ioctlsocket(fd, FIONBIO, &nonBlockMode);
	if (opt == SOCKET_ERROR)
		return false;
#else
	int nonBlockMode = 1;

	opt = ::ioctl(fd, FIONBIO, &nonBlockMode);
	if (opt == -1)
		return false;
#endif
	return true;
}

/* Literature
 * - https://linux.die.net/man/3/inet_ntoa
 * - https://man7.org/linux/man-pages/man3/inet_ntop.3.html
 * - https://linux.die.net/man/3/htons
 */
void TcpListening::processInfo(char *pBuf, char *pBufEnd)
{
	//dInfo("State\t\t\t%s\n", ProcStateString[mState]);

	bool hasIPv4 = mAddrIPv4.size();

	if (hasIPv4)
		dInfo("%s:%d", mAddrIPv4.c_str(), mPort);

#if CONFIG_PROC_IPV6_ENABLED
	if (mAddrIPv6.size())
	{
		if (hasIPv4) dInfo(", ");
		dInfo("[%s]:%d", mAddrIPv6.c_str(), mPort);
	}
#endif
	dInfo("\n");

	dInfo("Connections created\t%d\n", (int)mConnCreated);
	dInfo("Queue\t\t\t%zu\n", ppPeerFd.size());
}

