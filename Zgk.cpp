#include "stdafx.h"
#ifdef _WIN32
#include <winsock.h> 
#else
#include <sys/types.h> 
#include <sys/socket.h>
#endif
#include "Zgk.h"
#include <stdlib.h>

void LOG(unsigned loglevel, const char* pszmsg, ...) {
	va_list list;
	va_start(list, pszmsg);
	if ((LOGMASK&loglevel) != 0) {
		time_t now = time(NULL);
		struct tm *pnow = localtime(&now);
		printf("%04d/%02d/%02d %02d:%02d:%02d [%08x] ", pnow->tm_year, pnow->tm_mon,
			pnow->tm_mday, pnow->tm_hour, pnow->tm_min, pnow->tm_sec, loglevel);
		vprintf(pszmsg, list);
		printf("\n");
		va_end(list);
	}
}


void atobin(byte ret[], const char src[]) {
	for (int i = 0; src[i]; i++) {
		if ((i & 1) == 0)
			ret[i >> 1] = ((src[i] >= 'a') ? (src[i] - 'a' + 10) :
			((src[i] >= 'A') ? (src[i] - 'a' + 10) : src[i] - '0')) << 4;
		else
			ret[i >> 1] |= ((src[i] >= 'a') ? (src[i] - 'a' + 10) :
			((src[i] >= 'A') ? (src[i] - 'a' + 10) : src[i] - '0'));
	}
}

void SetCurDate6(byte* p) {
	time_t t;
	time(&t);
	struct tm *ptime = localtime(&t);
	int yy = ptime->tm_year % 100;
	*p++ = ((yy / 10) << 4) | (yy % 10);
	*p++ = ((ptime->tm_mon / 10) << 4) | (ptime->tm_mon % 10);
	*p++ = ((ptime->tm_mday / 10) << 4) | (ptime->tm_mday % 10);
	*p++ = ((ptime->tm_hour / 10) << 4) | (ptime->tm_hour % 10);
	*p++ = ((ptime->tm_min / 10) << 4) | (ptime->tm_min % 10);
	*p = ((ptime->tm_sec / 10) << 4) | (ptime->tm_sec % 10);
}

void SetBinary(byte*p, unsigned num,  int size) {
	if (size < 2) {
		*p = (byte)(num & 0xff);
	}
#if(1)	// BIG ENDIAN
	else if (size < 3) {
		*p++ = (num >> 8) & 0xff;
		*p = num & 0xff;
	}
	else if (size < 4) {
		*p++ = (num >> 16) & 0xff;
		*p++ = (num >> 8) & 0xff;
		*p = num & 0xff;
	} 
	else {
		*p++ = (num >> 24) & 0xff;
		*p++ = (num >> 16) & 0xff;
		*p++ = (num >> 8) & 0xff;
		*p = num & 0xff;
	}
#else
	else if (size < 3) {
		*p++ = num & 0xff;
		*p = (num >> 8) & 0xff;
	}
	else if (size < 4) {
		*p++ = num & 0xff;
		*p++ = (num >> 8) & 0xff;
		*p = (num >> 16) & 0xff;
	}
	else {
		*p++ = num & 0xff;
		*p++ = (num >> 8) & 0xff;
		*p++ = (num >> 16) & 0xff;
		*p = (num >> 24) & 0xff;
	}
#endif
}

unsigned GetBinary(byte*p, int size) {
	if (size < 2)
		return *p;
#if(1)	// BIG ENDIAN
	else if (size < 3)
		return (p[0] << 8 | p[1]);
	else if (size < 4)
		return ((p[0] << 16) | (p[1] << 8) | p[2]);
	else
		return ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
#else
	else if (size < 3)
		return *((unsigned short *)p);
	else if (size < 4)
		return (p[0] | (p[1] << 8) | (p[2] << 16));
	else
		return *((unsigned *)p);
#endif
}

void IncrementBCD(byte* bcdval, int size) {
	for (int idx = size - 1; idx >= 0; idx--) {
		if (++bcdval[idx] != 0)
			break;
	}
}

void Add2BIVal(byte* bcdval, int size, int addval) {
	int idxStart = size - sizeof(unsigned);
	unsigned  LEval = (bcdval[idxStart + 3] | (bcdval[idxStart + 2] << 8) | (bcdval[idxStart + 1] << 16) | (bcdval[idxStart] << 24)) + addval;
	bcdval[idxStart + 3] = LEval & 0xff; 
	bcdval[idxStart + 2] = (LEval >> 8) & 0xff;
	bcdval[idxStart + 1] = (LEval >> 16) & 0xff;
	bcdval[idxStart] = (LEval >> 24) & 0xff;
}

ZgkSubLMsg::ZgkSubLMsg() {
	pszmsg = 0;
	_length = 0;	
}
ZgkSubLMsg::~ZgkSubLMsg() {
	if (pszmsg)
		free(pszmsg);
}

ZgkSubLMsg::ZgkSubLMsg(const ZgkSubLMsg& src) {
	_length = src._length;
	if (src.pszmsg) {
		pszmsg = (byte *)malloc(_length);
		memcpy(pszmsg, src.pszmsg, _length);
	}
	else
		pszmsg = nullptr;
}

byte*	ZgkSubLMsg::makeTextData(unsigned len, bool isCtl) {
	_length = len + sizeof(ZgkSubLHeader);
	if (pszmsg)
		pszmsg = (byte *)realloc(pszmsg, _length);
	else
		pszmsg = (byte *)malloc(_length);
	((ZgkSubLHeader*)pszmsg)->initialize();
	((ZgkSubLHeader*)pszmsg)->setidno(isCtl ? 1 : 0);
	((ZgkSubLHeader*)pszmsg)->SetDataLen(_length);

	return (pszmsg + sizeof(ZgkSubLHeader));
}

byte *	ZgkSubLMsg::getMsgPtr() {
	if (!pszmsg) {
		pszmsg = (byte *)malloc(4 + sizeof(ZgkSubLHeader));
		_length = sizeof(ZgkSubLHeader);
	}
	return pszmsg;
}

void	ZgkSubLMsg::setDataType(bool isCtrl) { 
	ZgkSubLHeader* pheader = (ZgkSubLHeader*)getMsgPtr();
	((ZgkSubLHeader*)pszmsg)->setidno(isCtrl ? 1 : 0);
}



