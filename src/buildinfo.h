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
        static constexpr std::string_view CommitSHA = "'b25119e'";

        static constexpr std::string_view Timestamp = "2026-03-01T09:37:02";
        
        static constexpr std::string_view Version = "0.5.4";
    };

}

#endif // BW_BUILDINFO_H

