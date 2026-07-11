#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>
#include <nlohmann/json.hpp>

namespace opencode {

struct HttpResponse {
    int status_code{0};
    std::string body;
    bool success() const { return status_code >= 200 && status_code < 300; }
};

class HttpClient {
public:
    HttpClient();
    
    HttpResponse post(const std::string& url, const nlohmann::json& data, const std::string& headers);
    HttpResponse get(const std::string& url, const std::string& headers);
    
private:
    std::string perform_request(const std::string& url, const std::string& method, 
                                const std::string& post_data, const std::string& headers);
};

} // namespace opencode

#endif // HTTP_CLIENT_HPP
