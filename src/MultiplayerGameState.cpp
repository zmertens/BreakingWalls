#include "MultiplayerGameState.hpp"

#include <SDL3/SDL.h>

#include <glad/glad.h>

#include <cstdint>
#include <regex>
#include <sstream>
#include <algorithm>

#include "GameState.hpp"
#include "HttpClient.hpp"
#include "JsonUtils.hpp"
#include "MusicPlayer.hpp"
#include "NetworkProtocol.hpp"
#include "StateStack.hpp"

namespace
{
    constexpr std::string_view NETWORK_URL_KEY = "network_url";
    constexpr float NETWORK_SEND_INTERVAL = 0.1f;
    constexpr float NETWORK_REGISTRATION_INTERVAL = 5.0f;
    constexpr float NETWORK_DISCOVERY_INTERVAL = 2.0f;

    bool parseJsonStringField(const std::string& src, std::string_view key, std::string& out)
    {
        const std::regex fieldRegex(
            std::string{"\""} + std::string(key) + std::string{"\"\\s*:\\s*\"([^\"]*)\""});

        std::smatch match;
        if (std::regex_search(src, match, fieldRegex) && match.size() > 1)
        {
            out = match[1].str();
            return true;
        }

        return false;
    }

    bool parseJsonIntField(const std::string& src, std::string_view key, unsigned short& out)
    {
        const std::regex fieldRegex(
            std::string{"\""} + std::string(key) + std::string{"\"\\s*:\\s*(\\d+)"});

        std::smatch match;
        if (std::regex_search(src, match, fieldRegex) && match.size() > 1)
        {
            try
            {
                out = static_cast<unsigned short>(std::stoul(match[1].str()));
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        return false;
    }

    std::string makePeerKey(const sf::IpAddress& ip, unsigned short port)
    {
        return ip.toString() + ":" + std::to_string(port);
    }

    void logResponseSnippet(const char* label, const std::string& response)
    {
        const size_t maxLen = 160;
        const size_t len = std::min(response.size(), maxLen);
        const std::string snippet = response.substr(0, len);
        SDL_Log("MultiplayerGameState: %s (%zu bytes): %s", label, response.size(), snippet.c_str());
    }
}

MultiplayerGameState::MultiplayerGameState(StateStack& stack, Context context)
    : State(stack, context)
    , mLocalGame(std::make_unique<GameState>(stack, context))
    , mMusic{ nullptr }
{
    initializeNetwork();
}

void MultiplayerGameState::draw() const noexcept
{
    if (mLocalGame)
    {
        mLocalGame->draw();
    }
}

bool MultiplayerGameState::update(float dt, unsigned int subSteps) noexcept
{
    if (mLocalGame)
    {
        mLocalGame->update(dt, subSteps);
    }

    if (mNetworkReady)
    {
        if (mInitialNetworkSync)
        {
            mInitialNetworkSync = false;
            runRegistration();
            runDiscovery();
        }

        mRegistrationAccumulator += dt;
        if (mRegistrationAccumulator >= NETWORK_REGISTRATION_INTERVAL)
        {
            mRegistrationAccumulator = 0.0f;
            runRegistration();
        }

        mDiscoveryAccumulator += dt;
        if (mDiscoveryAccumulator >= NETWORK_DISCOVERY_INTERVAL)
        {
            mDiscoveryAccumulator = 0.0f;
            runDiscovery();
        }

        // Removed async handlers
    }

    pollNetwork();
    sendLocalState(dt);

    return true;
}

bool MultiplayerGameState::handleEvent(const SDL_Event& event) noexcept
{
    if (mLocalGame)
    {
        mLocalGame->handleEvent(event);
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            // Commented because returning to Menu from Settings with ESCAPE causes fallthrough effect
            // mShowMainMenu = false;
        }
    }

    return true;
}

void MultiplayerGameState::initializeNetwork()
{
    mNetworkUrl = loadNetworkUrl();
    if (mNetworkUrl.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "MultiplayerGameState: NETWORK_URL not found");
        return;
    }

