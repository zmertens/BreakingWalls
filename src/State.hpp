<<<<<<< HEAD
#ifndef STATE_HPP
#define STATE_HPP

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "HttpClient.hpp"
#include "Loggable.hpp"
#include "Player.hpp"
#include "RenderWindow.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "SoundPlayer.hpp"
#include "StateIdentifiers.hpp"

#include <MazeBuilder/configurator.h>

class StateStack;

union SDL_Event;

class State : public Loggable
{
public:
    typedef std::unique_ptr<State> Ptr;

    // Named parameter idiom for passing shared context to states
    struct Context final
    {
        [[nodiscard]] RenderWindow *getRenderWindow() const noexcept { return mWindow.value_or(nullptr); }
        [[nodiscard]] FontManager *getFontManager() const noexcept { return mFonts.value_or(nullptr); }
        [[nodiscard]] LevelsManager *getLevelsManager() const noexcept { return mLevels.value_or(nullptr); }
        [[nodiscard]] ModelsManager *getModelsManager() const noexcept { return mModels.value_or(nullptr); }
        [[nodiscard]] MusicManager *getMusicManager() const noexcept { return mMusic.value_or(nullptr); }
        [[nodiscard]] OptionsManager *getOptionsManager() const noexcept { return mOptions.value_or(nullptr); }
        [[nodiscard]] SoundBufferManager *getSoundBufferManager() const noexcept { return mSoundBuffers.value_or(nullptr); }
        [[nodiscard]] SoundPlayer *getSoundPlayer() const noexcept { return mSounds.value_or(nullptr); }
        [[nodiscard]] ShaderManager *getShaderManager() const noexcept { return mShaders.value_or(nullptr); }
        [[nodiscard]] TextureManager *getTextureManager() const noexcept { return mTextures.value_or(nullptr); }
        [[nodiscard]] Player *getPlayer() const noexcept { return mPlayer.value_or(nullptr); }
        [[nodiscard]] HttpClient *getHttpClient() const noexcept { return mHttpClient.value_or(nullptr); }

        Context &withRenderWindow(RenderWindow &window)
        {
            mWindow = &window;
            return *this;
        }

        Context &withFontManager(FontManager &fonts)
        {
            mFonts = &fonts;
            return *this;
        }

        Context &withLevelsManager(LevelsManager &levels)
        {
            mLevels = &levels;
            return *this;
        }

        Context &withModelsManager(ModelsManager &models)
        {
            mModels = &models;
            return *this;
        }

        Context &withMusicManager(MusicManager &music)
        {
            mMusic = &music;
            return *this;
        }

        Context &withOptionsManager(OptionsManager &options)
        {
            mOptions = &options;
            return *this;
        }

        Context &withSoundBufferManager(SoundBufferManager &soundBuffers)
        {
            mSoundBuffers = &soundBuffers;
            return *this;
        }

        Context &withSoundPlayer(SoundPlayer &sounds)
        {
            mSounds = &sounds;
            return *this;
        }

        Context &withShaderManager(ShaderManager &shaders)
        {
            mShaders = &shaders;
            return *this;
        }

        Context &withTextureManager(TextureManager &textures)
        {
            mTextures = &textures;
            return *this;
        }

        Context &withPlayer(Player &player)
        {
            mPlayer = &player;
            return *this;
        }

        Context &withHttpClient(HttpClient &httpClient)
        {
            mHttpClient = &httpClient;
            return *this;
        }

    private:
        std::optional<RenderWindow *> mWindow;
        std::optional<FontManager *> mFonts;
        std::optional<LevelsManager *> mLevels;
        std::optional<ModelsManager *> mModels;
        std::optional<MusicManager *> mMusic;
        std::optional<OptionsManager *> mOptions;
        std::optional<SoundBufferManager *> mSoundBuffers;
        std::optional<SoundPlayer *> mSounds;
        std::optional<ShaderManager *> mShaders;
        std::optional<TextureManager *> mTextures;
        std::optional<Player *> mPlayer;
        std::optional<HttpClient *> mHttpClient;
    }; // Context struct

    explicit State(StateStack &stack, Context context);

    virtual ~State() = default;

    State(const State &) = delete;
    State &operator=(const State &) = delete;

    // Allow move constructor and move assignment operator
    State(State &&) = default;
    State &operator=(State &&) = default;

    virtual void draw() const noexcept = 0;
    virtual bool update(float dt, unsigned int subSteps) noexcept = 0;
    virtual bool handleEvent(const SDL_Event &event) noexcept = 0;

    virtual void log(std::string_view message, const char delimiter = '\n') noexcept override;
    virtual std::string_view view() const noexcept override;
    virtual std::string_view consumeView() noexcept override;

protected:
    void requestStackPush(States::ID stateID);
    void requestStackPop();
    void requestStateClear();

    Context getContext() const noexcept;

    StateStack &getStack() const noexcept;

