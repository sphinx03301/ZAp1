#pragma once
#include <process.h>
#include <thread>
#include <future>
#include <mutex>
#include <vector>
#include <chrono>
#include <queue>
#include <time.h>

using namespace std;
#define elementsof(a) (sizeof(a)/sizeof(a[0]))
#pragma pack(push)
#pragma pack(1)

//----------- マクロ、TYPEDEF --------------------------//

#define LMASK_DEBUG		0x00000100
#define LMASK_INFO		0x04000000
#define LMASK_WARNING	0x02000000
#define LMASK_ERROR		0x01000000

typedef unsigned char byte;
typedef promise<int> IPromise;
typedef future<int> IFuture;

//----------- 関数プロトタイプ --------------------------//
#ifdef _NOLOG
#define LOG(level, msg) ()
#else
#define LOGMASK  0xFFFFFFFF
void LOG(unsigned loglevel, const char* pszmsg, ...);
#endif

// 16進文字列からバイナリ変換
void atobin(byte ret[], const char src[]);

// 現在日時 (YYMMDDHHmmSS)をBCDで6バイトへ格納する
void SetCurDate6(byte* p);

void IncrementBCD(byte* bcdval, int size);
void Add2BIVal(byte* bcdval, int size, int addval);

//----------- クラス定義   -------------------------//

//---------------------------------------------------------
//! Windowsのイベントのように別スレッドにシグナルを
//!	送るためのpromiseを拡張したクラス
//---------------------------------------------------------
template<class T> class promiseEx {

	promise<T> *pppromise[16];
	int		idxSet, idxGet, bitMask;
public:
	promiseEx<T>() {
		idxSet = idxGet = 0;
		memset(pppromise, 0, sizeof(pppromise));
		*pppromise = new  promise<T>();
		bitMask = elementsof(pppromise) - 1;
	}
	~promiseEx<T>() {
		for (int i = 0; i < elementsof(pppromise); i++)
			if (pppromise[i])
				delete pppromise[i];
	}

	T get_value() {
		while (pppromise[idxGet] == NULL)
			std::this_thread::yield();
		future<T> f = pppromise[idxGet]->get_future();
		f.wait();
		T ret = f.get();
		delete pppromise[idxGet];
		pppromise[idxGet] = NULL;
		idxGet = ++idxGet & bitMask;
		return ret;
	}

	template<class _Rep, class _Per>
	bool get_value_for(const chrono::duration<_Rep, _Per>&  span, T* pret) {
		while (pppromise[idxGet] == NULL)
			std::this_thread::yield();
		future<T> f = pppromise[idxGet]->get_future();
		future_status fsts = f.wait_for(span);
		if (fsts == future_status::timeout) {

		} else {
			*pret = f.get();
			delete pppromise[idxGet];
			pppromise[idxGet] = NULL;
			idxGet = ++idxGet & bitMask;
		}

		return  (fsts != future_status::timeout);
		
	}

	void set_value(T value) {
		pppromise[idxSet++]->set_value(value);
		idxSet &= bitMask;
		if (pppromise[idxSet])
			delete pppromise[idxSet];
		pppromise[idxSet] = new  promise<T>();
	}

};
typedef promiseEx<int> IPromiseEx;

class ZgkLayer {
public:
	ZgkLayer() {}
	virtual int SendRequest() { return 0;  }
	virtual void SendFinished() {}
	virtual int DataReceived() { return 0; }
};


void SetCurDate6(byte* p);
void SetBinary(byte*p, unsigned num, int size = 2);
unsigned GetBinary(byte*p, int size = 2);

class ZgkSubLHeader {
public:
	byte msglen[2];
	byte vno_idno;
	char filler[5];
	ZgkSubLHeader() { initialize(); }
	void initialize(bool isAck = false) { 
		*((long long *)this) = 0; 
		if (isAck) { SetDataLen(sizeof(ZgkSubLHeader)); setidno(1); }
	}
	void setidno(byte idno) { vno_idno = 0x10 | (idno & 0x0f); }
	bool isCtrlMsg() { return (vno_idno & 0x0f) != 0; }
	void SetDataLen(unsigned len) { SetBinary(msglen, len , sizeof(msglen)); }
	unsigned getmsglen() { return GetBinary(msglen, sizeof(msglen)); }
};

