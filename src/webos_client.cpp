#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp> // Use a JSON library like nlohmann/json
#include <endpoints.h>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>

#include <cppcodec/base32_crockford.hpp>
#include <cppcodec/base64_rfc4648.hpp>

using websocketpp::connection_hdl;
typedef websocketpp::client<websocketpp::config::asio_client> client;

const std::string KEY_FILE_NAME = ".pylgtv";
const std::string USER_HOME = "HOME";
const std::string HANDSHAKE_FILE_NAME = "handshake.json";

using json = nlohmann::json;
using base64 = cppcodec::base64_rfc4648;
using base32 = cppcodec::base32_crockford;

using namespace std::this_thread;     // sleep_for, sleep_until
using namespace std::chrono_literals; // ns, us, ms, s, h, etc.
using std::chrono::system_clock;

// Exception class for pairing errors
class PyLGTVPairException : public std::exception {
public:
    std::string id;
    std::string message;

    PyLGTVPairException(std::string id, std::string message) : id(id), message(message) {}
    const char* what() const noexcept override { return message.c_str(); }
};

// WebOSClient class
class WebOsClient {
private:
    std::string ip;
    int port;
    std::string keyFilePath;
    std::string clientKey;
    client wsClient;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> m_thread;
    client::connection_ptr con;
    int commandCount = 0;
    json lastResponse;

    std::string getKeyFilePath() const {
        const char* home = std::getenv(USER_HOME.c_str());
        if (home != nullptr) {
            return std::string(home) + "/" + KEY_FILE_NAME;
        }
        return "./" + KEY_FILE_NAME;
    }

    void loadKeyFile() {
        std::string keyfilep;
        clientKey = "";
        if (!keyFilePath.empty()) {
            keyfilep = keyFilePath;
        } else {
            keyfilep = getKeyFilePath();
        }

        
        std::ifstream keyFile(keyfilep);
        if (keyFile.fail()) {
            std::cerr << "Failed to open key file: " << keyfilep << std::endl;
            return;
        }

        json keyData;
        keyFile >> keyData;
        if (keyData.contains(ip)) {
            clientKey = keyData[ip].get<std::string>();
        }
        keyFile.close();
    }

    void saveKeyFile() {
        if (clientKey.empty()) return;

        std::string keyfilep;
        

        if (!keyFilePath.empty()) {
            keyfilep = keyFilePath;
        } else {
            keyfilep = getKeyFilePath();
        }
        std::ifstream keyFilein(keyfilep);
        
        
        if (keyFilein.fail()) {
            std::cerr << "Failed to open key file: " << keyfilep << std::endl;
            return;
        }

        json keyData;
        keyFilein >> keyData;
        keyFilein.close();

        std::ofstream keyFileout(keyfilep, std::ios::trunc);
        if(keyFileout.fail()) {
            std::cerr << "Failed to open key file: " << keyfilep << std::endl;
            return;
        }
        
        keyData[ip] = clientKey;
        keyFileout << keyData.dump();
        keyFileout.close();
    }

public:
    WebOsClient(const std::string& ip, const std::string& keyFilePath = "") 
        : ip(ip), port(3000), keyFilePath(keyFilePath) {
        wsClient.clear_access_channels(websocketpp::log::alevel::all);
        wsClient.clear_error_channels(websocketpp::log::elevel::all);
        wsClient.init_asio();
        wsClient.start_perpetual();

        m_thread.reset(new websocketpp::lib::thread(&client::run, &wsClient));

        loadKeyFile();
    }

    bool isRegistered() const {
        return !clientKey.empty();
    }

