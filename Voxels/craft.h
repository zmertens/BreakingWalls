#ifndef CRAFT_H
#define CRAFT_H

#include <memory>
#include <string_view>

#include <MazeBuilder/singleton_base.h>

namespace mazes
{
    class randomizer;
}

/// @brief Coordinates the voxel subsystems and game logic
/// @file craft.h
class craft final : mazes::singleton_base<craft> {
    friend class singleton_base;
public:
    // Necessary cnstr/dstrct for pimpl idiom
    explicit craft();
    ~craft();

    void initialize(std::string_view title, const unsigned int w, const unsigned int h) noexcept;

    bool run(mazes::randomizer& rng) const noexcept;

    // -----------------------------------------------------------------------
    // Synchronous legacy path (kept for backward compatibility).
    // -----------------------------------------------------------------------
    [[nodiscard]] std::string artifacts() const noexcept;
    [[nodiscard]] bool is_download_ready() const noexcept;
    void reset_download_flag() const noexcept;

    // -----------------------------------------------------------------------
    // Async export — non-blocking path for the web front-end.
    // JS calls begin_export(), polls is_export_ready(), then calls get_export().
    // -----------------------------------------------------------------------
    void begin_export() const noexcept;
    [[nodiscard]] bool is_export_ready() const noexcept;
    [[nodiscard]] std::string get_export() noexcept;
    [[nodiscard]] std::string get_export_status() const noexcept;

    [[nodiscard]] std::string maze(const std::string &args) const noexcept;

    [[nodiscard]] std::string get_version() const noexcept;
private:
    struct craft_impl;

    std::unique_ptr<craft_impl> crafting_impl;
};

#endif // CRAFT_H
