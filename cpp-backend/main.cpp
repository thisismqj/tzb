#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <iostream>
#include <string>
#include <set>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdlib>    // For std::getenv
#include <cstring>    // For strcmp
#include <json/json.h>  // JSON-C++ library
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <curl/curl.h>

typedef websocketpp::server<websocketpp::config::asio> WsServer;

// API type enumeration
enum ApiType {
    API_DOUBAO,
    API_DEEPSEEK
};

// Callback structure for CURL
struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        fprintf(stderr, "Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

class ChatServer {
public:
    ChatServer(ApiType apiType = API_DOUBAO) : m_apiType(apiType) {
        // Initialize server
        m_server.init_asio();
        m_server.clear_access_channels(websocketpp::log::alevel::all);
        m_server.set_access_channels(websocketpp::log::alevel::connect | websocketpp::log::alevel::disconnect);

        // Set handlers
        m_server.set_open_handler(std::bind(&ChatServer::onOpen, this, std::placeholders::_1));
        m_server.set_close_handler(std::bind(&ChatServer::onClose, this, std::placeholders::_1));
        m_server.set_message_handler(std::bind(&ChatServer::onMessage, this, std::placeholders::_1, std::placeholders::_2));

        // Get API keys from environment
        const char* doubaoApiKey = std::getenv("ARK_API_KEY");
        if (doubaoApiKey) {
            m_doubaoApiKey = std::string(doubaoApiKey);
            std::cout << "ARK_API_KEY loaded from environment" << std::endl;
        } else {
            std::cerr << "Warning: ARK_API_KEY environment variable not set" << std::endl;
        }

        const char* deepseekApiKey = std::getenv("DEEPSEEK_API_KEY");
        if (deepseekApiKey) {
            m_deepseekApiKey = std::string(deepseekApiKey);
            std::cout << "DEEPSEEK_API_KEY loaded from environment" << std::endl;
        } else {
            std::cerr << "Warning: DEEPSEEK_API_KEY environment variable not set" << std::endl;
        }

        const char* systemPrompt = std::getenv("DEEPSEEK_SYSTEM_PROMPT");
        if (systemPrompt) {
            m_deepseekSystemPrompt = std::string(systemPrompt);
            std::cout << "DEEPSEEK_SYSTEM_PROMPT loaded from environment" << std::endl;
        } else {
            // 设置默认系统提示（可根据需要修改）
            m_deepseekSystemPrompt = "You are a helpful assistant.";
            std::cout << "Using default system prompt: " << m_deepseekSystemPrompt << std::endl;
        }

        // Initialize CURL
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~ChatServer() {
        stop();
        curl_global_cleanup();
    }

    void run(uint16_t port) {
        m_server.listen(port);
        m_server.start_accept();
        std::cout << "Server started on port " << port << " with API: " 
                  << (m_apiType == API_DOUBAO ? "Doubao" : "DeepSeek") << std::endl;
        m_server.run();
    }

    void stop() {
        std::cout << "Stopping server..." << std::endl;

        // Close all client connections
        for (auto hdl : m_clients) {
            m_server.close(hdl, websocketpp::close::status::going_away, "Server shutting down");
        }
        m_clients.clear();

        // Stop the server
        if (m_server.is_listening()) {
            m_server.stop_listening();
        }

        // Stop the io_service
        m_server.stop();

        std::cout << "Server stopped" << std::endl;
    }

private:
    void onOpen(websocketpp::connection_hdl hdl) {
        std::cout << "Client connected" << std::endl;
        m_clients.insert(hdl);
    }

    void onClose(websocketpp::connection_hdl hdl) {
        std::cout << "Client disconnected" << std::endl;
        m_clients.erase(hdl);
    }

    void onMessage(websocketpp::connection_hdl hdl, WsServer::message_ptr msg) {
        try {
            // Parse incoming JSON message
            Json::Value root;
            Json::Reader reader;
            reader.parse(msg->get_payload(), root);

            std::string type = root["type"].asString();
            std::string content = root["content"].asString();
            std::string model = root["model"].asString(); // 新增：解析模型

            // 根据模型设置 API 类型
            ApiType apiType = API_DEEPSEEK; // 默认
            if (model == "doubao") {
                apiType = API_DOUBAO;
            } else if (model == "deepseek") {
                apiType = API_DEEPSEEK;
            }

            std::cout << "Received " << type << " message from client, model: " << model << std::endl;

            // Process message and generate response
            processMessage(hdl, type, content, apiType);

        } catch (const std::exception& e) {
            std::cerr << "Error parsing message: " << e.what() << std::endl;
        }
    }

    void processMessage(websocketpp::connection_hdl hdl, const std::string& type, const std::string& content, ApiType apiType) {
        Json::Value response;

        if (type == "text") {
            std::cout << "Text message: " << content << std::endl;

            // Check if user wants to generate an image (Doubao only)
            // Support commands: "/image <prompt>" or "生成图片：<prompt>"
            if (isImageGenerationRequest(content)) {
                // If Doubao API is selected, generate image; otherwise, inform user
                if (apiType == API_DOUBAO) {
                    std::string prompt = extractImagePrompt(content);
                    
                    if (prompt.empty()) {
                        response["type"] = "text";
                        response["content"] = "请提供图片描述，例如：/image 一只可爱的猫咪";
                        response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
                        sendMessage(hdl, response);
                        return;
                    }

                    if (m_doubaoApiKey.empty()) {
                        response["type"] = "text";
                        response["content"] = "错误：未设置 ARK_API_KEY 环境变量";
                        response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
                        sendMessage(hdl, response);
                        return;
                    }

                    // Send typing indicator
                    sendTypingIndicator(hdl, true);

                    // Generate image
                    std::cout << "Generating image with prompt: " << prompt << std::endl;
                    std::string imageBase64 = generateDoubaoImage(prompt);

                    sendTypingIndicator(hdl, false);

                    if (imageBase64.empty()) {
                        response["type"] = "text";
                        response["content"] = "图片生成失败，请稍后重试";
                        response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
                        sendMessage(hdl, response);
                    } else {
                        // Send generated image
                        response["type"] = "image";
                        response["content"] = "data:image/png;base64," + imageBase64;
                        response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
                        sendMessage(hdl, response);
                    }
                } else {
                    // DeepSeek does not support image generation
                    response["type"] = "text";
                    response["content"] = "当前使用 DeepSeek API，不支持图像生成。请发送普通文本消息进行对话。";
                    response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
                    sendMessage(hdl, response);
                }
                return;
            }

            // Not an image request - handle based on API type
            sendTypingIndicator(hdl, true);

            if (apiType == API_DOUBAO) {
                // Doubao: simple echo with delay
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                response["type"] = "text";
                response["content"] = "I received your message: " + content;
                response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
            } else { // API_DEEPSEEK
                // DeepSeek: generate intelligent reply via API
                if (m_deepseekApiKey.empty()) {
                    response["type"] = "text";
                    response["content"] = "错误：未设置 DEEPSEEK_API_KEY 环境变量";
                    response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
                } else {
                    std::string reply = generateDeepSeekResponse(content);
                    if (reply.empty()) {
                        response["type"] = "text";
                        response["content"] = "DeepSeek API 调用失败，请稍后重试";
                        response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
                    } else {
                        response["type"] = "text";
                        response["content"] = reply;
                        response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
                    }
                }
            }

            sendTypingIndicator(hdl, false);
            sendMessage(hdl, response);

        } else if (type == "image") {
            // Handle image message
            std::cout << "Image received (base64 data, length: " << content.length() << ")" << std::endl;

            response["type"] = "text";
            response["content"] = "I received your image!";
            response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);

            sendMessage(hdl, response);

            // Send example image back
            sendImageResponse(hdl);
        }
    }

    // Check if message is an image generation request
    bool isImageGenerationRequest(const std::string& content) {
        // Check for /image command
        if (content.find("/image") == 0 || content.find("/img") == 0) {
            return true;
        }
        // Check for Chinese commands
        if (content.find("生成图片") != std::string::npos || 
            content.find("画一张") != std::string::npos ||
            content.find("画个") != std::string::npos) {
            return true;
        }
        return false;
    }

    // Extract prompt from image generation request
    std::string extractImagePrompt(const std::string& content) {
        std::string prompt = content;

        // Handle /image or /img command
        if (content.find("/image") == 0) {
            prompt = content.substr(6);
        } else if (content.find("/img") == 0) {
            prompt = content.substr(4);
        } else if (content.find("生成图片") != std::string::npos) {
            size_t pos = content.find("生成图片");
            prompt = content.substr(pos + 4);
        } else if (content.find("画一张") != std::string::npos) {
            size_t pos = content.find("画一张");
            prompt = content.substr(pos + 3);
        } else if (content.find("画个") != std::string::npos) {
            size_t pos = content.find("画个");
            prompt = content.substr(pos + 2);
        }

        // Trim leading/trailing whitespace and separators
        size_t start = prompt.find_first_not_of(" :：");
        if (start == std::string::npos) {
            return "";
        }
        size_t end = prompt.find_last_not_of(" \t\n\r");
        return prompt.substr(start, end - start + 1);
    }

    // Example: Send an image to the client
    void sendImageResponse(websocketpp::connection_hdl hdl) {
        // Example 1: Load image from file and convert to base64
        // You'll need a library like libbase64 or implement your own
        std::string imageBase64 = loadImageAsBase64("kabun.webp");

        if (!imageBase64.empty()) {
            Json::Value response;
            response["type"] = "image";
            response["content"] = "data:image/webp;base64," + imageBase64;
            response["timestamp"] = Json::Value::UInt64(std::time(nullptr) * 1000);
            sendMessage(hdl, response);
        }
   }

    // Generate image using Doubao API
    std::string generateDoubaoImage(const std::string& prompt) {
        if (m_doubaoApiKey.empty()) {
            std::cerr << "Doubao API key not set" << std::endl;
            return "";
        }

        CURL *curl;
        CURLcode res;
        std::string response;

        curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Failed to initialize CURL" << std::endl;
            return "";
        }

        // Create JSON payload
        Json::Value payload;
        payload["model"] = "doubao-seedream-5-0-260128";
        payload["prompt"] = prompt;
        payload["size"] = "2K";
        payload["output_format"] = "png";
        payload["watermark"] = false;

        Json::StreamWriterBuilder writer;
        std::string json_data = Json::writeString(writer, payload);

        // Setup response buffer
        struct MemoryStruct chunk;
        chunk.memory = (char *)malloc(1);
        chunk.size = 0;

        // Set CURL options
        curl_easy_setopt(curl, CURLOPT_URL, "https://ark.cn-beijing.volces.com/api/v3/images/generations");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1); // Force HTTP/1.1

        // Set headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth_header = "Authorization: Bearer " + m_doubaoApiKey;
        headers = curl_slist_append(headers, auth_header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Set POST data
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());

        // Set callback for response
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        // Perform request
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "CURL request failed: " << curl_easy_strerror(res) << std::endl;
            free(chunk.memory);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return "";
        }

        // Get HTTP response code
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        if (response_code != 200) {
            std::cerr << "API request failed with status: " << response_code << std::endl;
            std::cerr << "Response: " << chunk.memory << std::endl;
            free(chunk.memory);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return "";
        }

        response = std::string(chunk.memory, chunk.size);

        // Parse response to extract image URL
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(response, root)) {
            if (!root["data"].empty() && root["data"][0]["url"].isString()) {
                std::string imageUrl = root["data"][0]["url"].asString();
                std::cout << "Generated image URL: " << imageUrl << std::endl;

                // Download the image and convert to base64
                std::string imageBase64 = downloadImageAsBase64(imageUrl);

                free(chunk.memory);
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);

                return imageBase64;
            }
        }

        std::cerr << "Failed to parse API response" << std::endl;
        free(chunk.memory);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return "";
    }

    // Generate text response using DeepSeek API
    std::string generateDeepSeekResponse(const std::string& userMessage) {
        if (m_deepseekApiKey.empty()) {
            std::cerr << "DeepSeek API key not set" << std::endl;
            return "";
        }

        CURL *curl;
        CURLcode res;
        std::string response;

        curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Failed to initialize CURL" << std::endl;
            return "";
        }

        // Create JSON payload for DeepSeek chat completion
        Json::Value payload;
        payload["model"] = "deepseek-chat";
        Json::Value messages(Json::arrayValue);

        if (!m_deepseekSystemPrompt.empty()) {
            Json::Value systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = m_deepseekSystemPrompt;
            messages.append(systemMsg);
        }

        Json::Value userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = userMessage;
        messages.append(userMsg);
        payload["messages"] = messages;
        payload["stream"] = false;

        Json::StreamWriterBuilder writer;
        std::string json_data = Json::writeString(writer, payload);

        // Setup response buffer
        struct MemoryStruct chunk;
        chunk.memory = (char *)malloc(1);
        chunk.size = 0;

        // Set CURL options
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1); // Force HTTP/1.1

        // Set headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth_header = "Authorization: Bearer " + m_deepseekApiKey;
        headers = curl_slist_append(headers, auth_header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Set POST data
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());

        // Set callback for response
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        // Perform request
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "DeepSeek CURL request failed: " << curl_easy_strerror(res) << std::endl;
            free(chunk.memory);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return "";
        }

        // Get HTTP response code
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        if (response_code != 200) {
            std::cerr << "DeepSeek API request failed with status: " << response_code << std::endl;
            std::cerr << "Response: " << chunk.memory << std::endl;
            free(chunk.memory);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return "";
        }

        response = std::string(chunk.memory, chunk.size);

        // Parse response to extract assistant message
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(response, root)) {
            if (root.isMember("choices") && root["choices"].size() > 0) {
                std::string reply = root["choices"][0]["message"]["content"].asString();
                free(chunk.memory);
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return reply;
            }
        }

        std::cerr << "Failed to parse DeepSeek API response" << std::endl;
        free(chunk.memory);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return "";
    }

    // Download image from URL and convert to base64
    std::string downloadImageAsBase64(const std::string& imageUrl) {
        CURL *curl;
        CURLcode res;

        curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Failed to initialize CURL for download" << std::endl;
            return "";
        }

        // Setup response buffer
        struct MemoryStruct chunk;
        chunk.memory = (char *)malloc(1);
        chunk.size = 0;

        curl_easy_setopt(curl, CURLOPT_URL, imageUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1); // Force HTTP/1.1

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "Failed to download image: " << curl_easy_strerror(res) << std::endl;
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return "";
        }

        // Convert binary data to base64 using OpenSSL
        std::string base64 = base64Encode((unsigned char *)chunk.memory, chunk.size);

        free(chunk.memory);
        curl_easy_cleanup(curl);

        return base64;
    }

    // Base64 encode using OpenSSL
    std::string base64Encode(unsigned char* data, size_t len) {
        EVP_ENCODE_CTX* ctx = EVP_ENCODE_CTX_new();
        if (!ctx) {
            return "";
        }

        size_t encodedSize = len * 2;
        std::vector<unsigned char> encoded(encodedSize);

        EVP_EncodeInit(ctx); 
        int encodedLen = 0;
        EVP_EncodeUpdate(ctx, encoded.data(), &encodedLen, data, len);
        int finalLen = 0;
        EVP_EncodeFinal(ctx, encoded.data() + encodedLen, &finalLen);
        EVP_ENCODE_CTX_free(ctx);

        std::string result(reinterpret_cast<char*>(encoded.data()), encodedLen + finalLen);
        result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
        result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());

        return result;
    }

    // Helper function to load image as base64 using OpenSSL
    std::string loadImageAsBase64(const std::string& filepath) {
        // Read file content
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open image file: " << filepath << std::endl;
            return "";
        }

        // Get file size
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read file into buffer
        std::vector<unsigned char> buffer(fileSize);
        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
        file.close();

        // Create base64 encoding context
        EVP_ENCODE_CTX* ctx = EVP_ENCODE_CTX_new();
        if (!ctx) {
            std::cerr << "Failed to create EVP_ENCODE_CTX" << std::endl;
            return "";
        }

        // Calculate output buffer size (base64 expands by ~33%)
        size_t encodedSize = fileSize*2;
        std::vector<unsigned char> encoded(encodedSize);

        // Encode
        EVP_EncodeInit(ctx);
        int encodedLen = 0;
        EVP_EncodeUpdate(ctx, encoded.data(), &encodedLen, buffer.data(), fileSize);
        EVP_EncodeFinal(ctx, encoded.data() + encodedLen, &encodedLen);
        EVP_ENCODE_CTX_free(ctx);

        // Convert to string and remove newlines
        std::string result(reinterpret_cast<char*>(encoded.data()));
        result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
        result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());

        return result;
    }

    void sendTypingIndicator(websocketpp::connection_hdl hdl, bool isTyping) {
        Json::Value response;
        response["type"] = "typing";
        response["status"] = isTyping;
        sendMessage(hdl, response);
    }

    void sendMessage(websocketpp::connection_hdl hdl, const Json::Value& message) {
        Json::StreamWriterBuilder writer;
        std::string payload = Json::writeString(writer, message);
        m_server.send(hdl, payload, websocketpp::frame::opcode::text);
    }

    WsServer m_server;
    std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> m_clients;
    ApiType m_apiType;
    std::string m_doubaoApiKey;
    std::string m_deepseekApiKey;
    std::string m_deepseekSystemPrompt;
};

int main(int argc, char* argv[]) {
    ApiType apiType = API_DOUBAO; // Default to Doubao

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--api") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "deepseek") == 0) {
                apiType = API_DEEPSEEK;
            } else if (strcmp(argv[i+1], "doubao") == 0) {
                apiType = API_DOUBAO;
            } else {
                std::cerr << "Unknown API type: " << argv[i+1] << ". Using default Doubao." << std::endl;
            }
            i++; // skip the value
        }
    }

    try {
        ChatServer server(apiType);
        server.run(8080);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}