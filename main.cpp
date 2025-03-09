#include "main.h"

int main() {
    // Ū���]�w��.
    settxt initSet("ini.txt");
    serverPort = initSet.getValue("Port", "505");
    serverIP = initSet.getValue("IP", "0.0.0.0");
    deviceRTU = initSet.getValue("RTUDevice", "/dev/pts/1");

    // ��l�� Modbus TCP.
    initModbusTCP(serverIP, atoi(serverPort.c_str()));
    if (!ctx_tcp) return -1;

    // �j�w TCP ���A��.
    int server_socket = modbus_tcp_listen(ctx_tcp, 5);
    if (server_socket == -1) {
        std::cerr << "Failed to bind server socket\n";
        cleanupModbus(ctx_tcp, server_socket);
        return -1;
    }
    std::cout << "Modbus TCP Server is running on port " << serverPort << " ...\n";

    // ��l�� Modbus RTU.
    initModbusRTU(deviceRTU, RTU_BAUDRATE_96);
    if (!ctx_rtu) {
        std::cerr << "Warning: Failed to initialize Modbus RTU.\n";
    }else {
        // �Ұ� RTU �B�z����� ( �u�� RTU ���\�ɤ~���� ).
        std::thread rtuThread(processTCPRequests);
        rtuThread.detach();
    }

    // �B�z TCP Requests.
    handleTCPRequests(ctx_tcp, server_socket);

    // ����귽.
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

    // �]�m���ռƾ�.
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

            // �N TCP Request �[�J Queue(�߸��T�{�����^��).
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

        // �ˬd�O�_�� TCP Request.
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
        usleep(50000);  // �קK�L�׮��� CPU.
#elif defined(_WIN32) || defined(WIN32)    
        Sleep(50);
#endif
    }
}

bool sendRTURequest(modbus_t* ctx_rtu, const std::vector<uint8_t>& request, int request_size) {
    modbus_set_slave(ctx_rtu, request[6]);
    std::vector<uint8_t> rtu_request(request.begin() + 6, request.end());  // ���� TCP Modbus �ǿ餺�e�� Header.
    int rtu_rc = modbus_send_raw_request(ctx_rtu, rtu_request.data(), rtu_request.size());
    if (rtu_rc == -1) {
        std::cerr << "Failed to send RTU request\n";
        return false;
    }

    // ���� RTU �^��.
    uint8_t response[MODBUS_RTU_MAX_ADU_LENGTH];
    int response_length = modbus_receive_confirmation(ctx_rtu, response);
    if (response_length == -1) {
        std::cerr << "No response from RTU device\n";
        return false;
    }  

    if (response[1] == MODBUS_FC_READ_HOLDING_REGISTERS) {
        std::fill(mb_mapping->tab_registers, mb_mapping->tab_registers + 256, 0);
        int num_bytes = response[2];  // ��3�Ӧ줸�ժ�ܦ^�Ǫ��ƾڪ��ס]byte �ơ^.

        // �p�⦳�h�֭� 16-bit �H�s���]�C�ӱH�s���� 2 Bytes�^.
        int num_registers = num_bytes / 2;

        // �ѪR�^���ƾڡA�s�J mb_RTU_mapping->tab_registers.
        for (int i = 0; i < num_registers; i++) {
            mb_RTU_mapping->tab_registers[i] =
                (response[3 + i * 2] << 8) | response[4 + i * 2];  // ���� + �C��
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
