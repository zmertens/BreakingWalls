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
        static constexpr std::string_view CommitSHA = "'46bab02'";

        static constexpr std::string_view Timestamp = "2026-02-25T06:42:37";
        
        static constexpr std::string_view Version = "0.5.3";
    };

}

#endif // BW_BUILDINFO_H