class ZgkSubLMsg {
friend class ZgkSubLMsg;
protected:
	byte *pszmsg;
	int  _length;
public:
	ZgkSubLMsg();
	ZgkSubLMsg(const ZgkSubLMsg&);
	~ZgkSubLMsg();
	inline byte *getTextPtr() { return (pszmsg ? &pszmsg[8] : nullptr); }
	byte *getMsgPtr();
	ZgkSubLHeader* getHeaderPtr() { return (ZgkSubLHeader*)getMsgPtr(); }
	int getLength() { return _length;  }
	byte*	makeTextData(unsigned  len, bool isCtl = false);
	void	setDataType(bool isCtrl);
	void	Move(ZgkSubLMsg& src) {
		if (pszmsg)	free(pszmsg);
		pszmsg = src.pszmsg;
		_length = src._length;
		src.pszmsg = nullptr;
		src._length = 0;
	}
};

class TcpLayer {
friend class ZgkSublayer;
protected:
	SOCKET	_soc;
	struct sockaddr_in _inaddr;
	bool	isConnected;
public:
	TcpLayer(const char* pszHost, int port) {
		_soc = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		struct timeval recv_tv;
		recv_tv.tv_sec = 60;	// リードタイムアウト指定
		recv_tv.tv_sec = 0;
		setsockopt(_soc, SOL_SOCKET, SO_RCVTIMEO, (const char *)&recv_tv, sizeof(recv_tv));		
		memset(&_inaddr, 0, sizeof(_inaddr));
		_inaddr.sin_family = AF_INET;
		_inaddr.sin_addr.s_addr = inet_addr(pszHost);
		_inaddr.sin_port = htons(port);
	}

	~TcpLayer() {
		Disconnect();
	}

	int Connect() {
		int ret = connect(_soc, (struct sockaddr *) &_inaddr, sizeof(_inaddr));
		if (ret == 0) {
			isConnected = true;
		}
		return ret;
	}

	int Disconnect() {
		if (isConnected) {
			closesocket(_soc);
			isConnected = false;
		}
		return 0;
	}
};

class ZgkSublayer {
#define COMM_TIMEOUT  30*1000
friend class ZgkSublayer;
public:
	enum SLSTATUS {
		DISCONNECT = 1,
		NEUTRAL ,
		WAITINGACK,
		RECEIVEING ,
	};
	/*
	DISCONNECT*/
	enum SLCMD {
		CONNECT = 1,
		RECVDATA,
		SENDACK,
		SENDMSG,
		RECVACK,
		ABEND = 9
	};

	enum EVENTS {
		EV_E1 = 1,
		EV_E2,
		EV_E3,
		EV_E4,
		EV_OUTPUTDATA = EV_E4,
		EV_E5,
		EV_WAITLACK = EV_E5,
		EV_E6,
		EV_RCVRROR  = EV_E6,
		EV_E7,
		EV_TIMEOUT=7,
		EV_E8,
		EV_E9,
		EV_ERROR,
		EV_QUIT,
	};

	enum EVOBJ {
		EO_QUIT = 0,
		EO_COMM,
		EO_TOUT_ON,
		EO_TOUT_FIRE,
		EO_EVENT,
		EO_MAX
	};
	TcpLayer* _socket;
	ZgkLayer* _applayer;
	queue<ZgkSubLMsg*> _dataque;
protected:
	IPromiseEx *psignal[EO_MAX];
	bool isQuitting;
	union {
		unsigned int	ltmp;
		struct _flags {
			unsigned	flg_connect : 1;	//E1
			unsigned	flg_shrecv : 1;		// E2
			unsigned	flg_recvfin : 1;	//E3	
			unsigned	flg_sendreq : 1;	//E4
			unsigned	flg_recvack : 1;	//E5
			unsigned	flg_recverr : 1;	//E6
			unsigned	flg_timeout : 1;	//E7
			unsigned	flg_relreq : 1;		//E8
			unsigned	flg_relnotice : 1;
			unsigned	flg_error : 1;
			unsigned	flg_quit : 1;
		} flags;
	} uflags, uflags_ev;
	ZgkSubLMsg  msgAck;
public:
	SLSTATUS status;
	ZgkSublayer(TcpLayer* soc, ZgkLayer *papp) {
		_socket = soc;  
		status = DISCONNECT; 
		isQuitting = false;
		_applayer = papp;
		msgAck.getHeaderPtr()->initialize(true);	// LAckとして初期化する
		main();
	}
	~ZgkSublayer() { Dispose(); }

