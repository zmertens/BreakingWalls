#include "MultiplayerGameState.hpp"

#include <SDL3/SDL.h>

#include <glad/glad.h>

#include <cstdint>
#include <regex>
#include <sstream>
#include <algorithm>
#include <future>
#include <chrono>

#include <dearimgui/imgui.h>

#include "GameState.hpp"
#include "HttpClient.hpp"
#include "JsonUtils.hpp"
#include "MusicPlayer.hpp"
#include "StateStack.hpp"

namespace
{
    constexpr float NETWORK_SEND_INTERVAL = 0.25f;
    constexpr float NETWORK_REGISTRATION_INTERVAL = 15.0f;
    constexpr float NETWORK_DISCOVERY_INTERVAL = 5.0f;
    constexpr float LOBBY_STATUS_INTERVAL = 1.0f;

    bool parseJsonStringField(const std::string &src, std::string_view key, std::string &out)
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

    bool parseJsonIntField(const std::string &src, std::string_view key, unsigned short &out)
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

    std::string makePeerKey(const sf::IpAddress &ip, unsigned short port)
    {
        return ip.toString() + ":" + std::to_string(port);
    }

    std::string makeResponseSnippet(const char *label, const std::string &response)
    {
        const size_t maxLen = 160;
        const size_t len = std::min(response.size(), maxLen);
        const std::string snippet = response.substr(0, len);
        return std::string("MultiplayerGameState: ") + label +
               " (" + std::to_string(response.size()) + " bytes): " + snippet;
    }
}

MultiplayerGameState::MultiplayerGameState(StateStack &stack, Context context)
    : State(stack, context), mLocalGame(std::make_unique<GameState>(stack, context)), mMusic{nullptr}
{
    log("MultiplayerGameState: Constructed");
    initializeNetwork();
    log("MultiplayerGameState: Initialized");
}

void MultiplayerGameState::draw() const noexcept
{
    if (mLocalGame)
    {
        mLocalGame->draw();

        // Render remote players if lobby is ready
        if (mLobbyReady && !mRemotePlayers.empty())
        {
            // Prepare for 3D rendering (clear depth after path tracer)
            glClear(GL_DEPTH_BUFFER_BIT);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);

            // Get World and Camera from local game
            auto &world = mLocalGame->getWorld();
            const auto &camera = mLocalGame->getCamera();

            // Render each remote player
            for (const auto &[playerName, remoteState] : mRemotePlayers)
            {
                if (remoteState.initialized)
                {
                    AnimationRect frame = remoteState.animator.getCurrentFrame();
                    world.renderCharacterFromState(
                        remoteState.position,
                        remoteState.facing,
                        frame,
                        camera);
                }
            }
        }
    }

    // Draw lobby status overlay if not ready
    if (!mLobbyReady)
    {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(350, 100), ImGuiCond_Always);
        ImGui::Begin("Multiplayer Lobby", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse);

        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "WAITING FOR PLAYERS...");
        ImGui::Separator();
        ImGui::Text("Connected: %d / %d", mConnectedPlayerCount, mMinimumPlayers);
        ImGui::Text("Minimum: %d players", mMinimumPlayers);

        if (mConnectedPlayerCount < mMinimumPlayers)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                               "Need %d more player(s)", mMinimumPlayers - mConnectedPlayerCount);
        }

        ImGui::End();
    }
}

