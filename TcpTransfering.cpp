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
#include <chrono>
#ifndef _WIN32
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#endif
#if defined(__FreeBSD__)
#include <netinet/in.h>
#endif

#include "TcpTransfering.h"

#define dForEach_ProcState(gen) \
		gen(StSrvStart) \
		gen(StSrvArgCheck) \
		gen(StCltStart) \
		gen(StCltArgCheck) \
		gen(StCltConnDoneWait) \
		gen(StCltConnDone) \
		gen(StConnMain) \
		gen(StTmp) \

#define dGenProcStateEnum(s) s,
dProcessStateEnum(ProcState);

#if 0
#define dGenProcStateString(s) #s,
dProcessStateStr(ProcState);
#endif

using namespace std;
using namespace chrono;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifdef _WIN32
#if CONFIG_PROC_HAVE_DRIVERS
mutex TcpTransfering::mtxGlobalInit;
#endif
bool TcpTransfering::globalInitDone = false;
#endif
#if CONFIG_PROC_HAVE_DRIVERS
mutex TcpTransfering::mtxStrerror;
#endif

#define dTmoDefaultConnDoneMs			2000

/*
 * Literature
 * - https://stackoverflow.com/questions/28027937/cross-platform-sockets
 */
TcpTransfering::TcpTransfering(SOCKET fd)
	: Transfering("TcpTransfering")
	, mStartMs(0)
#if CONFIG_PROC_HAVE_DRIVERS
	, mSocketFdMtx()
#endif
	, mSocketFd(fd)
	, mHostAddrStr("")
	, mHostPort(0)
	, mpHostAddr(NULL)
	, mErrno(0)
	, mInfoSet(false)
#if CONFIG_PROC_IPV6_ENABLED
	, mIsIPv6Local(false)
	, mIsIPv6Remote(false)
#endif
	, mBytesReceived(0)
	, mBytesSent(0)
{
	mState = StSrvStart;
	mSendReady = true;
	addrInfoSet();
}

// strAddrHost can be
// - IPv4
// - IPv6
// - Domain (TODO)
TcpTransfering::TcpTransfering(const string &hostAddr, uint16_t hostPort)
	: Transfering("TcpTransfering")
	, mStartMs(0)
#if CONFIG_PROC_HAVE_DRIVERS
	, mSocketFdMtx()
#endif
	, mSocketFd(INVALID_SOCKET)
	, mHostAddrStr(hostAddr)
	, mHostPort(hostPort)
	, mpHostAddr(NULL)
	, mErrno(0)
	, mInfoSet(false)
#if CONFIG_PROC_IPV6_ENABLED
	, mIsIPv6Local(false)
	, mIsIPv6Remote(false)
#endif
	, mBytesReceived(0)
	, mBytesSent(0)
{
	mState = StCltStart;
	mSendReady = false;
}

/*
 * Literature
 * - https://www.geeksforgeeks.org/tcp-server-client-implementation-in-c/
 * - https://man7.org/linux/man-pages/man3/inet_pton.3.html
 * - https://man7.org/linux/man-pages/man2/socket.2.html
 * - https://man7.org/linux/man-pages/man2/connect.2.html
 * - https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-connect
 */