    if (!startListener())
    {
        return;
    }

    mNetworkReady = true;
}

bool MultiplayerGameState::startListener()
{
    if (mListener.listen(0) != sf::Socket::Status::Done)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "MultiplayerGameState: Failed to start listener");
        return false;
    }

    mListener.setBlocking(false);
    mLocalPort = mListener.getLocalPort();
    mLocalPlayerName = "player_" + std::to_string(mLocalPort);

    mSelector.add(mListener);

    SDL_Log("MultiplayerGameState: Listening on port %u", mLocalPort);

    return true;
}

void MultiplayerGameState::runRegistration()
{
    if (!getContext().httpClient)
    {
        return;
    }

    SDL_Log("MultiplayerGameState: Starting registration POST");

    const std::string playerName = mLocalPlayerName;
    const unsigned short playerPort = mLocalPort;

    std::ostringstream payload;
    payload << "{"
            << "\"player_name\":\"" << playerName << "\""
            << ",\"port\":" << playerPort
            << "}";

    const std::string response = getContext().httpClient->post(
        "/mazes/networks/data", payload.str());
    if (response.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "MultiplayerGameState: Registration POST returned no data");
    }
    else
    {
        logResponseSnippet("Registration response", response);
    }
}

void MultiplayerGameState::runDiscovery()
{
    if (!getContext().httpClient)
    {
        return;
    }

    SDL_Log("MultiplayerGameState: Starting discovery GET");

    const std::string response = getContext().httpClient->get("/mazes/networks/data");
    if (!response.empty())
    {
        logResponseSnippet("Discovery response", response);
    }
    discoverPeers(response);
}

void MultiplayerGameState::discoverPeers(const std::string& response)
{
    if (response.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "MultiplayerGameState: No peer data received");
        return;
    }

    const auto peers = parseActivePlayers(response);
    SDL_Log("MultiplayerGameState: Discovery response %zu bytes, %zu peer(s)",
        response.size(), peers.size());
    for (const auto& peer : peers)
    {
        if (peer.port == 0)
        {
            continue;
        }

        if (peer.port == mLocalPort)
        {
            continue;
        }

        const auto peerKey = makePeerKey(peer.ip, peer.port);
        if (mKnownPeers.find(peerKey) != mKnownPeers.end())
        {
            continue;
        }

        mKnownPeers.insert(peerKey);

        SDL_Log("MultiplayerGameState: Peer %s @ %s:%u",
            peer.name.c_str(), peer.ip.toString().c_str(), peer.port);
        connectToPeer(peer);
    }
}

void MultiplayerGameState::connectToPeer(const PeerInfo& peer)
{
    auto socket = std::make_unique<sf::TcpSocket>();
    socket->setBlocking(true);

    SDL_Log("MultiplayerGameState: Connecting to %s:%u",
        peer.ip.toString().c_str(), peer.port);

    if (socket->connect(peer.ip, peer.port, sf::seconds(0.2f)) != sf::Socket::Status::Done)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "MultiplayerGameState: Failed to connect to %s:%u",
            peer.ip.toString().c_str(), peer.port);
        return;
    }

    socket->setBlocking(false);
    mSelector.add(*socket);
    mPeerSockets.push_back(std::move(socket));

    SDL_Log("MultiplayerGameState: Connected to %s:%u", peer.ip.toString().c_str(), peer.port);
}

