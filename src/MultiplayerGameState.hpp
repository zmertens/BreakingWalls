#ifndef MULTIPLAYER_GAME_STATE_HPP
#define MULTIPLAYER_GAME_STATE_HPP

#include "State.hpp"

#include <SFML/Network.hpp>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

class GameState;
class MusicPlayer;

class MultiplayerGameState : public State
{
public:
    explicit MultiplayerGameState(StateStack& stack, Context context);

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event& event) noexcept override;

private:
    struct PeerInfo
    {
        std::string name;
        sf::IpAddress ip{ sf::IpAddress::LocalHost };
        unsigned short port{ 0 };
    };

    struct RemotePlayerState
    {
        glm::vec3 position{ 0.0f };
        float facing{ 0.0f };
        bool moving{ false };
        std::uint8_t animState{ 0 };
    };

    void initializeNetwork();
    void startListener();
    void discoverPeers();
    void connectToPeer(const PeerInfo& peer);
    void pollNetwork();
    void handlePacket(sf::Packet& packet);
    void sendLocalState(float dt);
    std::string loadNetworkUrl() const;
    std::vector<PeerInfo> parseActivePlayers(const std::string& json) const;

    std::unique_ptr<GameState> mLocalGame;
    MusicPlayer* mMusic;

    std::string mNetworkUrl;
    std::string mLocalPlayerName;
    unsigned short mLocalPort{ 0 };
    bool mNetworkReady{ false };
    float mSendAccumulator{ 0.0f };

    sf::TcpListener mListener;
    sf::SocketSelector mSelector;
    std::vector<std::unique_ptr<sf::TcpSocket>> mPeerSockets;
    std::unordered_map<std::string, RemotePlayerState> mRemotePlayers;

};

#endif // MULTIPLAYER_GAME_STATE_HPP
