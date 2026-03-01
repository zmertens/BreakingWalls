#include "HttpClient.hpp"

#include <regex>
#include <sstream>
#include <unordered_map>

#include <SFML/Network.hpp>

namespace
{
    constexpr unsigned short DefaultPort = 80;
}

void HttpClient::setServerURL(const std::string &url) noexcept
{
    mServerURL = url;
    parseServerURL();
}

std::string HttpClient::getServerURL() const noexcept
{
    return mServerURL;
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

    if (regex_match(mServerURL, matches, url_regex))
    {
        mHost = matches[1].str();
        if (mHost == "localhost")
        {
            mHost = "127.0.0.1";
        }
        if (matches.size() > 2 && matches[2].matched)
        {
            try
            {
                mPort = static_cast<unsigned short>(stoi(matches[2].str()));
            }
            catch (const std::exception &e)
            {
                mPort = 80;
            }
        }
    }
    else
    {
        mHost = mNetworkData.empty() ? "" : mNetworkData;
        if (mHost == "localhost")
        {
            mHost = "127.0.0.1";
        }
        mPort = 80;
    }
}

std::string HttpClient::get(const std::string &path)
{
    try
    {
        sf::Http http(mHost, mPort);

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
        sf::Http http(mHost, mPort);

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
    return mHost.empty() ? std::string_view{} : std::string_view(mHost);
}
