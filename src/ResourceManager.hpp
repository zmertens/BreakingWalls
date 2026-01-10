#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include <dearimgui/imgui.h>

struct SDL_Window;

template <typename Resource, typename Identifier>
class ResourceManager
{
public:

    void load(SDL_Window* window, Identifier id, std::string_view filename);

    void load(Identifier id, std::string_view filename, std::uint32_t channelOffset = 0);

    /// Load texture from maze string with cell size
    void loadFromStr(Identifier id, std::string_view mazeStr, int cellSize);

    template <typename Parameter>
    void load(SDL_Window* window, Identifier id, std::string_view filename, const Parameter& secondParam);

    template <typename Parameter1, typename Parameter2, typename PixelSize = float>
    void load(Identifier id, const Parameter1& param1, const Parameter2& param2, const PixelSize& pixelSize);

    Resource& get(Identifier id);
    const Resource& get(Identifier id) const;

    void clear() noexcept
    {
        mResourceMap.clear();
    }

    bool isEmpty() const noexcept { return mResourceMap.empty(); }

private:
    void insertResource(Identifier id, std::unique_ptr<Resource> resource);

private:
    std::map<Identifier, std::unique_ptr<Resource>> mResourceMap;
};

template <typename Resource, typename Identifier>
void ResourceManager<Resource, Identifier>::load(SDL_Window* window, Identifier id, std::string_view filename)
{
    // Create and load resource
    auto resource = std::make_unique<Resource>();

    if (!resource->loadFromFile(filename))
    {
        throw std::runtime_error("ResourceManager::load - Failed to load " + std::string(filename));
    }

    // If loading successful, insert resource to map
    insertResource(id, std::move(resource));
}

template <typename Resource, typename Identifier>
void ResourceManager<Resource, Identifier>::load(Identifier id, std::string_view filename, std::uint32_t channelOffset)
{
    // Create and load resource
    auto resource = std::make_unique<Resource>();

    if (!resource->loadFromFile(filename, channelOffset))
    {
        throw std::runtime_error("ResourceManager::load - Failed to load " + std::string{filename});
    }

    // If loading successful, insert resource to map
    insertResource(id, std::move(resource));
}

template <typename Resource, typename Identifier>
void ResourceManager<Resource, Identifier>::loadFromStr(Identifier id, std::string_view mazeStr, int cellSize)
{
    auto resource = std::make_unique<Resource>();

    if (!resource->loadFromStr(mazeStr, cellSize))
    {
        throw std::runtime_error("ResourceManager::loadFromStr - Failed to load from maze string");
    }

    insertResource(id, std::move(resource));
}

template <typename Resource, typename Identifier>
template <typename Parameter1, typename Parameter2, typename PixelSize>
void ResourceManager<Resource, Identifier>::load(Identifier id, const Parameter1& param1, const Parameter2& param2, const PixelSize& pixelSize)
{
    auto resource = std::make_unique<Resource>();
    if (!resource->loadFromMemoryCompressedTTF(param1, param2, pixelSize))
    {
        throw std::runtime_error("ResourceManager::load - Failed to load font from memory");
    }

    insertResource(id, std::move(resource));
}

template <typename Resource, typename Identifier>
Resource& ResourceManager<Resource, Identifier>::get(Identifier id)
{
    auto found = mResourceMap.find(id);
    assert(found != mResourceMap.cend());

    return *found->second;
}

template <typename Resource, typename Identifier>
const Resource& ResourceManager<Resource, Identifier>::get(Identifier id) const
{
    auto found = mResourceMap.find(id);
    assert(found != mResourceMap.cend());

    return *found->second;
}

template <typename Resource, typename Identifier>
void ResourceManager<Resource, Identifier>::insertResource(Identifier id, std::unique_ptr<Resource> resource)
{
    // Insert and check success
    auto inserted = mResourceMap.insert(std::make_pair(id, std::move(resource)));
    assert(inserted.second);
}

#endif // RESOURCE_MANAGER_HPP