    void sendRegisterPayload(websocketpp::connection_hdl hdl) {
        // Load handshake json
        const char* home = std::getenv(USER_HOME.c_str());
        std::string handshakefilep = std::string(home) + "/" + HANDSHAKE_FILE_NAME;
        
        
        std::ifstream handshakeFile(handshakefilep);
        
        if (!handshakeFile) {
            std::cerr << "Failed to open handshake file: " << HANDSHAKE_FILE_NAME << std::endl;
            return;
        }

        json handshake;
        handshakeFile >> handshake;
        handshake["payload"]["client-key"] = clientKey;
        printf("sending register payload: %s\n", clientKey.c_str());
        // Send JSON over WebSocket
        std::string jsonStr = handshake.dump();
        wsClient.send(hdl, jsonStr, websocketpp::frame::opcode::text);
        printf("sent register payload\n");

    }

    void _registerClient() {
        try {
                // wsClient.set_message_handler([this](websocketpp::connection_hdl hdl, client::message_ptr msg) {
                // json response;
                // std::istringstream(msg->get_payload()) >> response;
                // if (response["type"].get<std::string>() == "registered") {
                //     clientKey = response["payload"]["client-key"].get<std::string>();
                //     saveKeyFile();
                //     std::cout << "saved key file" << std::endl;
                // }
            // });

            std::string uri = "ws://" + ip + ":" + std::to_string(port);
            websocketpp::lib::error_code ec;
            con = wsClient.get_connection(uri, ec);
            if (ec) {
                std::cerr << "Failed to create connection: " << ec.message() << std::endl;
                return;
            }
            con->set_open_handler([this](websocketpp::connection_hdl hdl) {
                sendRegisterPayload(hdl);
            });
            con->set_fail_handler([this](websocketpp::connection_hdl hdl) {
                std::cerr << "Failed to connect to TV" << std::endl;
            });
            con->set_message_handler([this](websocketpp::connection_hdl hdl, client::message_ptr msg) {
                json response;
                std::istringstream(msg->get_payload()) >> response;
                if (response["type"].get<std::string>() == "registered") {
                    clientKey = response["payload"]["client-key"].get<std::string>();
                    saveKeyFile();
                    std::cout << "saved key file" << std::endl;
                } else {
                    std::cout << "Received message: " << response.dump() << std::endl;
                }
                lastResponse = response;
            });
            wsClient.connect(con);
            printf("Connected to TV\n");
            sleep_for(5s);
            sendRegisterPayload(con->get_handle());
            printf("Sent register payload\n");

            

        } catch (const std::exception& e) {
            std::cerr << "Error during registration: " << e.what() << std::endl;
        }
    }

    void registerClient() {
        int attempts = 0;
        while(clientKey.empty()) {
            _registerClient();
            if (attempts++ > 5) {
                throw PyLGTVPairException("pairing-failed", "Failed to pair with TV");
                return;
            }
            sleep_for(100ms);
            
        }
        std::cout << "Successfully paired with TV" << std::endl;
    }

    void _sendCommand(const json& msg) {
        commandCount++;
        // json message;
        // message["id"] = requestType + "_" + std::to_string(commandCount);
        // message["type"] = requestType;
        // message["uri"] = "ssap://" + uri;
        // message["payload"] = payload;

        // Send JSON over WebSocket
        std::string jsonStr = msg.dump();
        wsClient.send(con->get_handle(), jsonStr, websocketpp::frame::opcode::text);
    }

    void sendCommand(const std::string& requestType, const std::string& uri, const json& payload) {
        json message;
        message["id"] = requestType + "_" + std::to_string(commandCount);
        message["type"] = requestType;
        message["uri"] = "ssap://" + uri;
        message["payload"] = payload;
        lastResponse = json();
        _sendCommand(message);
    }

    void request(const std::string& uri, const json& payload) {
        sendCommand("request", uri, payload);
    }

    void send_message(std::string message) {
        json payload;
        payload["message"] = message;
        payload["iconData"] = "";
        payload["iconExtension"] = "";

        request(EP_SHOW_MESSAGE, payload);
    }
};

// Example usage:
int main() {
    WebOsClient client("192.168.86.41");
    printf("Client created\n");
    if (!client.isRegistered()) {
        client.registerClient();
    }

    //send a command to the tv
    printf("Sending message\n");
    client.send_message("Hello, World!");

    // Further logic to send commands or interact with the TV...
    return 0;
}