	void Dispose() {
		for (int i = 0; i < elementsof(psignal); i++) {
			if (psignal[i])
				delete psignal[i];
		}
	}

	void main() {
		for (int i = 0; i < elementsof(psignal); i++)
			psignal[i] = new IPromiseEx();

		std::thread tcomm([](void *p) {((ZgkSublayer*)p)->CommLoop();  }, this);
		tcomm.detach();
		std::thread ttimer([](void *p) {((ZgkSublayer*)p)->TimeoutTimer();  }, this);
		ttimer.detach();
		//std::thread tevent([](void *p) {((ZgkSublayer*)p)->EventLoop();  }, this);
		//tevent.detach();
	}


	void CommLoop() {
		ZgkSubLMsg msg;
		int	ret;
		unsigned ttl;
		bool isOnece;
		while (!isQuitting) {
			if (psignal[EO_COMM]->get_value())
				break;
			switch (status) {
			case DISCONNECT: 
				{
					LOG(LMASK_INFO, "Comm Disconnect");
				}
				break;
			case NEUTRAL: {
				status = RECEIVEING;
				ttl = 0;
				char *ptr = (char *)msg.getMsgPtr();
				do {
					ret = recv(_socket->_soc, ptr + ttl, 8 - ttl, 0);
					ttl += ret;
				} while (ret > 0 && ttl < 8);
				unsigned remain = ((ZgkSubLHeader*)ptr)->getmsglen() - sizeof(ZgkSubLHeader);
				byte *pdata = msg.makeTextData(remain);
				ttl = 0;
				do {
					ret = recv(_socket->_soc, (char *)pdata + ttl, remain - ttl, 0);
					ttl += ret;
				} while (ret > 0 && ttl < remain);

				if (ttl < remain || msg.getHeaderPtr()->isCtrlMsg()) {
					// 長さが足りない、もしくは識別子がデータじゃないならエラー
					LOG(LMASK_ERROR, "Comm  NEUTRAL  RECV ERROR");
					SetEvent(EV_RCVRROR);
				}
				else {
					_dataque.push(new ZgkSubLMsg(msg));
					LOG(LMASK_DEBUG, "Comm NEUTRAL  RECV Okay");
					SetEvent(EV_E3);
				}
			}
				break;
			case WAITINGACK: {
				ttl = 0;
				isOnece = true;
				char *ptr = (char *)msg.getMsgPtr();
				do {
					ret = recv(_socket->_soc, ptr + ttl, 8 - ttl, 0);
					ttl += ret;
					if (isOnece) {
						isOnece = false;
						psignal[EO_TOUT_FIRE]->set_value(0);
					}
				} while (ret > 0 && ttl < 8);
				if (!((ZgkSubLHeader*)ptr)->isCtrlMsg()) {
					// 論理ACKならば識別子は1となるはず、エラー
					LOG(LMASK_ERROR, "Comm WAITINGACK LAck Error");
					SetEvent(EV_RCVRROR);
				}
				else {
					LOG(LMASK_DEBUG, "Comm WAITINGACK LAck Okay");
					status = NEUTRAL;
					_applayer->SendFinished();
				}
			}
				break;
			case  RECEIVEING: {
				LOG(LMASK_DEBUG, "Comm RECEIVEING");
			}
				break;
			}
		}
	}


	IPromiseEx* GetPromise(enum EVOBJ idx) {
		return  psignal[idx];
	}

	void QuitReq() {
		isQuitting = true;
		GetPromise(EO_TOUT_ON)->set_value(1);
		GetPromise(EO_COMM)->set_value(1);
		GetPromise(EO_EVENT)->set_value(EV_QUIT);
	}

