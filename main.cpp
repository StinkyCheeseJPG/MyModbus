#include "main.h"

int main() {
    // 讀取設定檔.
    settxt initSet("ini.txt");
    serverPort = initSet.getValue("Port", "505");
    serverIP = initSet.getValue("IP", "0.0.0.0");
    deviceRTU = initSet.getValue("RTUDevice", "/dev/pts/1");

    // 初始化 Modbus TCP.
    initModbusTCP(serverIP, atoi(serverPort.c_str()));
    if (!ctx_tcp) return -1;

    // 綁定 TCP 伺服器.
    int server_socket = modbus_tcp_listen(ctx_tcp, 5);
    if (server_socket == -1) {
        std::cerr << "Failed to bind server socket\n";
        cleanupModbus(ctx_tcp, server_socket);
        return -1;
    }
    std::cout << "Modbus TCP Server is running on port " << serverPort << " ...\n";

    // 初始化 Modbus RTU.
    initModbusRTU(deviceRTU, RTU_BAUDRATE_96);
    if (!ctx_rtu) {
        std::cerr << "Warning: Failed to initialize Modbus RTU.\n";
    }else {
        // 啟動 RTU 處理執行緒 ( 只有 RTU 成功時才執行 ).
        std::thread rtuThread(processTCPRequests);
        rtuThread.detach();
    }

    // 處理 TCP Requests.
    handleTCPRequests(ctx_tcp, server_socket);

    // 釋放資源.
    cleanupModbus(ctx_tcp, server_socket);
    if (ctx_rtu) cleanupModbus(ctx_rtu, -1);

    return 0;
}

void initModbusTCP(const std::string& ip, int port) {
    ctx_tcp = modbus_new_tcp(ip.c_str(), port);
    if (ctx_tcp == nullptr) {
        std::cerr << "Failed to create Modbus TCP Server\n";
    }

    mb_mapping = modbus_mapping_new(256, 256, 128, 128);

    // 設置測試數據.
    //mb_mapping->tab_bits[0] = 0;
    //mb_mapping->tab_input_bits[0] = 1;
    //mb_mapping->tab_registers[0] = 2244;
    //mb_mapping->tab_input_registers[0] = 3355;
}

void initModbusRTU(const std::string& device, int baudrate) {
    ctx_rtu = modbus_new_rtu(device.c_str(), baudrate, 'N', 8, 1);
    if (ctx_rtu == nullptr) {
        std::cerr << "Failed to create Modbus RTU context\n";
    }
    
    if (modbus_connect(ctx_rtu) == -1) {
        std::cerr << "Failed to connect to RTU device\n";
        modbus_free(ctx_rtu);
        ctx_rtu = nullptr;
    }

    modbus_set_response_timeout(ctx_rtu, 1, 0);
    mb_RTU_mapping = modbus_mapping_new(32, 0, 256, 0);
}

void cleanupModbus(modbus_t* ctx, int socket) {
    if (ctx) {
        modbus_close(ctx);
        modbus_free(ctx);
    }

    if (socket != -1) {
#ifdef _WIN32
        closesocket(socket);
#else
        close(socket);
#endif
    }
}

void handleTCPRequests(modbus_t* ctx_tcp, int server_socket) {
    while (true) {
        int client_socket = modbus_tcp_accept(ctx_tcp, &server_socket);
        if (client_socket == -1) {
            std::cerr << "Failed to accept client\n";
            continue;
        }

        uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
        while (true) {
            int rc = modbus_receive(ctx_tcp, query);
            if (rc == -1) {
                std::cerr << "Modbus TCP receive error: " << modbus_strerror(errno) << "\n";
                break;
            }

            int tmpslaveID = query[6];
            int function_code = query[7];

            // 將 TCP Request 加入 Queue(心跳確認直接回應).
            if (tmpslaveID == 0){
                modbus_reply(ctx_tcp, query, rc, mb_mapping);
            }
            else {
                std::lock_guard<std::mutex> lock(queueMutex);
                QestPack tmpquest;
                tmpquest.request = std::vector<uint8_t>(query, query + rc);
                tmpquest.length = rc;
                requestQueue.push(tmpquest);
            }
        }
    }
}

void processTCPRequests() {
    if (!ctx_rtu) {
        std::cerr << "RTU context is null. Skipping RTU processing.\n";
        return;
    }

    while (true) {
        QestPack tmpquest;

        // 檢查是否有 TCP Request.
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!requestQueue.empty()) {
                tmpquest = requestQueue.front();
                requestQueue.pop();
            }
        }    

        if (!tmpquest.request.empty()) {
            sendRTURequest(ctx_rtu, tmpquest.request, tmpquest.length);
        }

#ifdef __unix__ 
        usleep(50000);  // 避免過度消耗 CPU.
#elif defined(_WIN32) || defined(WIN32)    
        Sleep(50);
#endif
    }
}

bool sendRTURequest(modbus_t* ctx_rtu, const std::vector<uint8_t>& request, int request_size) {
    modbus_set_slave(ctx_rtu, request[6]);
    std::vector<uint8_t> rtu_request(request.begin() + 6, request.end());  // 移除 TCP Modbus 傳輸內容的 Header.
    int rtu_rc = modbus_send_raw_request(ctx_rtu, rtu_request.data(), rtu_request.size());
    if (rtu_rc == -1) {
        std::cerr << "Failed to send RTU request\n";
        return false;
    }

    // 等待 RTU 回應.
    uint8_t response[MODBUS_RTU_MAX_ADU_LENGTH];
    int response_length = modbus_receive_confirmation(ctx_rtu, response);
    if (response_length == -1) {
        std::cerr << "No response from RTU device\n";
        return false;
    }  

    if (response[1] == MODBUS_FC_READ_HOLDING_REGISTERS) {
        std::fill(mb_mapping->tab_registers, mb_mapping->tab_registers + 256, 0);
        int num_bytes = response[2];  // 第3個位元組表示回傳的數據長度（byte 數）.

        // 計算有多少個 16-bit 寄存器（每個寄存器佔 2 Bytes）.
        int num_registers = num_bytes / 2;

        // 解析回應數據，存入 mb_RTU_mapping->tab_registers.
        for (int i = 0; i < num_registers; i++) {
            mb_RTU_mapping->tab_registers[i] =
                (response[3 + i * 2] << 8) | response[4 + i * 2];  // 高位 + 低位
        }
        if (modbus_reply(ctx_tcp, request.data(), request_size, mb_RTU_mapping) == -1) {
            std::cerr << "Failed to send TCP response\n";
            return false;
        }
        return true;
    }

    if(modbus_reply(ctx_tcp, request.data(), request_size, mb_mapping) == -1) {
        std::cerr << "Failed to send TCP response\n";
        return false;
    }
    return true;
}
