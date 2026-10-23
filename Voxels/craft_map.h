#ifndef CRAFT_MAP_H
#define CRAFT_MAP_H

#include <cstdint>
#include <vector>

// 3-D integer sparse voxel map.
//
// Implemented as an open-addressing hash table with linear probing.
// Keys are stored as unsigned-byte offsets from a (dx, dy, dz) base origin,
// so valid absolute coordinates must satisfy  0 ≤ (coord − base) ≤ 255.
//
// Supports range-for via a nested iterator that yields decoded absolute
// coordinates, replacing the old MAP_FOR_EACH / END_MAP_FOR_EACH macros.
class craft_map
{
public:
    // Value exposed by the iterator.
    struct voxel
    {
        int x, y, z, w;
    };

    // Forward iterator over occupied (non-zero) entries.
    struct iterator
    {
        const craft_map *m;
        unsigned int idx;
        voxel operator*() const noexcept;
        iterator &operator++() noexcept;
        bool operator!=(const iterator &o) const noexcept { return idx != o.idx; }
    };

    // Re-initialise in place (replaces the old map_alloc pattern on existing objects).
    void init(int dx, int dy, int dz, unsigned int mask);

    // Write a voxel. Returns 1 if the map changed, 0 otherwise.
    int set(int x, int y, int z, int w);

    // Read a voxel.  Returns 0 for out-of-range or absent coordinates.
    [[nodiscard]] int get(int x, int y, int z) const noexcept;

    // Range-for support.
    [[nodiscard]] iterator begin() const noexcept;
    [[nodiscard]] iterator end() const noexcept;

    [[nodiscard]] unsigned int size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] int dx() const noexcept { return m_dx; }
    [[nodiscard]] int dy() const noexcept { return m_dy; }
    [[nodiscard]] int dz() const noexcept { return m_dz; }

private:
    // Compact 4-byte entry: value == 0 means the slot is empty.
    union map_entry
    {
        unsigned int value;
        struct
        {
            unsigned char x;
            unsigned char y;
            unsigned char z;
            char w;
        } e;

        static bool is_empty(const map_entry &entry) noexcept
        {
            return entry.value == 0;
        }

        static bool is_occupied(const map_entry &entry) noexcept
        {
            return entry.e.w != 0;
        }
    };

    void grow();

    int m_dx{};
    int m_dy{};
    int m_dz{};
    unsigned int m_mask{};
    unsigned int m_size{};
    std::vector<map_entry> m_data;
};

#endif // CRAFT_MAP_H