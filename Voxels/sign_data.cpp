#include "sign_data.h"

#include <algorithm>

// Swap-removes matching entries; face is ignored when null.
int sign_data::sign_list_remove_if(std::vector<sign> &signs, const int x, const int y, const int z, const int *face)
{
    int result = 0;
    std::size_t i = 0;
    while (i < signs.size())
    {
        const sign &e = signs[i];
        if (e.x == x && e.y == y && e.z == z && (!face || e.face == *face))
        {
            signs[i] = signs.back();
            signs.pop_back();
            result++;
        }
        else
        {
            i++;
        }
    }
    return result;
}

void sign_data::sign_list_add(std::vector<sign> &signs, const int x, const int y, const int z, const int face, const std::string_view text)
{
    if (sign_data::sign_list_remove_if(std::ref(signs), x, y, z, &face))
    {
        return;
    }
    sign entry{};
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.face = face;
    if (const auto length = std::min(text.size(), MAX_SIGN_LENGTH - 1))
    {
        std::copy_n(text.data(), length, entry.text);
        signs.push_back(entry);
    }
}

int sign_data::sign_list_remove(std::vector<sign> &signs, const int x, const int y, const int z, const int face)
{
    return sign_data::sign_list_remove_if(std::ref(signs), x, y, z, &face);
}

int sign_data::sign_list_remove_all(std::vector<sign> &signs, const int x, const int y, const int z)
{
    return sign_data::sign_list_remove_if(std::ref(signs), x, y, z, nullptr);
}
