#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>

/// @brief HTTP client for communicating with Corners maze building server
class HttpClient
{
public:
    /// @brief Constructor
    /// @param network_data Network data for initializing the HTTP client (e.g. server URL)
    explicit HttpClient(const std::string& network_data);

    /// @brief Issue a GET request and return the response body (empty on failure)
    /// @param path Absolute path on server (e.g. "/mazes/networks/data")
    std::string get(const std::string& path);

    /// @brief Issue a POST request and return the response body (empty on failure)
    /// @param path Absolute path on server (e.g. "/mazes/networks/data")
    /// @param body Request payload
    /// @param contentType MIME type for payload
    std::string post(
        const std::string& path,
        const std::string& body,
        const std::string& contentType = "application/json");

    std::string_view getHostURL() const noexcept;

private:
    /// @brief Parse server URL and extract host and port
    void parseServerURL() noexcept;

    std::string m_network_data;
    std::string m_server_url;
    std::string m_host;
    unsigned short m_port;
};

#endif // HTTP_CLIENT_HPP