	void UseTimer() {
		GetPromise(EO_TOUT_ON)->set_value(0);
	}

	void CommReady() {
		GetPromise(EO_COMM)->set_value(0);
	}

	//! 無通信監視用タイマー。
	void TimeoutTimer() {
		while (!isQuitting) {
			if (psignal[EO_TOUT_ON]->get_value())
				break;
			int value;
			if (!psignal[EO_TOUT_FIRE]->get_value_for(chrono::milliseconds(COMM_TIMEOUT), &value)) {
				SetEvent(EV_TIMEOUT);
				LOG(LMASK_DEBUG, "Timeout occured");
			}
		}
	}

	void SetEvent(int flg) {
		//uflags_ev.ltmp |= (1 << (flg - 1));
		//uflags.ltmp |= uflags_ev.ltmp;
		//psignal[EO_EVENT]->set_value(uflags.ltmp);
		int  EventBits = (1 << (flg - 1));
		psignal[EO_EVENT]->set_value(EventBits);
	}

	void EventLoop() {
		while (!isQuitting) {
			uflags.ltmp |= psignal[EO_EVENT]->get_value();
			if (uflags.flags.flg_quit) {
				_socket->Disconnect();
				break;
			}
			switch (status) {
			case DISCONNECT: {
				if (uflags.flags.flg_connect) {
					uflags.flags.flg_connect = 0;
					if (_socket->Connect() != 0) {
						LOG(LMASK_ERROR, "Event DISCONNECT CONNECT ERROR");
						SetEvent(EV_ERROR);
					}
					else {
						status = NEUTRAL;
						SetEvent(EV_OUTPUTDATA);
					}
				} else if (uflags.flags.flg_error) {
					isQuitting = true;
				}
			}
				break;
			case NEUTRAL: {
				if (uflags.flags.flg_sendreq) {	//E1から開局要求
					uflags.flags.flg_sendreq = 0;
					LOG(LMASK_DEBUG, "Event NEUTRAL Send Request");
					if (_applayer->SendRequest() == 0) {	//送信したら
						status = WAITINGACK;			// Ack待ちへ遷移
						CommReady();					//通信再開
					}
					else {
						_socket->Disconnect();
						isQuitting = true;
					}
				} else if (uflags.flags.flg_error) {
					_socket->Disconnect();
					isQuitting = true;
				}
			}
				break;
			case WAITINGACK: {
				if (uflags.flags.flg_error) {
					LOG(LMASK_ERROR, "Event WAITINGACK error");
					_socket->Disconnect();
					isQuitting = true;
				}
				else if (uflags.flags.flg_recverr) {
					LOG(LMASK_ERROR, "Event WAITINGACK Receive Error");
					status = DISCONNECT;
					_socket->Disconnect();
					isQuitting = true;
				}
			}
				break;
			case  RECEIVEING: {
				if (uflags.flags.flg_recvfin) {	// 受信完了
					uflags.flags.flg_recvfin = 0;
					//Ack送信
					SendMessage(msgAck.getMsgPtr(), sizeof(ZgkSubLHeader), false);
					LOG(LMASK_DEBUG, "Event RECEIVEING Receive Okay and Ack sent");
					status = NEUTRAL;
					_applayer->DataReceived();
				}
				else if (uflags.flags.flg_error || uflags.flags.flg_recverr || uflags.flags.flg_timeout) {
					LOG(LMASK_ERROR, "Event RECEIVEING Receive error");
					_socket->Disconnect();
					isQuitting = true;
				}

				}
				break;
			}
		}
	}

	int SendMessage(byte* pdata, int length, bool chkTime = true) {
		if(chkTime)
			UseTimer();
		if (send(_socket->_soc, (const char*)pdata, length, 0) < 0) {
			// エラー処理へ
			return -11;
		}
		return 0;
	}


private:
};

