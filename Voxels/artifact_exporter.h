#ifndef ARTIFACT_EXPORTER_H
#define ARTIFACT_EXPORTER_H

#include <future>
#include <string>
#include <tuple>
#include <vector>

/// Handles async OBJ export of voxel block data.
class artifact_exporter
{
public:
    /// Convert block data to Wavefront OBJ text (synchronous, suitable for async launch).
    static std::string blocks_to_wavefront_obj(
        const std::vector<std::tuple<int, int, int, int>> &blocks) noexcept;

    /// Begin an asynchronous export.
    void start_async(std::vector<std::tuple<int, int, int, int>> blocks) noexcept;

    /// Returns true if an export is currently in progress (started but not yet ready).
    [[nodiscard]] bool is_running() const noexcept;

    /// Returns true if the async export has finished (or no export was started).
    [[nodiscard]] bool is_ready() const noexcept;

    /// Retrieve the result (waits if not yet done, caches it afterwards).
    [[nodiscard]] std::string get_result() noexcept;

private:
    std::future<std::string> m_future;
    std::string m_cached;
};

#endif // ARTIFACT_EXPORTER_H
