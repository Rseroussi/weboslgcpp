#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <filesystem>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp> // Use a JSON library like nlohmann/json
#include <endpoints.h>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>

// #include <cppcodec/base32_crockford.hpp>
// #include <cppcodec/base64_rfc4648.hpp>

#define WFR_SLEEP_TIME_MS 200000 //the sleep time in microseconds to wait for a response from the tv until checking again for a response. This is used with WAIT_FOR_RESPONSE. 
#define WAIT_FOR_RESPONSE 10 //this is used with WFR_SLEEP_TIME_MS to wait for a response from the tv. So the max time you will wait is WAIT_FOR_RESPONSE * WFR_SLEEP_TIME_MS after registration

#define REGISTRATION_TIMEOUT 20 //the max time to wait for registration in seconds
#define REGISTRATION_RETRY_INTERVAL 5 //the time to wait before retrying registration in seconds
using websocketpp::connection_hdl;
typedef websocketpp::client<websocketpp::config::asio_client> client;

const std::string KEY_FILE_NAME = ".cpplgtv";
const std::string USER_HOME = "HOME";
const std::string HANDSHAKE_FILE_NAME = "handshake.json";

using json = nlohmann::json;
// using base64 = cppcodec::base64_rfc4648;
// using base32 = cppcodec::base32_crockford;

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
    std::condition_variable response_cv;
    std::mutex response_mutex;
    volatile bool response_ready = false;

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

    //to use this function you must set lastResponse to empty before calling it. you need to set this before you send the request
    void wait_for_response() {
        std::unique_lock<std::mutex> lock(response_mutex);
        response_cv.wait(lock, [this] { return response_ready; });
        response_ready = false;
        // int wait = 0;
        // while (lastResponse.empty() || wait++ < WAIT_FOR_RESPONSE) {
        //     usleep(WFR_SLEEP_TIME_MS);
        // }
        // std::cout << lastResponse.dump() << std::endl;
    }

