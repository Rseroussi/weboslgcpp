#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <nlohmann/json.hpp> // Use a JSON library like nlohmann/json
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <base64.h> // Use a base64 library like cppcodec

using websocketpp::connection_hdl;
using client = websocketpp::client<websocketpp::config::asio_client>;

const std::string KEY_FILE_NAME = ".pylgtv";
const std::string USER_HOME = "HOME";
const std::string HANDSHAKE_FILE_NAME = "handshake.json";

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
    int commandCount = 0;

    std::string getKeyFilePath() const {
        const char* home = std::getenv(USER_HOME.c_str());
        if (home != nullptr) {
            return std::string(home) + "/" + KEY_FILE_NAME;
        }
        return "./" + KEY_FILE_NAME;
    }

    void loadKeyFile() {
        keyFilePath = getKeyFilePath();
        std::ifstream keyFile(keyFilePath);
        if (!keyFile) {
            std::cerr << "Failed to open key file: " << keyFilePath << std::endl;
            return;
        }

        Json::Value keyData;
        keyFile >> keyData;
        if (keyData.isMember(ip)) {
            clientKey = keyData[ip].asString();
        }
        keyFile.close();
    }

    void saveKeyFile() {
        if (clientKey.empty()) return;
        
        std::ofstream keyFile(keyFilePath, std::ios::trunc);
        if (!keyFile) {
            std::cerr << "Failed to save key file: " << keyFilePath << std::endl;
            return;
        }

        Json::Value keyData;
        keyData[ip] = clientKey;
        keyFile << keyData;
        keyFile.close();
    }

public:
    WebOsClient(const std::string& ip, const std::string& keyFilePath = "") 
        : ip(ip), port(3000), keyFilePath(keyFilePath) {
        loadKeyFile();
    }

    bool isRegistered() const {
        return !clientKey.empty();
    }

    void sendRegisterPayload(websocketpp::connection_hdl hdl) {
        // Load handshake JSON
        std::ifstream handshakeFile(HANDSHAKE_FILE_NAME);
        if (!handshakeFile) {
            std::cerr << "Failed to open handshake file: " << HANDSHAKE_FILE_NAME << std::endl;
            return;
        }

        Json::Value handshake;
        handshakeFile >> handshake;
        handshake["payload"]["client-key"] = clientKey;

        // Send JSON over WebSocket
        std::string jsonStr = handshake.toStyledString();
        wsClient.send(hdl, jsonStr, websocketpp::frame::opcode::text);
    }

    void registerClient() {
        try {
            wsClient.init_asio();
            wsClient.set_message_handler([this](websocketpp::connection_hdl hdl, client::message_ptr msg) {
                Json::Value response;
                std::istringstream(msg->get_payload()) >> response;
                if (response["type"].asString() == "registered") {
                    clientKey = response["payload"]["client-key"].asString();
                    saveKeyFile();
                }
            });

            std::string uri = "ws://" + ip + ":" + std::to_string(port);
            websocketpp::lib::error_code ec;
            auto con = wsClient.get_connection(uri, ec);
            if (ec) {
                std::cerr << "Failed to create connection: " << ec.message() << std::endl;
                return;
            }

            wsClient.connect(con);
            wsClient.run();

        } catch (const std::exception& e) {
            std::cerr << "Error during registration: " << e.what() << std::endl;
        }
    }

    void sendCommand(const std::string& requestType, const std::string& uri, const Json::Value& payload) {
        commandCount++;
        Json::Value message;
        message["id"] = requestType + "_" + std::to_string(commandCount);
        message["type"] = requestType;
        message["uri"] = "ssap://" + uri;
        message["payload"] = payload;

        // Send the message (handling WebSocket setup is omitted for brevity)
    }
};

// Example usage:
int main() {
    WebOsClient client("192.168.0.10");
    if (!client.isRegistered()) {
        client.registerClient();
    }

    // Further logic to send commands or interact with the TV...
    return 0;
}