Success TcpTransfering::process()
{
	uint32_t curTimeMs = millis();
	uint32_t diffMs = curTimeMs - mStartMs;
	Success success;
	int res, numErr = 0;
	ssize_t connCheck;
#ifdef _WIN32
	bool ok;
#endif
#if 0
	dStateTrace;
#endif
	switch (mState)
	{
	case StSrvStart:

		mState = StSrvArgCheck;

		break;
	case StSrvArgCheck:

		if (mSocketFd == INVALID_SOCKET)
			return procErrLog(-1, "socket file descriptor not set");

		success = socketOptionsSet();
		if (success != Positive)
			return procErrLog(-1, "could not set socket options");

		mState = StConnMain;

		break;
	case StCltStart:

#ifdef _WIN32
		ok = wsaInit();
		if (!ok)
			return procErrLog(-2, "could not init WSA");
#endif
		mState = StCltArgCheck;

		break;
	case StCltArgCheck:

		// create local address structure

		if (mHostAddrStr == "localhost")
			mHostAddrStr = "127.0.0.1";

		mpHostAddr = addrStringToSock(mHostAddrStr, mHostPort);
		if (!mpHostAddr)
			return procErrLog(-1, "could not parse IP address. Given: '%s'",
					mHostAddrStr.c_str());

		// create and configure socket

		mSocketFd = socket(mpHostAddr->ss_family, SOCK_STREAM, 0);
		if (mSocketFd == INVALID_SOCKET)
			return procErrLog(-1, "could not create socket: %s",
							errnoToStr(errGet()).c_str());

		success = socketOptionsSet();
		if (success != Positive)
			return procErrLog(-1, "could not set socket options");

		res = connect(mSocketFd, (struct sockaddr *)mpHostAddr, sizeof(*mpHostAddr));

		free(mpHostAddr);
		mpHostAddr = NULL;

		if (!res)
		{
			mState = StCltConnDone;
			break;
		}

		numErr = errGet();
#ifdef _WIN32
		if (numErr == WSAEINPROGRESS)
#else
		if (numErr == EINPROGRESS)
#endif
		{
			mStartMs = curTimeMs;
			mState = StCltConnDoneWait;
			break;
		}

		return procErrLog(-1, "could not connect to host: %s (%d)",
						errnoToStr(numErr).c_str(), numErr);

		break;
	case StCltConnDoneWait:

		if (diffMs > dTmoDefaultConnDoneMs)
			return procErrLog(-1, "timeout connecting to host");

		success = connClientDone();
		if (success == Pending)
			break;

		if (success != Positive)
			return procErrLog(-1, "client connect failed");

		mState = StCltConnDone;

		break;
	case StCltConnDone:

		addrInfoSet();
		mSendReady = true;

		mState = StConnMain;

		break;
	case StConnMain:

		if (mDone)
			return Positive;

		connCheck = read(NULL, 0);
		if (connCheck >= 0)
			break;

		if (mErrno)
			return procErrLog(-1, "connection error occured: %s",
							errnoToStr(mErrno).c_str());

		return Positive;

		break;
	case StTmp:

		break;
	default:
		break;
	}

	return Pending;
}

Success TcpTransfering::shutdown()
{
	if (mpHostAddr)
	{
		free(mpHostAddr);
		mpHostAddr = NULL;
	}

#if CONFIG_PROC_HAVE_DRIVERS
	Guard lock(mSocketFdMtx);
#endif
	disconnect();

	return Positive;
}

/*
Literature socket programming
- https://man7.org/linux/man-pages/man2/poll.2.html
- https://man7.org/linux/man-pages/man2/recvmsg.2.html
- https://man7.org/linux/man-pages/man2/select.2.html
- https://man7.org/linux/man-pages/man2/read.2.html
- https://man7.org/linux/man-pages/man2/send.2.html
  - Important: MSG_NOSIGNAL
- https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-send
- https://www.ibm.com/support/knowledgecenter/en/ssw_ibm_i_74/rzab6/poll.htm
- https://docs.microsoft.com/en-us/windows/desktop/winsock/complete-server-code
- https://stackoverflow.com/questions/28027937/cross-platform-sockets
- https://daniel.haxx.se/docs/poll-vs-select.html
- https://deepix.github.io/2016/10/21/tcprst.html
- https://stackoverflow.com/questions/11436013/writing-to-a-closed-local-tcp-socket-not-failing
- https://www.usenix.org/legacy/publications/library/proceedings/usenix99/full_papers/banga/banga_html/node3.html
- https://www.linuxtoday.com/blog/multiplexed-i0-with-poll.html
- https://github.com/torvalds/linux/blob/master/include/uapi/asm-generic/poll.h
- https://stackoverflow.com/questions/24791625/how-to-handle-the-linux-socket-revents-pollerr-pollhup-and-pollnval
*/
ssize_t TcpTransfering::read(void *pBuf, size_t lenReq)
{
#if CONFIG_PROC_HAVE_DRIVERS
	Guard lock(mSocketFdMtx);
#endif
	if (!mReadReady)
		return 0;

	if (mSocketFd == INVALID_SOCKET)
		return -1;

	ssize_t numBytes = 0;
	bool peek = false;
	char buf[1];

	if (!pBuf || !lenReq)
	{
		pBuf = buf;
		lenReq = sizeof(buf);
		peek = true;
	}
#ifdef _WIN32
	numBytes = ::recv(mSocketFd, (char *)pBuf, (int)lenReq, MSG_PEEK);
#else
	numBytes = ::recv(mSocketFd, (char *)pBuf, lenReq, MSG_PEEK);
#endif
	if (numBytes < 0)
	{
		int numErr = errGet();
#ifdef _WIN32
		if (numErr == WSAEWOULDBLOCK || numErr == WSAEINPROGRESS)
			return 0; // std case and ok

		if (numErr == WSAECONNRESET)
		{
			procDbgLog("connection reset by peer");
			disconnect();
			return -2;
		}
#else
		if (numErr == EWOULDBLOCK || numErr == EINPROGRESS || numErr == EAGAIN)
			return 0; // std case and ok

		if (numErr == ECONNRESET)
		{
			procDbgLog("connection reset by peer");
			disconnect();
			return -2;
		}
#endif
		disconnect(numErr);

		return procErrLog(-3, "recv() failed: %s",
							errnoToStr(numErr).c_str());
	}

	if (!numBytes)
	{
		procDbgLog("connection closed by peer (EOF)");
		disconnect();
		return -4;
	}

	if (peek)
		return numBytes;
#ifdef _WIN32
	numBytes = ::recv(mSocketFd, (char *)pBuf, (int)numBytes, 0);
#else
	numBytes = ::recv(mSocketFd, (char *)pBuf, (size_t)numBytes, 0);
#endif
	//procDbgLog("received data. len: %d", numBytes);

	mBytesReceived += (size_t)numBytes;

	return numBytes;
}