class TTC {
public:
	//unsigned ctype : 4;	// 接続区分 0:汎用機 1:PC 
	//unsigned ttype : 4;	// 0:制御電文(通信、ファイル) 1:データ電文
	byte tctype;
	byte seqno[2];
	byte txtlen[2];
	TTC() { initialize();  };
	void setseqno(unsigned n) { SetBinary(seqno, n, sizeof(seqno)); }
	void settxtlen(unsigned n) { SetBinary(txtlen, n, sizeof(txtlen)); }
	void initialize() { settype(0, 1);	 }
	// ttype 電文タイプ 0:制御電文(通信、ファイル) 1:データ電文
	// ctype 1固定
	void settype(unsigned ttype, unsigned ctype) {		tctype = (byte)(((ctype & 0x0f) << 4) | (ttype & 0x0f));	}
	unsigned getttype() { return ((tctype >> 4) & 0x0f); }
	unsigned getctype() { return (tctype & 0x0f);	 }
	unsigned getseqno() { return GetBinary(seqno); }
	unsigned gettxtlen() { return GetBinary(txtlen); }
	void forCtrl() { settype(0, 1); setseqno(0);  }
	void forData(unsigned seqno) { settype(1, 1); setseqno(seqno); }
};

//通信制御電文クラス
class ComCtlT {
public:
	// 電文区分 0:開局要求 1：開局応答 2:閉局要求 3:閉局応答 4:モード変更要求: 5:モード変更回答
	byte ttype;		
	// 0:OK 10:電文区分エラー 11: 相手センタ確認エラー 12:当方センタ確認エラー
	// 13:サービス時間帯エラー 14：パスワードエラー 15：16：17： 99:その他エラー
	byte result;	
	byte ToCode[7];
	byte FromCode[7];
	byte curtime[6];
	byte Passwd[6];
	byte appid;	// アプリケーションID 0 固定
	byte mode;	// 方向 0：送信	1：受信
	byte fillter[34];
	void initialize() {
		memset(this, 0, sizeof(ComCtlT));
		appid = 0xf0;	// 0固定
		mode = 0xf0;
		SetCurDate6(curtime);
	}
	ComCtlT() {		initialize(); 	}
};

class FileCtlT {
public:
	// 電文区分 10:開始要求  11:開始回答  12:終了要求  13:終了回答  14:再送要求
	byte ttype;
	byte result;			// 処理結果 00: 10～19, 99 何かしらエラー
	byte Filenm[12];
	byte AccessKey[6];
	byte textcount[2];		// 同一ファイルにおける総テキスト数
	byte reccount[3];		// 同一ファイルにおける総レコード数
	byte recid;				// コードID  0：固定長 のみ
	byte reclen[2];			// 固定長のレコード長さ
	byte resendkbn[4];		// 再送区分、From2＋To2
	byte comptype;			// 圧縮ID 0：圧縮なし 1：あり
	byte fillter[31];
	void initialize() {
		memset(this, 0, sizeof(FileCtlT));
		recid = 0xf0;
		comptype = 0xf0;
	}
	FileCtlT() { initialize(); }
	void setreccnt(unsigned n) {		SetBinary(reccount, n, sizeof(reccount));	}
	void setreclen(unsigned n) { SetBinary(reclen, n, sizeof(reclen)); }
	void settxtcnt(unsigned n) { SetBinary(textcount, n, sizeof(textcount)); }
	void setResendKbn(unsigned from, unsigned to) {
		SetBinary(resendkbn, from, 2);
		SetBinary(&resendkbn[2], to, 2);
	}
};

class ZgkAppMsg {
public:
	ZgkSubLMsg _parent;
	TTC* _pttc;
	ZgkAppMsg() { initialize(); }
	void initialize() {		_pttc = nullptr;	}
	// len : レコード長  type 1:データ電文 0 : 制御電文
	byte* makeTextData(unsigned len, int type) {
		byte* pdata = _parent.makeTextData(len + sizeof(TTC));
		byte* pret = pdata + sizeof(TTC);
		_pttc = (TTC*)pdata;
		_pttc->settxtlen(len + sizeof(TTC));
		if (type)
			_pttc->forData(1);
		else
			_pttc->forCtrl();
		return pret;
	}

