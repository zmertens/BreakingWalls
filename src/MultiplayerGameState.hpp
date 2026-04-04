#ifndef MULTIPLAYER_GAME_STATE_HPP
#define MULTIPLAYER_GAME_STATE_HPP

#include "State.hpp"
#include "Animation.hpp"
#include "MatchController.hpp"

#include <SFML/Network.hpp>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <future>

class GameState;
class MusicPlayer;

class MultiplayerGameState : public State
{
public:
    explicit MultiplayerGameState(StateStack &stack, Context context);

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event &event) noexcept override;

private:
    struct PeerInfo
    {
        std::string name;
        sf::IpAddress ip{sf::IpAddress::LocalHost};
        unsigned short port{0};
    };

    struct RemotePlayerState
    {
        glm::vec3 position{0.0f};
        float facing{0.0f};
        bool moving{false};
        std::uint8_t animState{0};
        CharacterAnimator animator;
        bool initialized{false};
    };

    struct SimulatedPlayerState
    {
        RemotePlayerState renderState;
        std::unique_ptr<IPlayerController> controller;
        bool active{true};
    };

    void initializeNetwork();
    void initializeOfflineBots();
    void updateOfflineBots(float dt) noexcept;
    void renderPlayers(const std::unordered_map<std::string, RemotePlayerState> &players) const noexcept;
    bool startListener();
    void startRegistration();
    void pollRegistration();
    void startDiscovery();
    void pollDiscovery();
    void discoverPeers(const std::string &response);
    void connectToPeer(const PeerInfo &peer);
    void pollNetwork();
    void handlePacket(sf::Packet &packet);
    void sendLocalState(float dt);
    void sendLobbyStatus();
    void checkLobbyReady();
    std::string loadNetworkUrl() const;
    std::vector<PeerInfo> parseActivePlayers(const std::string &json) const;

    std::unique_ptr<GameState> mLocalGame;
    MusicPlayer *mMusic;

    std::string mNetworkUrl;
    std::string mLocalPlayerName;
    unsigned short mLocalPort{0};
    bool mNetworkReady{false};
    float mSendAccumulator{0.0f};
    float mRegistrationAccumulator{0.0f};
    float mDiscoveryAccumulator{0.0f};
    bool mInitialNetworkSync{true};
    float mDebugAccumulator{0.0f};
    float mPacketLogAccumulator{0.0f};
    size_t mPacketCount{0};
    bool mRegistrationInFlight{false};
    bool mDiscoveryInFlight{false};
    bool mRegistrationCompleteOnce{false};
    std::future<std::string> mRegistrationFuture;
    std::future<std::string> mDiscoveryFuture;

    sf::TcpListener mListener;
    sf::SocketSelector mSelector;
    std::vector<std::unique_ptr<sf::TcpSocket>> mPeerSockets;
    std::unordered_map<std::string, RemotePlayerState> mRemotePlayers;
    std::unordered_map<std::string, SimulatedPlayerState> mOfflineBots;
    std::unordered_set<std::string> mKnownPeers;

    // Lobby state
    bool mLobbyReady{false};
    int mConnectedPlayerCount{0};
    int mMinimumPlayers{2}; // 2 for debugging, can be changed to 4
    int mMaximumPlayers{4};
    float mLobbyStatusAccumulator{0.0f};

    // Local AI fallback mode (single-player + bots) before online lobby fills.
    bool mOfflineAIMode{true};
    float mOfflineRunnerSpeed{30.0f};
    float mOfflineStrafeLimit{30.0f};
    float mOfflineElapsedSeconds{0.0f};
    int mOfflineBotCount{2};
};

#endif // MULTIPLAYER_GAME_STATE_HPP
