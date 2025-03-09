#pragma once

#ifdef __unix__ 
	#define OS_Windows 0
	#include <unistd.h>

#elif defined(_WIN32) || defined(WIN32)
	#define OS_Windows 1
	#include <io.h>
	#include <process.h>
	#include <winsock2.h>
	#pragma comment(lib, "ws2_32.lib")  // 連結 Winsock 函式庫.
#endif

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <queue>
#include <mutex>

#include "modbus.h"
#include "settxt.h"

// TCP RTU 連線全域變數.
modbus_t* ctx_rtu;
modbus_t* ctx_tcp;
modbus_mapping_t* mb_mapping;
modbus_mapping_t* mb_RTU_mapping;

// 文本設定檔.
string serverIP;
string serverPort;
string deviceRTU;

// 多緒處理.
struct requestPack
{
	std::vector<uint8_t> request;
	int length;
} typedef QestPack;
std::queue<requestPack> requestQueue;  // 儲存 TCP Client 請求.
std::mutex queueMutex;


void initModbusTCP(const std::string& ip, int port);				// 初始化 Modbus TCP 伺服器.
void initModbusRTU(const std::string& device, int baudrate);		// 初始化 Modbus RTU.
void cleanupModbus(modbus_t* ctx, int socket);						// 釋放資源.
void handleTCPRequests(modbus_t* ctx_tcp, int server_socket);		// TCP 請求處理函式.
bool sendRTURequest(modbus_t* ctx_rtu, const std::vector<uint8_t>& request, int request_size);	// 發送 RTU 請求.
void processTCPRequests();

enum RTU_BAUDRATE
{
	RTU_BAUDRATE_96 = 9600,
	RTU_BAUDRATE_19 = 19200,
	RTU_BAUDRATE_38 = 38400,
	RTU_BAUDRATE_115 = 115200,
};

