#include "HttpClient.hpp"

#include <regex>
#include <sstream>
#include <unordered_map>

#include <SFML/Network.hpp>

namespace
{
    constexpr std::string_view NETWORK_URL_KEY = "network_url";
    constexpr unsigned short DefaultPort = 80;
}

HttpClient::HttpClient(const std::string &network_data)
    : m_network_data(network_data), m_server_url(""), m_port(DefaultPort)
{
    parseServerURL();
}

void HttpClient::parseServerURL() noexcept
{
    using std::ostringstream;
    using std::regex;
    using std::smatch;
    using std::stoi;
    using std::string;

    // Parse URL to extract host and port
    regex url_regex(R"(^https?://([^:/]+)(?::(\d+))?(?:/.*)?$)");
    smatch matches;

    if (regex_match(m_network_data, matches, url_regex))
    {
        m_host = matches[1].str();
        if (m_host == "localhost")
        {
            m_host = "127.0.0.1";
        }
        if (matches.size() > 2 && matches[2].matched)
        {
            try
            {
                m_port = static_cast<unsigned short>(stoi(matches[2].str()));
            }
            catch (const std::exception &e)
            {
                m_port = 80;
            }
        }
    }
    else
    {
        m_host = m_network_data;
        if (m_host == "localhost")
        {
            m_host = "127.0.0.1";
        }
        m_port = 80;
    }
}

std::string HttpClient::get(const std::string &path)
{
    try
    {
        sf::Http http(m_host, m_port);

        sf::Http::Request request(path, sf::Http::Request::Method::Get);
        request.setHttpVersion(1, 0);
        request.setField("Connection", "close");

        sf::Http::Response response = http.sendRequest(request, sf::seconds(0.5f));
        const auto status = response.getStatus();

        if (status != sf::Http::Response::Status::Ok)
        {
            return response.getBody();
        }

        return response.getBody();
    }
    catch (const std::exception &)
    {
        return {};
    }
}

std::string HttpClient::post(
    const std::string &path,
    const std::string &body,
    const std::string &contentType)
{
    try
    {
        sf::Http http(m_host, m_port);

        sf::Http::Request request(path, sf::Http::Request::Method::Post);
        request.setHttpVersion(1, 0);
        request.setField("Connection", "close");
        request.setField("Content-Type", contentType);
        request.setField("Content-Length", std::to_string(body.size()));
        request.setBody(body);

        sf::Http::Response response = http.sendRequest(request, sf::seconds(0.5f));
        const auto status = response.getStatus();
        if (status != sf::Http::Response::Status::Ok &&
            status != sf::Http::Response::Status::Created)
        {
            return response.getBody();
        }

        return response.getBody();
    }
    catch (const std::exception &)
    {
        return {};
    }
}

std::string_view HttpClient::getHostURL() const noexcept
{
    return m_host;
}