bool MultiplayerGameState::update(float dt, unsigned int subSteps) noexcept
{
    mDebugAccumulator += dt;
    if (mDebugAccumulator >= 5.0f)
    {
        mDebugAccumulator = 0.0f;
        log("MultiplayerGameState: Update tick (peers=" +
            std::to_string(mRemotePlayers.size()) +
            ", sockets=" + std::to_string(mPeerSockets.size()) +
            ", reg=" + std::to_string(mRegistrationInFlight ? 1 : 0) +
            ", disc=" + std::to_string(mDiscoveryInFlight ? 1 : 0) + ")");
    }

    mPacketLogAccumulator += dt;
    if (mPacketLogAccumulator >= 1.0f)
    {
        mPacketLogAccumulator = 0.0f;
        if (mPacketCount > 0)
        {
            log("MultiplayerGameState: Packets received/s=" + std::to_string(mPacketCount));
        }
        mPacketCount = 0;
    }

    if (mLocalGame)
    {
        // Only update the game if lobby is ready (enough players connected)
        if (mLobbyReady)
        {
            mLocalGame->update(dt, subSteps);
        }
    }

    if (mNetworkReady)
    {
        if (mInitialNetworkSync)
        {
            mInitialNetworkSync = false;
            startRegistration();
        }

        mRegistrationAccumulator += dt;
        if (mRegistrationAccumulator >= NETWORK_REGISTRATION_INTERVAL)
        {
            mRegistrationAccumulator = 0.0f;
            startRegistration();
        }

        if (mRegistrationCompleteOnce)
        {
            mDiscoveryAccumulator += dt;
            if (mDiscoveryAccumulator >= NETWORK_DISCOVERY_INTERVAL)
            {
                mDiscoveryAccumulator = 0.0f;
                startDiscovery();
            }
        }

        pollRegistration();
        pollDiscovery();

        // Send lobby status periodically
        mLobbyStatusAccumulator += dt;
        if (mLobbyStatusAccumulator >= LOBBY_STATUS_INTERVAL)
        {
            mLobbyStatusAccumulator = 0.0f;
            sendLobbyStatus();
            checkLobbyReady();
        }
    }

    pollNetwork();

    // Update remote player animations
    for (auto &[playerName, remoteState] : mRemotePlayers)
    {
        if (remoteState.initialized)
        {
            remoteState.animator.update();
        }
    }

    // Only send position updates if lobby is ready
    if (mLobbyReady)
    {
        sendLocalState(dt);
    }

    return true;
}

bool MultiplayerGameState::handleEvent(const SDL_Event &event) noexcept
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
        log("WARN: MultiplayerGameState: NETWORK_URL not found");
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
        log("WARN: MultiplayerGameState: Failed to start listener");
        return false;
    }

    mListener.setBlocking(false);
    mLocalPort = mListener.getLocalPort();
    mLocalPlayerName = "player_" + std::to_string(mLocalPort);

    mSelector.add(mListener);

    log("MultiplayerGameState: Listening on port " + std::to_string(mLocalPort));

    return true;
}

void MultiplayerGameState::startRegistration()
{
    if (mRegistrationInFlight || mDiscoveryInFlight || !getContext().httpClient)
    {
        return;
    }

    mRegistrationInFlight = true;
    log("MultiplayerGameState: Starting registration POST");

    const std::string playerName = mLocalPlayerName;
    const unsigned short playerPort = mLocalPort;

    std::ostringstream payload;
    payload << "{"
            << "\"player_name\":\"" << playerName << "\""
            << ",\"port\":" << playerPort
            << "}";

    mRegistrationFuture = std::async(std::launch::async,
                                     [client = getContext().httpClient, body = payload.str()]()
                                     {
                                         try
                                         {
                                             return client ? client->post("/mazes/networks/data", body) : std::string{};
                                         }
                                         catch (...)
                                         {
                                             return std::string{};
                                         }
                                     });
}

void MultiplayerGameState::pollRegistration()
{
    if (!mRegistrationInFlight)
    {
        return;
    }

    if (mRegistrationFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        return;
    }

    mRegistrationInFlight = false;
    std::string response;
    try
    {
        response = mRegistrationFuture.get();
    }
    catch (const std::exception &e)
    {
        log("WARN: MultiplayerGameState: Registration future error: " + std::string(e.what()));
        return;
    }
    catch (...)
    {
        log("WARN: MultiplayerGameState: Registration future error (unknown)");
        return;
    }
    if (response.empty())
    {
        log("WARN: MultiplayerGameState: Registration POST returned no data");
        return;
    }

    mRegistrationCompleteOnce = true;
    log(makeResponseSnippet("Registration response", response));
}

