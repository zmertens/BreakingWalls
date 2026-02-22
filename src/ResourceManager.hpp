#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <MazeBuilder/configurator.h>

#include <dearimgui/imgui.h>

#include "GLTFModel.hpp"

namespace mazes
{
    class configurator;
}

template <typename Resource, typename Identifier>
class ResourceManager
{
public:
    // Level loading
    void load(Identifier id, const std::vector<mazes::configurator> &configs, bool appendResults);

    // SoundBuffer loading and Model loading
    void load(Identifier id, std::string_view filename);

    // Music loading
    void load(Identifier id, std::string_view filename, float volume, bool loop);

    // Texture loading
    void load(Identifier id, std::string_view filename, std::uint32_t channelOffset);

    void load(Identifier id, unsigned int width, unsigned int height,
              const std::function<void(std::vector<std::uint8_t>&, int, int)> &generator,
              std::uint32_t channelOffset);

    // Font loading
    void load(Identifier id, const void *data, const std::size_t capacity);

    Resource &get(Identifier id);
    const Resource &get(Identifier id) const;

    /// Insert a pre-constructed resource
    void insert(Identifier id, std::unique_ptr<Resource> resource)
    {
        insertResource(id, std::move(resource));
    }

    void clear() noexcept
    {
        if constexpr (std::is_same_v<Resource, Shader>)
        {
            for (auto &[_, res] : mResourceMap)
            {
                res->cleanUp();
            }
        }
    }

    bool isEmpty() const noexcept { return mResourceMap.empty(); }

private:
    void insertResource(Identifier id, std::unique_ptr<Resource> resource);

private:
    std::map<Identifier, std::unique_ptr<Resource>> mResourceMap;
};

template <typename Resource, typename Identifier>
void ResourceManager<Resource, Identifier>::load(Identifier id, const std::vector<mazes::configurator> &configs, bool appendResults)
{
    // Create and load resource
    auto resource = std::make_unique<Resource>();

    if (!resource->load(configs, appendResults))
    {
        throw std::runtime_error("ResourceManager::load - Failed to load from config.");
    }

    // If loading successful, insert resource to map
    insertResource(id, std::move(resource));
}

template <typename Resource, typename Identifier>
void ResourceManager<Resource, Identifier>::load(Identifier id, std::string_view filename)
{
    // Create and load resource
    auto resource = std::make_unique<Resource>();

    if constexpr (std::is_same_v<Resource, GLTFModel>)
    {
        if (!resource->readFile(filename))
        {
            throw std::runtime_error("ResourceManager::load - Failed to load " + std::string(filename));
        }
    }
    else
    {
        if (!resource->loadFromFile(filename))
        {
            throw std::runtime_error("ResourceManager::load - Failed to load " + std::string(filename));
        }
    }

    // If loading successful, insert resource to map
    insertResource(id, std::move(resource));
}

template <typename Resource, typename Identifier>
void ResourceManager<Resource, Identifier>::load(Identifier id, unsigned int width, unsigned int height,
    const std::function<void(std::vector<std::uint8_t>&, int, int)> &generator,
    std::uint32_t channelOffset)
{
    // Create and load resource
    auto resource = std::make_unique<Resource>();

    if (!resource->loadProceduralTextures(width, height, generator, channelOffset))
    {
        throw std::runtime_error("ResourceManager::load - Failed to load noise texture");
    }

    // If loading successful, insert resource to map
    insertResource(id, std::move(resource));
}

template <typename Resource, typename Identifier>
void ResourceManager<Resource, Identifier>::load(Identifier id, std::string_view filename, float volume, bool loop)
{
    // Create and load resource
    auto resource = std::make_unique<Resource>();

    if (!resource->openFromFile(filename))
    {
        throw std::runtime_error("ResourceManager::load - Failed to load " + std::string(filename));
    }

    resource->setVolume(volume);
    resource->setLoop(loop);

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
void ResourceManager<Resource, Identifier>::load(Identifier id, const void *data, const std::size_t capacity)
{
    auto resource = std::make_unique<Resource>();
    if (!resource->loadFromMemoryCompressedTTF(data, capacity))
    {
        throw std::runtime_error("ResourceManager::load - Failed to load font from memory");
    }

    insertResource(id, std::move(resource));
}

template <typename Resource, typename Identifier>
Resource &ResourceManager<Resource, Identifier>::get(Identifier id)
{
    auto found = mResourceMap.find(id);
    assert(found != mResourceMap.cend());

    return *found->second;
}

template <typename Resource, typename Identifier>
const Resource &ResourceManager<Resource, Identifier>::get(Identifier id) const
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
