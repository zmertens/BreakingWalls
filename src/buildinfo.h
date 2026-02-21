#ifndef BW_BUILDINFO_H
#define BW_BUILDINFO_H

#include <string_view>

/// @file buildinfo.h
/// @brief Build information for the application
/// @namespace bw
namespace bw
{
    /// @brief Build information for the application
    struct buildinfo
    {
        static constexpr std::string_view CommitSHA = "'63b98d7'";

        static constexpr std::string_view Timestamp = "2026-02-20T17:22:11";
        
        static constexpr std::string_view Version = "0.5.0";
    };

}

#endif // BW_BUILDINFO_H

