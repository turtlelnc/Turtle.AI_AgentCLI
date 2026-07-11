#include "http_client.hpp"
#include <curl/curl.h>

namespace opencode {

static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

HttpClient::HttpClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

std::string HttpClient::perform_request(const std::string& url, const std::string& method,
                                        const std::string& post_data, const std::string& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "";
    }
    
    std::string response_body;
    struct curl_slist* header_list = nullptr;
    
    // Parse and add headers
    std::istringstream header_stream(headers);
    std::string header_line;
    while (std::getline(header_stream, header_line)) {
        if (!header_line.empty()) {
            header_list = curl_slist_append(header_list, header_line.c_str());
        }
    }
    
    // Add Content-Type for POST
    if (method == "POST") {
        header_list = curl_slist_append(header_list, "Content-Type: application/json");
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    long response_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }
    
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    
    // Store status code in a thread-local or global for retrieval
    // For simplicity, we'll encode it in the response
    return std::to_string(response_code) + "\n" + response_body;
}

HttpResponse HttpClient::post(const std::string& url, const nlohmann::json& data, const std::string& headers) {
    HttpResponse response;
    std::string result = perform_request(url, "POST", data.dump(), headers);
    
    size_t newline_pos = result.find('\n');
    if (newline_pos != std::string::npos) {
        try {
            response.status_code = std::stoi(result.substr(0, newline_pos));
        } catch (...) {
            response.status_code = 0;
        }
        response.body = result.substr(newline_pos + 1);
    }
    
    return response;
}

HttpResponse HttpClient::get(const std::string& url, const std::string& headers) {
    HttpResponse response;
    std::string result = perform_request(url, "GET", "", headers);
    
    size_t newline_pos = result.find('\n');
    if (newline_pos != std::string::npos) {
        try {
            response.status_code = std::stoi(result.substr(0, newline_pos));
        } catch (...) {
            response.status_code = 0;
        }
        response.body = result.substr(newline_pos + 1);
    }
    
    return response;
}

} // namespace opencode