void MultiplayerGameState::pollNetwork()
{
    if (!mNetworkReady)
    {
        return;
    }

    if (mListener.getLocalPort() == 0 && mPeerSockets.empty())
    {
        return;
    }

    if (!mSelector.wait(sf::milliseconds(0)))
    {
        return;
    }

    if (mSelector.isReady(mListener))
    {
        auto socket = std::make_unique<sf::TcpSocket>();
        if (mListener.accept(*socket) == sf::Socket::Status::Done)
        {
            socket->setBlocking(false);
            mSelector.add(*socket);
            mPeerSockets.push_back(std::move(socket));
            SDL_Log("MultiplayerGameState: Accepted incoming connection");
        }
    }

    for (auto it = mPeerSockets.begin(); it != mPeerSockets.end();)
    {
        sf::TcpSocket& socket = *(*it);
        if (mSelector.isReady(socket))
        {
            sf::Packet packet;
            const auto status = socket.receive(packet);

            if (status == sf::Socket::Status::Done)
            {
                handlePacket(packet);
                SDL_Log("MultiplayerGameState: Packet received (%zu bytes)", packet.getDataSize());
            }
            else if (status == sf::Socket::Status::Disconnected)
            {
                mSelector.remove(socket);
                it = mPeerSockets.erase(it);
                continue;
            }
        }

        ++it;
    }
}

void MultiplayerGameState::handlePacket(sf::Packet& packet)
{
    std::int32_t packetType = 0;
    if (!(packet >> packetType))
    {
        return;
    }

    if (packetType == static_cast<std::int32_t>(Client::PositionUpdate))
    {
        std::string name;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float facing = 0.0f;
        std::uint8_t moving = 0;
        std::uint8_t animState = 0;

        if (packet >> name >> x >> y >> z >> facing >> moving >> animState)
        {
            auto& remote = mRemotePlayers[name];
            remote.position = glm::vec3{x, y, z};
            remote.facing = facing;
            remote.moving = (moving != 0);
            remote.animState = animState;
        }
    }
}

void MultiplayerGameState::sendLocalState(float dt)
{
    if (!mNetworkReady || mPeerSockets.empty())
    {
        return;
    }

    mSendAccumulator += dt;
    if (mSendAccumulator < NETWORK_SEND_INTERVAL)
    {
        return;
    }

    mSendAccumulator = 0.0f;

    const auto& player = *getContext().player;
    const auto position = player.getPosition();
    const auto facing = player.getFacingDirection();
    const auto moving = player.isMoving();
    const auto animState = static_cast<std::uint8_t>(player.getAnimator().getState());

    sf::Packet packet;
    packet << static_cast<std::int32_t>(Client::PositionUpdate)
           << mLocalPlayerName
           << position.x << position.y << position.z
           << facing
           << static_cast<std::uint8_t>(moving ? 1 : 0)
           << animState;

    for (auto& socket : mPeerSockets)
    {
        const auto status = socket->send(packet);
        if (status != sf::Socket::Status::Done)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "MultiplayerGameState: Failed to send packet (status=%d)",
                static_cast<int>(status));
        }
    }
}

std::string MultiplayerGameState::loadNetworkUrl() const
{
    if (!getContext().httpClient)
    {
        return {};
    }

    const auto hostUrl = getContext().httpClient->getHostURL();
    return std::string(hostUrl);
}

std::vector<MultiplayerGameState::PeerInfo> MultiplayerGameState::parseActivePlayers(
    const std::string& json) const
{
    std::vector<PeerInfo> peers;

    const std::regex objectRegex(R"(\{[^\}]*\})");
    auto begin = std::sregex_iterator(json.begin(), json.end(), objectRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it)
    {
        const std::string object = it->str();
        PeerInfo peer;
        std::string ipStr;
        unsigned short portValue = 0;

        if (!parseJsonStringField(object, "player_name", peer.name))
        {
            continue;
        }

        if (!parseJsonStringField(object, "ip", ipStr) || !parseJsonIntField(object, "port", portValue))
        {
            continue;
        }

        peer.ip = sf::IpAddress::resolve(ipStr).value_or(sf::IpAddress::LocalHost);

        peer.port = portValue;

        peers.push_back(peer);
    }

    return peers;
}
