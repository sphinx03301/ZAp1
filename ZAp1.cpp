// ZAp1.cpp : アプリケーションのエントリ ポイントを定義します。
//
#ifdef _WIN32
#include <winsock.h>
#endif
#include <stdio.h>
#include "Zgk.h"
#include "inifile.h"
using namespace std;
using namespace inifile;

int main(int narg, char *args[])
{
#ifdef	_WIN32
	const char *pszPath = ".\\ZAp1.ini";
#else
	const char *pszPath = "./ZAp1.ini";
#endif
	if (narg > 1)
		pszPath = args[1];
	IniFile inif;
	try { inif.load(pszPath); }
	catch (...) {
		printf("Couldn't load inifile\r\n");
		return -1;
	}
	
	int port = inif["Host"]["Port"].as<int>();
	int  count = inif["Host"]["Count"].as<int>();

	if (count < 1)
		count = 1;

	const char*  addr = inif["Host"]["Addr"].as<const char*>();
	const char*  ToCode = inif["Host"]["ToCode"].as<const char*>();
	const char*  FromCode = inif["Host"]["FromCode"].as<const char*>();
	const char*  Passwd = inif["Host"]["Passwd"].as<const char*>();
	const char*  Filenm = inif["File"]["Name"].as<const char*>();
	const char*  AccsKey = inif["File"]["AccsKey"].as<const char*>();
	const char*  filepath = inif["File"]["Path"].as<const char*>();


#ifdef _WIN32	// WIndowsでソケット使用の為必須
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 0), &wsaData);
#endif



	byte szToCode[7], szFromCode[7], szPasswd[6],
		szFName[20], szAccsKey[10];
	atobin(szToCode, ToCode);
	atobin(szFromCode, FromCode);
	atobin(szPasswd, Passwd);
	atobin(szFName, Filenm);
	atobin(szAccsKey, AccsKey);
	vector<std::thread*> thvec;

	for (int i = 0; i < count; i++) {
		std::thread *pt = new std::thread([](const char*addr, int port, byte*_szToCode, byte*_szFromCode,
			byte*szPasswd, byte*szFName, byte*szAccsKey, const char* filepath, int idx) {

			byte szToCode[7], szFromCode[7];
			for (int i = 0; i < sizeof(szToCode); i++) {
				szToCode[i] = _szToCode[i];
				szFromCode[i] = _szFromCode[i];
			}
			Add2BIVal(szToCode, sizeof(szToCode), idx);
			Add2BIVal(szFromCode, sizeof(szFromCode), idx);
			ZgkClient client;
			client.SetConnectInfo(addr, port);
			client.SetCenterCodes(szToCode, szFromCode);
			client.SetPassword(szPasswd);
			client.SetFileInfo(szFName, szAccsKey, filepath);

			client.StartConnection();
			client.EventLoop();

		}, addr, port, szToCode, szFromCode, szPasswd, szFName, szAccsKey, filepath, i);
		thvec.push_back(pt);
	}
	for (int i = 0; i < thvec.size(); i++) {
		std::thread *pt = thvec.at(i);
		pt->join();
		delete pt;
	}

}

