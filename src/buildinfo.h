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
        static constexpr std::string_view CommitSHA = "'b6110cc'";

        static constexpr std::string_view Timestamp = "2026-02-22T05:58:30";
        
        static constexpr std::string_view Version = "0.5.3";
    };

}

#endif // BW_BUILDINFO_H

