#ifndef CHUNK_INDEX_H
#define CHUNK_INDEX_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "chunk.h"

// Spatial index over the (p, q) chunk grid: an O(1) hash map for exact lookup
// plus a sparse quadtree for rectangle (range) queries.
class chunk_index
{
public:
    // Quadtree covers [-HALF_EXTENT, HALF_EXTENT) in chunk coordinates; chunks outside use an overflow list.
    static constexpr int HALF_EXTENT{1024};
    static constexpr std::size_t LEAF_CAPACITY{8};

    chunk_index() : m_root{std::make_unique<node>(-HALF_EXTENT, -HALF_EXTENT, HALF_EXTENT * 2)} {}

    [[nodiscard]] std::optional<chunk *> find(const int p, const int q) const noexcept
    {
        const auto it = m_lookup.find(key(p, q));
        if (it == m_lookup.end())
            return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_lookup.size(); }

    // Inserts (or replaces) the chunk at (c->p, c->q).
    void insert(chunk *c)
    {
        if (c == nullptr)
            return;
        if (const auto existing = find(c->p, c->q); existing.has_value())
        {
            if (existing.value() == c)
                return;
            remove(existing.value());
        }
        m_lookup[key(c->p, c->q)] = c;
        if (in_range(c->p, c->q))
            insert_into(*m_root, c);
        else
            m_overflow.push_back(c);
    }

    void remove(const chunk *c)
    {
        if (c == nullptr)
            return;
        const auto it = m_lookup.find(key(c->p, c->q));
        if (it == m_lookup.end() || it->second != c)
            return;
        m_lookup.erase(it);
        if (in_range(c->p, c->q))
            remove_from(*m_root, c);
        else
            std::erase(m_overflow, c);
    }

    void clear()
    {
        m_lookup.clear();
        m_overflow.clear();
        m_root = std::make_unique<node>(-HALF_EXTENT, -HALF_EXTENT, HALF_EXTENT * 2);
    }

    // Visits every chunk with min_p <= p <= max_p and min_q <= q <= max_q.
    template <typename F>
    void query(const int min_p, const int min_q, const int max_p, const int max_q, F &&fn) const
    {
        query_node(*m_root, min_p, min_q, max_p, max_q, fn);
        for (chunk *c : m_overflow)
            if (c->p >= min_p && c->p <= max_p && c->q >= min_q && c->q <= max_q)
                fn(c);
    }

    template <typename F>
    void for_each(F &&fn) const
    {
        for (const auto &entry : m_lookup)
            fn(entry.second);
    }

private:
    struct node
    {
        int min_p, min_q, size;
        std::vector<chunk *> items{};
        std::array<std::unique_ptr<node>, 4> children{};
        bool split{false};
        node(const int p, const int q, const int s) : min_p{p}, min_q{q}, size{s} {}
    };

    static std::uint64_t key(const int p, const int q) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p)) << 32) |
               static_cast<std::uint32_t>(q);
    }

    static bool in_range(const int p, const int q) noexcept
    {
        return p >= -HALF_EXTENT && p < HALF_EXTENT && q >= -HALF_EXTENT && q < HALF_EXTENT;
    }

    static int child_slot(const node &n, const int p, const int q) noexcept
    {
        const int half = n.size / 2;
        return (p >= n.min_p + half ? 1 : 0) | (q >= n.min_q + half ? 2 : 0);
    }

    static node &ensure_child(node &n, const int slot)
    {
        if (!n.children[slot])
        {
            const int half = n.size / 2;
            n.children[slot] = std::make_unique<node>(n.min_p + ((slot & 1) ? half : 0),
                                                      n.min_q + ((slot & 2) ? half : 0), half);
        }
        return *n.children[slot];
    }

    static void insert_into(node &n, chunk *c)
    {
        if (!n.split)
        {
            n.items.push_back(c);
            if (n.items.size() <= LEAF_CAPACITY || n.size <= 1)
                return;
            n.split = true;
            std::vector<chunk *> moved;
            moved.swap(n.items);
            for (chunk *m : moved)
                insert_into(ensure_child(n, child_slot(n, m->p, m->q)), m);
            return;
        }
        insert_into(ensure_child(n, child_slot(n, c->p, c->q)), c);
    }

    // Returns true when the node became empty and can be pruned by its parent.
    static bool remove_from(node &n, const chunk *c)
    {
        if (!n.split)
        {
            std::erase(n.items, c);
            return n.items.empty();
        }
        const int slot = child_slot(n, c->p, c->q);
        if (n.children[slot] && remove_from(*n.children[slot], c))
            n.children[slot].reset();
        return std::ranges::none_of(n.children, [](const auto &ch) { return ch != nullptr; });
    }

    template <typename F>
    static void query_node(const node &n, const int min_p, const int min_q,
                           const int max_p, const int max_q, F &fn)
    {
        if (n.min_p > max_p || n.min_p + n.size - 1 < min_p ||
            n.min_q > max_q || n.min_q + n.size - 1 < min_q)
            return;
        if (!n.split)
        {
            for (chunk *c : n.items)
                if (c->p >= min_p && c->p <= max_p && c->q >= min_q && c->q <= max_q)
                    fn(c);
            return;
        }
        for (const auto &ch : n.children)
            if (ch)
                query_node(*ch, min_p, min_q, max_p, max_q, fn);
    }

    std::unique_ptr<node> m_root;
    std::unordered_map<std::uint64_t, chunk *> m_lookup;
    std::vector<chunk *> m_overflow;
};

#endif // CHUNK_INDEX_H