    std::vector<std::string> mLogs;

private:
    StateStack *mStack;
    Context mContext;
    mutable std::string mLogViewBuffer;
};
#endif // STATE_HPP
=======
#ifndef STATE_HPP
#define STATE_HPP

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "HttpClient.hpp"
#include "Loggable.hpp"
#include "Player.hpp"
#include "RenderWindow.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "SoundPlayer.hpp"
#include "StateIdentifiers.hpp"

#include <MazeBuilder/configurator.h>

class StateStack;

union SDL_Event;

class State : public Loggable
{
public:
    typedef std::unique_ptr<State> Ptr;

    // Named parameter idiom for passing shared context to states
    struct Context final
    {
        [[nodiscard]] RenderWindow *getRenderWindow() const noexcept { return mWindow.value_or(nullptr); }
        [[nodiscard]] FontManager *getFontManager() const noexcept { return mFonts.value_or(nullptr); }
        [[nodiscard]] LevelsManager *getLevelsManager() const noexcept { return mLevels.value_or(nullptr); }
        [[nodiscard]] ModelsManager *getModelsManager() const noexcept { return mModels.value_or(nullptr); }
        [[nodiscard]] MusicManager *getMusicManager() const noexcept { return mMusic.value_or(nullptr); }
        [[nodiscard]] OptionsManager *getOptionsManager() const noexcept { return mOptions.value_or(nullptr); }
        [[nodiscard]] SoundBufferManager *getSoundBufferManager() const noexcept { return mSoundBuffers.value_or(nullptr); }
        [[nodiscard]] SoundPlayer *getSoundPlayer() const noexcept { return mSounds.value_or(nullptr); }
        [[nodiscard]] ShaderManager *getShaderManager() const noexcept { return mShaders.value_or(nullptr); }
        [[nodiscard]] TextureManager *getTextureManager() const noexcept { return mTextures.value_or(nullptr); }
        [[nodiscard]] Player *getPlayer() const noexcept { return mPlayer.value_or(nullptr); }
        [[nodiscard]] HttpClient *getHttpClient() const noexcept { return mHttpClient.value_or(nullptr); }

        Context &withRenderWindow(RenderWindow &window)
        {
            mWindow = &window;
            return *this;
        }

        Context &withFontManager(FontManager &fonts)
        {
            mFonts = &fonts;
            return *this;
        }

        Context &withLevelsManager(LevelsManager &levels)
        {
            mLevels = &levels;
            return *this;
        }

        Context &withModelsManager(ModelsManager &models)
        {
            mModels = &models;
            return *this;
        }

        Context &withMusicManager(MusicManager &music)
        {
            mMusic = &music;
            return *this;
        }

        Context &withOptionsManager(OptionsManager &options)
        {
            mOptions = &options;
            return *this;
        }

        Context &withSoundBufferManager(SoundBufferManager &soundBuffers)
        {
            mSoundBuffers = &soundBuffers;
            return *this;
        }

        Context &withSoundPlayer(SoundPlayer &sounds)
        {
            mSounds = &sounds;
            return *this;
        }

        Context &withShaderManager(ShaderManager &shaders)
        {
            mShaders = &shaders;
            return *this;
        }

        Context &withTextureManager(TextureManager &textures)
        {
            mTextures = &textures;
            return *this;
        }

        Context &withPlayer(Player &player)
        {
            mPlayer = &player;
            return *this;
        }

        Context &withHttpClient(HttpClient &httpClient)
        {
            mHttpClient = &httpClient;
            return *this;
        }

    private:
        std::optional<RenderWindow *> mWindow;
        std::optional<FontManager *> mFonts;
        std::optional<LevelsManager *> mLevels;
        std::optional<ModelsManager *> mModels;
        std::optional<MusicManager *> mMusic;
        std::optional<OptionsManager *> mOptions;
        std::optional<SoundBufferManager *> mSoundBuffers;
        std::optional<SoundPlayer *> mSounds;
        std::optional<ShaderManager *> mShaders;
        std::optional<TextureManager *> mTextures;
        std::optional<Player *> mPlayer;
        std::optional<HttpClient *> mHttpClient;
    }; // Context struct

    explicit State(StateStack &stack, Context context);

    virtual ~State() = default;

    State(const State &) = delete;
    State &operator=(const State &) = delete;

    // Allow move constructor and move assignment operator
    State(State &&) = default;
    State &operator=(State &&) = default;

    virtual void draw() const noexcept = 0;
    virtual bool update(float dt, unsigned int subSteps) noexcept = 0;
    virtual bool handleEvent(const SDL_Event &event) noexcept = 0;

    virtual bool isLoggable(const bool logCondition = false) const noexcept override;
protected:
    void requestStackPush(States::ID stateID);
    void requestStackPop();
    void requestStateClear();

    Context getContext() const noexcept;

    StateStack &getStack() const noexcept;

    bool mLogCondition{false};
private:
    StateStack *mStack;
    Context mContext;
};
#endif // STATE_HPP
>>>>>>> d3122ee0e58222ba762f9edf23a88344c9a14b0d
