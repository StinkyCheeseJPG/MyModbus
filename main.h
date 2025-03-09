#pragma once

#ifdef __unix__ 
	#define OS_Windows 0
	#include <unistd.h>

#elif defined(_WIN32) || defined(WIN32)
	#define OS_Windows 1
	#include <io.h>
	#include <process.h>
	#include <winsock2.h>
	#pragma comment(lib, "ws2_32.lib")  // �s�� Winsock �禡�w.
#endif

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <queue>
#include <mutex>

#include "modbus.h"
#include "settxt.h"

// TCP RTU �s�u�����ܼ�.
modbus_t* ctx_rtu;
modbus_t* ctx_tcp;
modbus_mapping_t* mb_mapping;
modbus_mapping_t* mb_RTU_mapping;

// �奻�]�w��.
string serverIP;
string serverPort;
string deviceRTU;

// �h���B�z.
struct requestPack
{
	std::vector<uint8_t> request;
	int length;
} typedef QestPack;
std::queue<requestPack> requestQueue;  // �x�s TCP Client �ШD.
std::mutex queueMutex;


void initModbusTCP(const std::string& ip, int port);				// ��l�� Modbus TCP ���A��.
void initModbusRTU(const std::string& device, int baudrate);		// ��l�� Modbus RTU.
void cleanupModbus(modbus_t* ctx, int socket);						// ����귽.
void handleTCPRequests(modbus_t* ctx_tcp, int server_socket);		// TCP �ШD�B�z�禡.
bool sendRTURequest(modbus_t* ctx_rtu, const std::vector<uint8_t>& request, int request_size);	// �o�e RTU �ШD.
void processTCPRequests();

enum RTU_BAUDRATE
{
	RTU_BAUDRATE_96 = 9600,
	RTU_BAUDRATE_19 = 19200,
	RTU_BAUDRATE_38 = 38400,
	RTU_BAUDRATE_115 = 115200,
};