	TTC* GetTTCPtr() { return _pttc; }
	byte* GetSubLMsg() {	return _parent.getMsgPtr(); 	}
	byte* GetDataPtr() {
		if(!_pttc)
			_pttc = (TTC*)_parent.getTextPtr();
		return ((byte*)_pttc + sizeof(TTC));
	}

	unsigned  getTextLen() {
		if (!_pttc)
			_pttc = (TTC*)_parent.getTextPtr();
		return _pttc->gettxtlen() - sizeof(TTC);
	}

	void AttachMove(ZgkSubLMsg& src) {
		_parent.Move(src);
	}
};


#pragma pack(pop)

class ZgkClient : public ZgkLayer
{
public:
	TcpLayer	*_pTcpLayer;
	ZgkSublayer *_pZgkSubLayer;
protected:
	byte	_ToCode[7],
			_FromCode[7],
			_Passwd[6],
			_Filenm[12],
			_AccessKey[6];
	char	_filepath[200];
	int		_appstate;
	long	fsize;
	unsigned short _reclen;
	unsigned short _textlen;
	unsigned _dataseq;
	unsigned _txtcount;
	unsigned _reccount;
	unsigned _recintext;
	FILE	*_fp;
public:
	ZgkClient() : ZgkLayer() {
		_dataseq = 1;
		_fp = NULL;
	}
	~ZgkClient() {
		if (_fp)	fclose(_fp);
	}
	void SetConnectInfo(const char* pszHost, int port) {
		_pTcpLayer = new TcpLayer(pszHost, port);
		_pZgkSubLayer = new ZgkSublayer(_pTcpLayer, this);
	}

	void SetCenterCodes(byte To[], byte From[]) {
		memcpy(_ToCode, To, sizeof(_ToCode));
		memcpy(_FromCode, From, sizeof(_FromCode));
	}

	void SetPassword(byte pass[]) { memcpy(_Passwd, pass, sizeof(_Passwd)); }

	int SetFileInfo(byte filenm[], byte *acckey, const char* filepath, unsigned reclen = 256, unsigned textlen = 2048) {
		memcpy(_Filenm, filenm, sizeof(_Filenm));
		if (acckey) {
			*((int *)&_AccessKey[0]) = *((int *)acckey);
			*((short *)&_AccessKey[4]) = *((short *)(acckey + 4));
		} else {
			*((int *)&_AccessKey[0]) = 0;
			*((short *)&_AccessKey[4]) = 0;
		}
		strcpy(_filepath, filepath);
		FILE *fp = fopen(this->_filepath, "rb");
		if (fp == NULL)
			return -1;
		fseek(fp, 0, SEEK_END);
		fsize = ftell(fp);
		fclose(fp);
		_reclen = reclen;
		_textlen = textlen;
		_reccount = (fsize + _reclen - 1) / _reclen;
		_recintext = (_textlen / _reclen);
		_txtcount = (_reccount + _recintext - 1) / _recintext;
		return 0;
	}

	void StartConnection() {
		_appstate = 1;
		_pZgkSubLayer->SetEvent(ZgkSublayer::EV_E1);
	}