ssize_t TcpTransfering::readFlush()
{
	ssize_t bytesRead = 1, bytesSum = 0;
	char buf[32];

	while (bytesRead)
	{
		bytesRead = read(buf, sizeof(buf));
		bytesSum += bytesRead;
	}

	return bytesSum;
}

ssize_t TcpTransfering::send(const void *pData, size_t lenReq)
{
	if (!mSendReady)
		return procErrLog(-1, "unable to send data. Not ready");
#if CONFIG_PROC_HAVE_DRIVERS
	Guard lock(mSocketFdMtx);
#endif
	// DO NOT SEND AN ERROR MESSAGE AT THIS POINT!
	// Reason:
	// Otherwise we have an endless loop for the
	// SystemDebugging() log peers
	if (mSocketFd == INVALID_SOCKET)
		return -1;

	ssize_t res;
	size_t lenBkup = lenReq;
	size_t bytesSent = 0;

	while (lenReq)
	{
		/* IMPORTANT:
		  * Connection may be reset by remote peer already.
		  * Flag MSG_NOSIGNAL prevents function send() to
		  * emit signal SIGPIPE and therefore kill the entire
		  * application in this case.
		  */
#ifdef _WIN32
		res = ::send(mSocketFd, (const char *)pData, (int)lenReq, MSG_NOSIGNAL);
#else
		res = ::send(mSocketFd, (const char *)pData, lenReq, MSG_NOSIGNAL);
#endif
		if (res < 0)
		{
			int numErr = errGet();
#ifdef _WIN32
			if (numErr == WSAEWOULDBLOCK || numErr == WSAEINPROGRESS)
				return 0; // std case and ok
#else
			if (numErr == EWOULDBLOCK || numErr == EINPROGRESS || numErr == EAGAIN)
				return 0; // std case and ok
#endif
			disconnect(numErr);

			return procErrLog(-1, "connection down: %s",
							errnoToStr(numErr).c_str());
		}

		if (!res)
			break;

		pData = ((const uint8_t *)pData) + res;
		lenReq -= (size_t)res;

		bytesSent += (size_t)res;
	}

	if (bytesSent != lenBkup)
		procWrnLog("not all data has been sent");

	mBytesSent += bytesSent;

	return (ssize_t)bytesSent;
}

void TcpTransfering::disconnect(int err)
{
#if CONFIG_PROC_HAVE_DRIVERS
	//Guard lock(mSocketFdMtx); // every caller must lock in advance!
#endif
	if (mSocketFd == INVALID_SOCKET)
	{
		procDbgLog("socket closed already");
		return;
	}

	procDbgLog("closing socket: %d", mSocketFd);
	mErrno = err;
#ifdef _WIN32
	::closesocket(mSocketFd);
#else
	::close(mSocketFd);
#endif
	mSocketFd = INVALID_SOCKET;
}

Success TcpTransfering::socketOptionsSet()
{
#if CONFIG_PROC_HAVE_DRIVERS
	Guard lock(mSocketFdMtx);
#endif
	int opt;
	int res;
	bool ok;

	opt = 1;
	res = ::setsockopt(mSocketFd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&opt, sizeof(opt));
	if (res)
		return procErrLog(-2, "setsockopt(SO_KEEPALIVE) failed: %s",
							errnoToStr(errGet()).c_str());

	ok = fileNonBlockingSet(mSocketFd);
	if (!ok)
		return procErrLog(-3, "could not set non blocking mode: %s",
							errnoToStr(errGet()).c_str());

	mReadReady = true;

	return Positive;
}