void MultiplayerGameState::startDiscovery()
{
    if (mDiscoveryInFlight || mRegistrationInFlight || !getContext().httpClient)
    {
        return;
    }

    mDiscoveryInFlight = true;
    log("MultiplayerGameState: Starting discovery GET");

    mDiscoveryFuture = std::async(std::launch::async,
                                  [client = getContext().httpClient]()
                                  {
                                      try
                                      {
                                          return client ? client->get("/mazes/networks/data") : std::string{};
                                      }
                                      catch (...)
                                      {
                                          return std::string{};
                                      }
                                  });
}

void MultiplayerGameState::pollDiscovery()
{
    if (!mDiscoveryInFlight)
    {
        return;
    }

    if (mDiscoveryFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        return;
    }

    mDiscoveryInFlight = false;
    std::string response;
    try
    {
        response = mDiscoveryFuture.get();
    }
    catch (const std::exception &e)
    {
        log("WARN: MultiplayerGameState: Discovery future error: " + std::string(e.what()));
        return;
    }
    catch (...)
    {
        log("WARN: MultiplayerGameState: Discovery future error (unknown)");
        return;
    }
    if (!response.empty())
    {
        log(makeResponseSnippet("Discovery response", response));
    }

    discoverPeers(response);
}

void MultiplayerGameState::discoverPeers(const std::string &response)
{
    if (response.empty())
    {
        log("WARN: MultiplayerGameState: No peer data received");
        return;
    }

    const auto peers = parseActivePlayers(response);
    log("MultiplayerGameState: Discovery response " + std::to_string(response.size()) +
        " bytes, " + std::to_string(peers.size()) + " peer(s)");
    for (const auto &peer : peers)
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

        log("MultiplayerGameState: Peer " + peer.name + " @ " +
            peer.ip.toString() + ":" + std::to_string(peer.port));
        connectToPeer(peer);
    }
}

void MultiplayerGameState::connectToPeer(const PeerInfo &peer)
{
    auto socket = std::make_unique<sf::TcpSocket>();
    socket->setBlocking(true);

    log("MultiplayerGameState: Connecting to " + peer.ip.toString() + ":" +
        std::to_string(peer.port));

    if (socket->connect(peer.ip, peer.port, sf::seconds(0.2f)) != sf::Socket::Status::Done)
    {
        log("WARN: MultiplayerGameState: Failed to connect to " +
            peer.ip.toString() + ":" + std::to_string(peer.port));
        return;
    }

    socket->setBlocking(false);
    mSelector.add(*socket);
    mPeerSockets.push_back(std::move(socket));

    log("MultiplayerGameState: Connected to " + peer.ip.toString() + ":" +
        std::to_string(peer.port));
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

    // Accept any pending connections (non-blocking listener)
    while (true)
    {
        auto socket = std::make_unique<sf::TcpSocket>();
        const auto status = mListener.accept(*socket);
        if (status == sf::Socket::Status::Done)
        {
            socket->setBlocking(false);
            mPeerSockets.push_back(std::move(socket));
            log("MultiplayerGameState: Accepted incoming connection");
            continue;
        }

        break;
    }

    for (auto it = mPeerSockets.begin(); it != mPeerSockets.end();)
    {
        sf::TcpSocket &socket = *(*it);
        sf::Packet packet;
        const auto status = socket.receive(packet);

        if (status == sf::Socket::Status::Done)
        {
            handlePacket(packet);
            ++mPacketCount;
        }
        else if (status == sf::Socket::Status::Disconnected)
        {
            it = mPeerSockets.erase(it);
            continue;
        }

        ++it;
    }
}