	int SendRequest() {
		int ret = 0;
		ZgkAppMsg msg1;
		switch (_appstate) {
		case 1: {	// 初期ステート
			// 開局要求電文
			ComCtlT* pdata = (ComCtlT*)msg1.makeTextData(sizeof(ComCtlT), 0);
			msg1.GetTTCPtr()->forCtrl();
			pdata->initialize();
			memcpy(pdata->FromCode, _FromCode, sizeof(_FromCode));
			memcpy(pdata->ToCode, _ToCode, sizeof(_ToCode));
			memcpy(pdata->Passwd, _Passwd, sizeof(_Passwd));
			if (_pZgkSubLayer->SendMessage(msg1._parent.getMsgPtr(), msg1._parent.getLength()) != 0) {
				_pZgkSubLayer->SetEvent(ZgkSublayer::EV_ERROR);
				LOG(LMASK_ERROR, "SendRequest 1 SEND error", (_dataseq - 1));
				return -1;
			}
			LOG(0x10000000, "SendRequest 1 passed");
			// LogicalAck待ちへ移行する
		}
			break;
		case 10: {	// 開始要求電文
			FileCtlT* pdata = (FileCtlT*)msg1.makeTextData(sizeof(FileCtlT), 0);
			msg1.GetTTCPtr()->forCtrl();
			pdata->initialize();
			pdata->ttype = 0x10;	// 開始要求
			memcpy(pdata->Filenm, _Filenm, sizeof(_Filenm));
			memcpy(pdata->AccessKey, _AccessKey, sizeof(_AccessKey));
			unsigned reccount = _reccount;
			unsigned textcnt = _txtcount;
			pdata->setreccnt(reccount);
			pdata->setreclen(_reclen);
			pdata->settxtcnt(textcnt);

			if (_pZgkSubLayer->SendMessage(msg1._parent.getMsgPtr(), msg1._parent.getLength()) != 0) {
				_pZgkSubLayer->SetEvent(ZgkSublayer::EV_ERROR);
				LOG(LMASK_ERROR, "SendRequest 10 SEND error", (_dataseq - 1));
				return -1;
			}
			LOG(0x10000000, "SendRequest 13 passed");
			// LogicalAck待ちへ移行する
		}
				break;
		case 11: {	// 閉局要求電文
			ComCtlT* pdata = (ComCtlT*)msg1.makeTextData(sizeof(ComCtlT), 0);
			msg1.GetTTCPtr()->forCtrl();
			pdata->initialize();
			pdata->ttype = 0x02;	// 閉局要求
			memcpy(pdata->FromCode, _FromCode, sizeof(_FromCode));
			memcpy(pdata->ToCode, _ToCode, sizeof(_ToCode));
			memcpy(pdata->Passwd, _Passwd, sizeof(_Passwd));
			if (_pZgkSubLayer->SendMessage(msg1._parent.getMsgPtr(), msg1._parent.getLength()) != 0) {
				_pZgkSubLayer->SetEvent(ZgkSublayer::EV_ERROR);
				LOG(LMASK_ERROR, "SendRequest 11 SEND error", (_dataseq - 1));
				return -1;
			}
			LOG(LMASK_DEBUG, "SendRequest 1 passed");
			// LogicalAck待ちへ移行する
		}
				break;
		case 16: {	// データ電文
			unsigned textlen = _recintext * _reclen;
			unsigned remain = fsize - (_dataseq - 1) * _reclen;	//残りサイズ
			if (remain < textlen)
				textlen = remain;
			byte* pdata = msg1.makeTextData(textlen, 1);
			msg1.GetTTCPtr()->setseqno(_dataseq++);
			if (_fp == NULL) {
				if ((_fp = fopen(this->_filepath, "rb")) == NULL) {
					LOG(LMASK_ERROR, "SendRequest Open file error");
					_pZgkSubLayer->SetEvent(ZgkSublayer::EV_ERROR);
					return -1;
				}
			}
			fread(pdata, textlen, 1, _fp);

			if (_pZgkSubLayer->SendMessage(msg1._parent.getMsgPtr(), msg1._parent.getLength()) != 0) {
				_pZgkSubLayer->SetEvent(ZgkSublayer::EV_ERROR);
				LOG(LMASK_ERROR, "SendRequest 16 SEND error", (_dataseq - 1));
				return -1;
			}
			LOG(LMASK_DEBUG, "SendRequest 16 passed");
			// LogicalAck待ちへ移行する
		}
				 break;
		case 14: {	// 終了要求電文
			FileCtlT* pdata = (FileCtlT*)msg1.makeTextData(sizeof(FileCtlT), 0);
			msg1.GetTTCPtr()->forCtrl();
			pdata->initialize();
			pdata->ttype = 0x12;	//終了要求
			memcpy(pdata->Filenm, _Filenm, sizeof(_Filenm));
			memcpy(pdata->AccessKey, _AccessKey, sizeof(_AccessKey));
			unsigned reccount = _reccount;
			unsigned textcnt = _txtcount;
			pdata->setreccnt(reccount);
			pdata->setreclen(_reclen);
			pdata->settxtcnt(textcnt);

			if (_pZgkSubLayer->SendMessage(msg1._parent.getMsgPtr(), msg1._parent.getLength()) != 0) {
				_pZgkSubLayer->SetEvent(ZgkSublayer::EV_ERROR);
				LOG(LMASK_ERROR, "SendRequest 14 SEND error");
				return -1;
			}
			LOG(LMASK_DEBUG, "SendRequest 14 passed");
			// LogicalAck待ちへ移行する
		}
				 break;

		}
		return ret;
	}

