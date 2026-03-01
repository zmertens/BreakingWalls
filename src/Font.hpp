#ifndef FONT_H
#define FONT_H

#include <cwchar>

struct ImFont;

class Font
{
public:
    // Load font from memory (compressed data)
    bool loadFromMemoryCompressedTTF(const void *compressedData, std::size_t compressedSize, float pixelSize = 28.f);

    [[nodiscard]] ImFont *get() const;

private:
    ImFont *mFont = nullptr;
};

#endif // FONT_H
