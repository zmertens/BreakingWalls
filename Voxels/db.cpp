#include "db.h"

#include <SDL3/SDL.h>

#include <algorithm>

void db::sign_list_alloc(sign_list *list, const std::size_t capacity)
{
    list->capacity = capacity;
    list->size = 0;
    list->data = capacity > 0 ? static_cast<sign *>(SDL_calloc(capacity, sizeof(sign))) : nullptr;
}

void db::sign_list_free(sign_list *list)
{
    SDL_free(list->data);
    list->data = nullptr;
    list->size = 0;
    list->capacity = 0;
}

void db::sign_list_grow(sign_list *list)
{
    const std::size_t capacity = list->capacity > 0 ? list->capacity * 2 : 16;
    auto *data = static_cast<sign *>(SDL_realloc(list->data, capacity * sizeof(sign)));
    if (!data)
    {
        return;
    }
    list->data = data;
    list->capacity = capacity;
}

// Swap-removes matching entries; face is ignored when null.
int db::sign_list_remove_if(sign_list *list, const int x, const int y, const int z, const int *face)
{
    int result = 0;
    std::size_t i = 0;
    while (i < list->size)
    {
        const sign &e = list->data[i];
        if (e.x == x && e.y == y && e.z == z && (!face || e.face == *face))
        {
            list->data[i] = list->data[--list->size];
            result++;
        }
        else
        {
            i++;
        }
    }
    return result;
}

void db::sign_list_add(sign_list *list, const int x, const int y, const int z, const int face, const std::string_view text)
{
    sign_list_remove_if(list, x, y, z, &face);
    if (list->size == list->capacity)
    {
        sign_list_grow(list);
        if (list->size == list->capacity)
        {
            return;
        }
    }
    sign entry{};
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.face = face;
    const auto length = std::min(text.size(), MAX_SIGN_LENGTH - 1);
    std::copy_n(text.data(), length, entry.text);
    list->data[list->size++] = entry;
}

int db::sign_list_remove(sign_list *list, const int x, const int y, const int z, const int face)
{
    return sign_list_remove_if(list, x, y, z, &face);
}

int db::sign_list_remove_all(sign_list *list, const int x, const int y, const int z)
{
    return sign_list_remove_if(list, x, y, z, nullptr);
}
