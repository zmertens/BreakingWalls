#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>

/// @brief HTTP client for communicating with Corners maze building server
class HttpClient
{
public:
    // Packets originated in the client
    enum PacketType
    {
        POSITION_UPDATE,
        // format: [Int32:packetType]
        LOBBY_STATUS,
        // format: [Int32:packetType] [Int32:playerCount] [Int32:minPlayers] [Int32:isReady]
        LOBBY_READY
    };

    void setServerURL(const std::string &url) noexcept;
    [[nodiscard]] std::string getServerURL() const noexcept;

    /// @brief Issue a GET request and return the response body (empty on failure)
    /// @param path Absolute path on server (e.g. "/mazes/networks/data")
    std::string get(const std::string &path);

    /// @brief Issue a POST request and return the response body (empty on failure)
    /// @param path Absolute path on server (e.g. "/mazes/networks/data")
    /// @param body Request payload
    /// @param contentType MIME type for payload
    std::string post(
        const std::string &path,
        const std::string &body,
        const std::string &contentType = "application/json");

    std::string_view getHostURL() const noexcept;

private:
    /// @brief Parse server URL and extract host and port
    void parseServerURL() noexcept;

    std::string mNetworkData;
    std::string mServerURL;
    std::string mHost;
    unsigned short mPort;
};

#endif // HTTP_CLIENT_HPP