/* Literature
 * - https://man7.org/linux/man-pages/man2/connect.2.html
 * - https://man7.org/linux/man-pages/man2/select.2.html
 * - https://man7.org/linux/man-pages/man2/poll.2.html
 * - https://man7.org/linux/man-pages/man2/getsockopt.2.html
 * - https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select
 * - https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsapoll
 * - https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-getsockopt
 */
Success TcpTransfering::connClientDone()
{
#ifdef _WIN32
	WSAPOLLFD fds[1];
#else
	struct pollfd fds[1];
#endif
	int res;

	fds[0].fd = mSocketFd;
	fds[0].events = POLLOUT;
#ifdef _WIN32
	res = WSAPoll(fds, 1, 0);
#else
	res = poll(fds, 1, 0);
#endif
	if (!res)
		return Pending;

	if (res < 0)
		return procErrLog(-1, "could not poll socket: %s",
						errnoToStr(errGet()).c_str());

	if (!(fds[0].revents & POLLOUT))
		return procErrLog(-1, "didn't get POLLOUT event");

	int errSock;
#ifdef _WIN32
	int len = sizeof(errSock);
	res = ::getsockopt(mSocketFd, SOL_SOCKET, SO_ERROR, (char *)&errSock, &len);
#else
	socklen_t len = sizeof(errSock);
	res = ::getsockopt(mSocketFd, SOL_SOCKET, SO_ERROR, &errSock, &len);
#endif
	if (res)
		return procErrLog(-2, "getsockopt(SO_ERROR) failed: %s",
							errnoToStr(errGet()).c_str());

	if (errSock)
		return procErrLog(-2, "socket error: %s",
							errnoToStr(errSock).c_str());

	return Positive;
}

/* Literature
 * - http://man7.org/linux/man-pages/man2/getsockname.2.html
 * - http://man7.org/linux/man-pages/man2/getpeername.2.html
 * - https://stackoverflow.com/questions/10167540/how-to-get-local-ip-address-and-port-in-unix-socket-programming
 *   - https://beej.us/guide/bgnet/html/multi/getpeernameman.html
 * - http://www.masterraghu.com/subjects/np/introduction/unix_network_programming_v1.3/ch04lev1sec10.html
 * - https://linux.die.net/man/3/inet_ntoa
 *   The inet_ntoa() function converts the Internet host address in, given in network byte order, to a string in IPv4 dotted-decimal notation.
 *   The string is returned in a statically allocated buffer, which subsequent calls will overwrite.
 */
void TcpTransfering::addrInfoSet()
{
	if (mInfoSet)
		return;

	if (mSocketFd == INVALID_SOCKET)
		return;

	struct sockaddr_storage addr;
	socklen_t addrLen;
	int res;
	bool ok;
#if CONFIG_PROC_IPV6_ENABLED
	bool &isIPv6Local = mIsIPv6Local;
	bool &isIPv6Remote = mIsIPv6Remote;
#else
	bool isIPv6Local, isIPv6Remote;
#endif
	memset(&addr, 0, sizeof(addr));
	addrLen = sizeof(addr);

	res = ::getsockname(mSocketFd, (struct sockaddr *)&addr, &addrLen);
#ifdef _WIN32
	if (res == SOCKET_ERROR)
		return;
#else
	if (res == -1)
		return;
#endif
	ok = sockaddrInfoGet(addr, mAddrLocal, mPortLocal, isIPv6Local);
	if (!ok)
		return;

	// -----------------

	memset(&addr, 0, sizeof(addr));
	addrLen = sizeof(addr);

	res = ::getpeername(mSocketFd, (struct sockaddr *)&addr, &addrLen);
#ifdef _WIN32
	if (res == SOCKET_ERROR)
		return;
#else
	if (res == -1)
		return;
#endif
	ok = sockaddrInfoGet(addr, mAddrRemote, mPortRemote, isIPv6Remote);
	if (!ok)
		return;

	mInfoSet = true;
}

struct sockaddr_storage *TcpTransfering::addrStringToSock(const string &strAddr, uint16_t numPort)
{
	struct sockaddr_storage *pAddr;

	pAddr = (struct sockaddr_storage *)calloc(1, sizeof(struct sockaddr_storage));
	if (!pAddr)
		return NULL;