	void SendFinished() {
		switch (_appstate) {
		case 1: {	// 初期ステート
			_appstate = 10;	// 開局回答待ちにする
			_pZgkSubLayer->CommReady();
		  }
	     break;
		case 10: {	// 開局回答待ち
			_appstate = 13;	// 開始応答待ちにする
			_pZgkSubLayer->CommReady();
		  }
		  break;
		case 13: {	//  開始応答待ち
			_appstate = 16;	// データ電文待ち
			_pZgkSubLayer->CommReady();
		  }
		 break;
		case 16: {	//データ送信
			if (_dataseq > _txtcount) {	// 全データ配信完了
				_appstate = 14;		// 終了要求
			}
			//データ出力指示イベント発生
			_pZgkSubLayer->SetEvent(ZgkSublayer::EV_E4);
		}
				 break;
		case 14: {	//終了要求回答待ち
			_pZgkSubLayer->CommReady();
		}
				 break;
		case 11: {	//閉局要求回答待ち
			_pZgkSubLayer->CommReady();
		}
				 break;
		}
	}

	int DataReceived() {
		ZgkSubLMsg* pmsg = _pZgkSubLayer->_dataque.front();
		_pZgkSubLayer->_dataque.pop();
		switch (_appstate) {
		case 10: {	// 開局応答
			ZgkAppMsg msg1, msgA;
			msgA.AttachMove(*pmsg);
			ComCtlT *pret = (ComCtlT *)msgA.GetDataPtr();
			if (pret->result != 0) {
				// 応答が正常でない場合こちら
				LOG(LMASK_ERROR, "S10 response says error. result = %d", pret->result);
				_pZgkSubLayer->SetEvent(ZgkSublayer::EV_ERROR);
				return -1;
			}
			//データ出力指示イベント発生
			_pZgkSubLayer->SetEvent(ZgkSublayer::EV_E4);

		  }
		  break;
		case 13: {	// 開始回答
			ZgkAppMsg msg1, msgA;
			msgA.AttachMove(*pmsg);
			ComCtlT *pret = (ComCtlT *)msgA.GetDataPtr();
			if (pret->result != 0) {
				LOG(LMASK_ERROR, "S13 response says error. result = %d", pret->result);
				// 応答が正常でない場合こちら
				_pZgkSubLayer->SetEvent(ZgkSublayer::EV_ERROR);
				return -1;
			}
			_appstate = 16;	// データ電文待ち
			_dataseq = 1;
			//データ出力指示イベント発生
			_pZgkSubLayer->SetEvent(ZgkSublayer::EV_E4);

		  }
			 break;
		case 14: {	// 終了要求回答待ち
			ZgkAppMsg msg1, msgA;
			msgA.AttachMove(*pmsg);
			ComCtlT *pret = (ComCtlT *)msgA.GetDataPtr();
			if (pret->result != 0) {
				LOG(LMASK_ERROR, "S14 response says error. result = %d", pret->result);
				// 応答が正常でない場合こちら
				_pZgkSubLayer->SetEvent(ZgkSublayer::EV_ERROR);
				return -1;
			}
			//!!!TODO ここでサイズチェックとか
			_appstate = 11;	// 閉局要求回答待ち
			//データ出力指示イベント発生
			_pZgkSubLayer->SetEvent(ZgkSublayer::EV_E4);

		}
				 break;
		case 11: {	//閉局要求応答待ち
			_pZgkSubLayer->SetEvent(ZgkSublayer::EV_QUIT);
		}
				 break;
		}
		delete pmsg;
		return 0;
	}

	void EventLoop() {
		_pZgkSubLayer->EventLoop();
	}
};