void MultiplayerGameState::handlePacket(sf::Packet &packet)
{
    std::int32_t packetType = 0;
    if (!(packet >> packetType))
    {
        return;
    }

    if (packetType == static_cast<std::int32_t>(HttpClient::PacketType::POSITION_UPDATE))
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
            auto &remote = mRemotePlayers[name];
            remote.position = glm::vec3{x, y, z};
            remote.facing = facing;
            remote.moving = (moving != 0);
            remote.animState = animState;

            // Initialize animator on first packet
            if (!remote.initialized)
            {
                remote.animator.initialize(0); // Character index 0
                remote.initialized = true;
            }

            // Update animator state based on movement
            if (remote.moving)
            {
                remote.animator.setState(CharacterAnimState::WALK_FORWARD);
            }
            else
            {
                remote.animator.setState(CharacterAnimState::IDLE);
            }

            // Update animator position and rotation
            remote.animator.setPosition(remote.position);
            remote.animator.setRotation(remote.facing);

            // Debug log for first position update from each player
            static std::unordered_set<std::string> loggedPlayers;
            if (loggedPlayers.find(name) == loggedPlayers.end())
            {
                loggedPlayers.insert(name);
                log("MultiplayerGameState: First position update from " + name +
                    " at (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
            }
        }
    }
    else if (packetType == static_cast<std::int32_t>(HttpClient::PacketType::LOBBY_STATUS))
    {
        std::int32_t playerCount = 0;
        std::int32_t minPlayers = 0;
        std::int32_t isReady = 0;

        if (packet >> playerCount >> minPlayers >> isReady)
        {
            // Update our understanding of peer lobby state
            // Could be used to synchronize lobby readiness across peers
        }
    }
    else if (packetType == static_cast<std::int32_t>(HttpClient::PacketType::LOBBY_READY))
    {
        log("MultiplayerGameState: Received LobbyReady from peer");
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

    const auto &player = *getContext().player;
    const auto position = player.getPosition();
    const auto facing = player.getFacingDirection();
    const auto moving = player.isMoving();
    const auto animState = static_cast<std::uint8_t>(player.getAnimator().getState());

    sf::Packet packet;
    packet << static_cast<std::int32_t>(HttpClient::PacketType::POSITION_UPDATE)
           << mLocalPlayerName
           << position.x << position.y << position.z
           << facing
           << static_cast<std::uint8_t>(moving ? 1 : 0)
           << animState;

    for (auto &socket : mPeerSockets)
    {
        const auto status = socket->send(packet);
        if (status != sf::Socket::Status::Done)
        {
            log("WARN: MultiplayerGameState: Failed to send packet (status=" +
                std::to_string(static_cast<int>(status)) + ")");
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
    const std::string &json) const
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

void MultiplayerGameState::sendLobbyStatus()
{
    if (!mNetworkReady || mPeerSockets.empty())
    {
        return;
    }

    // Count: self + connected peers
    mConnectedPlayerCount = 1 + static_cast<int>(mPeerSockets.size());

    sf::Packet packet;
    packet << static_cast<std::int32_t>(HttpClient::PacketType::LOBBY_STATUS)
           << static_cast<std::int32_t>(mConnectedPlayerCount)
           << static_cast<std::int32_t>(mMinimumPlayers)
           << static_cast<std::int32_t>(mLobbyReady ? 1 : 0);

    for (auto &socket : mPeerSockets)
    {
        [[maybe_unused]] auto status = socket->send(packet);
    }
}

void MultiplayerGameState::checkLobbyReady()
{
    // Count: self + connected peers
    mConnectedPlayerCount = 1 + static_cast<int>(mPeerSockets.size());

    bool wasReady = mLobbyReady;
    mLobbyReady = (mConnectedPlayerCount >= mMinimumPlayers);

    if (!wasReady && mLobbyReady)
    {
        log("MultiplayerGameState: Lobby ready! Players: " +
            std::to_string(mConnectedPlayerCount) + "/" +
            std::to_string(mMinimumPlayers) + " - GAME STARTING");

        // Broadcast ready status
        sf::Packet packet;
        packet << static_cast<std::int32_t>(HttpClient::PacketType::LOBBY_READY);
        for (auto &socket : mPeerSockets)
        {
            [[maybe_unused]] auto status = socket->send(packet);
        }
    }
    else if (wasReady && !mLobbyReady)
    {
        log("MultiplayerGameState: Lobby not ready (player disconnected). Players: " +
            std::to_string(mConnectedPlayerCount) + "/" +
            std::to_string(mMinimumPlayers));
    }
    else if (!mLobbyReady)
    {
        // Periodic waiting message (throttled above)
        log("MultiplayerGameState: Waiting for players... (" +
            std::to_string(mConnectedPlayerCount) + "/" +
            std::to_string(mMinimumPlayers) + ")");
    }
}