	struct sockaddr_in *pAddr4 = (struct sockaddr_in *)pAddr;
#if CONFIG_PROC_IPV6_ENABLED
	struct sockaddr_in6 *pAddr6 = (struct sockaddr_in6 *)pAddr;
#endif
	if (inet_pton(AF_INET, strAddr.c_str(), &pAddr4->sin_addr) == 1)
	{
		pAddr4->sin_family = AF_INET;
		pAddr4->sin_port = htons(numPort);
	}
#if CONFIG_PROC_IPV6_ENABLED
	else
	if (inet_pton(AF_INET6, strAddr.c_str(), &pAddr6->sin6_addr) == 1)
	{
		pAddr6->sin6_family = AF_INET6;
		pAddr6->sin6_port = htons(numPort);
	}
#endif
	else
	{
		free(pAddr);
		return NULL;
	}

	return pAddr;
}

int TcpTransfering::errGet()
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

string TcpTransfering::errnoToStr(int num)
{
#if CONFIG_PROC_HAVE_DRIVERS
	Guard lock(mtxStrerror);
#endif
	return string(::strerror(num));
}

void TcpTransfering::processInfo(char *pBuf, char *pBufEnd)
{
	//dInfo("State\t\t\t%s\n", ProcStateString[mState]);
	dInfo("Bytes received\t\t%d\n", (int)mBytesReceived);

	if (!mInfoSet)
		return;

#if CONFIG_PROC_IPV6_ENABLED
	dInfo("%s%s%s:%d <--> ",
		mIsIPv6Local ? "[" : "",
		mAddrLocal.c_str(),
		mIsIPv6Local ? "]" : "",
		mPortLocal);
#else
	dInfo("%s:%d <--> ", mAddrLocal.c_str(), mPortLocal);
#endif
	if (mAddrLocal.size() > INET_ADDRSTRLEN)
		dInfo("\n");

#if CONFIG_PROC_IPV6_ENABLED
	dInfo("%s%s%s:%d\n",
		mIsIPv6Remote ? "[" : "",
		mAddrRemote.c_str(),
		mIsIPv6Remote ? "]" : "",
		mPortRemote);
#else
	dInfo("%s:%d\n", mAddrRemote.c_str(), mPortRemote);
#endif
}

/* static functions */

bool TcpTransfering::sockaddrInfoGet(struct sockaddr_storage &addr,
								string &strAddr,
								uint16_t &numPort,
								bool &isIPv6)
{
#if CONFIG_PROC_IPV6_ENABLED
	char buf[INET6_ADDRSTRLEN];
#else
	char buf[INET_ADDRSTRLEN];
#endif
	const char *pRes = NULL;

	if (addr.ss_family == AF_INET)
	{
		struct sockaddr_in *pAddr = (struct sockaddr_in *)&addr;

		numPort = ntohs(pAddr->sin_port);
		isIPv6 = false;

		pRes = ::inet_ntop(AF_INET, &pAddr->sin_addr, buf, sizeof(buf));
	}
#if CONFIG_PROC_IPV6_ENABLED
	else if (addr.ss_family == AF_INET6)
	{
		struct sockaddr_in6 *pAddr = (struct sockaddr_in6 *)&addr;

		numPort = ntohs(pAddr->sin6_port);
		isIPv6 = true;

		pRes = ::inet_ntop(AF_INET6, &pAddr->sin6_addr, buf, sizeof(buf));
	}
#endif

	if (!pRes)
		return false;

	strAddr = string(buf);

	return true;
}

uint32_t TcpTransfering::millis()
{
	auto now = steady_clock::now();
	auto nowMs = time_point_cast<milliseconds>(now);
	return (uint32_t)nowMs.time_since_epoch().count();
}

bool TcpTransfering::fileNonBlockingSet(SOCKET fd)
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

#ifdef _WIN32
bool TcpTransfering::wsaInit()
{
#if CONFIG_PROC_HAVE_DRIVERS
	Guard lock(mtxGlobalInit);
#endif
	if (globalInitDone)
		return true;

	dbgLog("global WSA initialization");

	int verLow = 2;
	int verHigh = 2;
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;

	wVersionRequested = MAKEWORD(verLow, verHigh);

	err = WSAStartup(wVersionRequested, &wsaData);
	if (err)
	{
		errLog(-2, "WSAStartup() failed");
		return false;
	}

	if (LOBYTE(wsaData.wVersion) != verLow || HIBYTE(wsaData.wVersion) != verHigh)
	{
		errLog(-3, "could not find a usable version of Winsock.dll");
		WSACleanup();
		return false;
	}

	Processing::globalDestructorRegister(globalWsaDestruct);

	globalInitDone = true;

	return true;
}

void TcpTransfering::globalWsaDestruct()
{
	WSACleanup();
	dbgLog("TcpTransfering(): done");
}
#endif