public:
    WebOsClient(const std::string& ip, const std::string& keyFilePathi = "") 
        : ip(ip), port(3000), keyFilePath(keyFilePathi) {
        wsClient.clear_access_channels(websocketpp::log::alevel::all);
        wsClient.clear_error_channels(websocketpp::log::elevel::all);
        wsClient.init_asio();
        wsClient.start_perpetual();
        
        m_thread.reset(new websocketpp::lib::thread(&client::run, &wsClient));

        loadKeyFile();
        if(keyFilePathi.empty()) {
            keyFilePath = getKeyFilePath();
        }
        std::ofstream keyFile(keyFilePath);
        keyFile << "{}";
        keyFile.close();
        
    }

    bool isRegistered() const {
        return !clientKey.empty();
    }
    ~WebOsClient() {
        wsClient.stop_perpetual();
        wsClient.stop();
        m_thread->join();
        std::filesystem::remove(keyFilePath);
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
        //wait for response from register payload
        // sleep_for(1s);
        // wait_for_response();
        // printf("sent register payload and recieved response\n");

    }

    void _registerClient() {
        try {

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
                } 
                
                // else {
                //     std::cout << "Received message: " << response.dump() << std::endl;
                // }
                //lock the mutex and set the response
                {
                    std::lock_guard<std::mutex> lock(response_mutex);
                    lastResponse = response;
                    response_ready = true;
                    std::cout << "response ready" << std::endl;
                }
                response_cv.notify_one();
            });

            wsClient.connect(con);
            printf("Connected to TV\n");
            
            
            sendRegisterPayload(con->get_handle());

            // sleep_for(1s);
            // //wait for registration response
            // while(clientKey.empty()) {
            //     sleep_for(200ms);
            // }

            // printf("Sent register payload\n");

            

        } catch (const std::exception& e) {
            std::cerr << "Error during registration: " << e.what() << std::endl;
        }
    }

    void registerClient() {
        int attempts = 0;
        while(clientKey.empty()) {
            
            if (attempts++ % REGISTRATION_RETRY_INTERVAL == 0) {
                _registerClient();
            } else if(attempts > REGISTRATION_TIMEOUT) {
                throw PyLGTVPairException("pairing-failed", "Failed to pair with TV");
                return;
            }
            sleep_for(1s);
            
        }
        
        std::cout << "Successfully paired with TV" << std::endl;
    }

    void _sendCommand(const json& msg) {
        commandCount++;

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

    json wait_for_request(const std::string& uri, const json& payload) {
        lastResponse = json();
        response_ready = false;
        std::thread t1(&WebOsClient::request, this, uri, payload);
        std::thread t2(&WebOsClient::wait_for_response, this);
        // wait_for_response();
        // lastResponse = json();
        // request(uri, payload);
        t1.join();
        t2.join();
        return lastResponse;
    }

    void send_message(std::string message) {
        json payload;
        payload["message"] = message;
        payload["iconData"] = "";
        payload["iconExtension"] = "";

        request(EP_SHOW_MESSAGE, payload);
    }

    //Apps
    json response() {
        return lastResponse;
    }


    json listApps() {
        
        json output = wait_for_request(EP_GET_APPS, json());
        
        return output["payload"]["launchPoints"];
    }

    json getCurrentApp() {
        json output = wait_for_request(EP_GET_CURRENT_APP_INFO, json());
        return output["payload"]["appId"];
    }

    void launchApp(const std::string& appId) {
        json payload;
        payload["id"] = appId;
        //have to wait for response if you want your app request to work
        wait_for_request(EP_LAUNCH_APP, payload);
    }

    void launchAppWithParams(const std::string& appId, const json& params) {
        json payload;
        payload["id"] = appId;
        payload["params"] = params;
        wait_for_request(EP_LAUNCH, payload);
    }

    void launchAppWithContentID(const std::string& appId, const std::string& contentId) {
        json payload;
        payload["id"] = appId;
        payload["contentId"] = contentId;
        wait_for_request(EP_LAUNCH, payload);
    }

    void closeApp(const std::string& appId) {
        json payload;
        payload["id"] = appId;
        wait_for_request(EP_LAUNCHER_CLOSE, payload);
    }

    //services
    json listServices() {
        
        json output = wait_for_request(EP_GET_SERVICES, json());
       
        return output["payload"]["services"];
    }

    json getSoftwareInfo() {
        
        json output = wait_for_request(EP_GET_SOFTWARE_INFO, json());
        
        return output["payload"];
    }

    void powerOff() {
        request(EP_POWER_OFF, json());
    }
    
    void powerOn() {
        request(EP_POWER_ON, json());
    }

    //3d mode
    void turn3dOn() {
        request(EP_3D_ON, json());
    }
    void turn3dOff() {
        request(EP_3D_OFF, json());
    }
    
    //Inputs
    json listInputs() {
        
        json output = wait_for_request(EP_GET_INPUTS, json());

        return output["payload"]["devices"];
    }

    json getInput() {
        return getCurrentApp();
    }

    void setInput(const std::string& inputId) {
        json payload;
        payload["inputId"] = inputId;
        request(EP_SET_INPUT, payload);
    }

    //audio

    json getVolume() {
       
        json output = wait_for_request(EP_GET_VOLUME, json());

        return output["payload"];
    }

    void setVolume(int volume) {
        json payload;
        payload["volume"] = volume;
        request(EP_SET_VOLUME, payload);
    }

    void volumeUp() {
        request(EP_VOLUME_UP, json());
    }

    void volumeDown() {
        request(EP_VOLUME_DOWN, json());
    }

    //channels
    json getChannels() {
        
        json output = wait_for_request(EP_GET_TV_CHANNELS, json());
        
        return output["payload"]["channelList"];
    }

    json getCurrentChannel() {
        
        json output = wait_for_request(EP_GET_CURRENT_CHANNEL, json());
        
        return output["payload"];
    }

    void setChannel(const std::string& channelId) {
        json payload;
        payload["channelId"] = channelId;
        request(EP_SET_CHANNEL, payload);
    }

    json getChannelInfo(const std::string& channelId) {
        json payload;
        payload["channelId"] = channelId;
        
        json output = wait_for_request(EP_GET_CHANNEL_INFO, payload);
        
        return output["payload"];
    }

    void channelUp() {
        request(EP_TV_CHANNEL_UP, json());
    }

    void channelDown() {
        request(EP_TV_CHANNEL_DOWN, json());
    }

    //media
    void play() {
        request(EP_MEDIA_PLAY, json());
    }
    void pause() {
        request(EP_MEDIA_PAUSE, json());
    }
    void stop() {
        request(EP_MEDIA_STOP, json());
    }
    void rewind() {
        request(EP_MEDIA_REWIND, json());
    }
    void fastForward() {
        request(EP_MEDIA_FAST_FORWARD, json());
    }
    void close() {
        request(EP_MEDIA_CLOSE, json());
    }

    //keys
    void sendEnter() {
        request(EP_SEND_ENTER, json());
    }

    void sendDelete() {
        request(EP_SEND_DELETE, json());
    }

    //web
    void openURL(const std::string& url) {
        json payload;
        payload["target"] = url;
        request(EP_OPEN, payload);
    }

    void closeWebApp() {
        request(EP_CLOSE_WEB_APP, json());
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
    // client.powerOn();
    // printf("Sending message\n");
    // client.send_message("Hello, World!");

    for(int i = 0; i < 6 ; i++) {
        client.volumeUp();
        sleep_for(500ms);
    }
    // client.launchApp("youtube.leanback.v4");
    json apps = client.listApps();
    std::cout << apps << std::endl;
    client.launchApp("youtube.leanback.v4");

    // Further logic to send commands or interact with the TV...
    return 0;
}
