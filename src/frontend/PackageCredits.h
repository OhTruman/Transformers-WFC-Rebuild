// PC EXTENSION — third-party credit lines of a package build (not in the original game).
// The NVIDIA DLSS credit (user 2026-10-12: start-up splash, About if any, credits; "clean and unobtrusive", non-commercial
// project): one small line in the bottom-right corner on the start-up splash (the start-up movie and the boot logo movies
// Logo_Activision / Logo_Hasbro / Logo_HighMoon; not FMV_intro, the story cinematic) and while the credits movie plays.
// The original has no About screen, so there is none here. Compiled in only for DLSS package builds (CMake
// -DWFC_DLSS_PACKAGE=ON, configured by Systems' DLSS package build); every other build draws nothing and keeps the
// original frontend unchanged.
#pragma once
#include <cctype>
#include <string>

#ifndef WFC_DLSS_PACKAGE
#define WFC_DLSS_PACKAGE 0
#endif

namespace frontend {

constexpr bool kDlssPackage = WFC_DLSS_PACKAGE != 0;
// NVIDIA's attribution text (no logo artwork).
constexpr const char* kDlssCreditText =
    "NVIDIA DLSS: Copyright (c) NVIDIA Corporation. NVIDIA, the NVIDIA logo and DLSS are trademarks of NVIDIA Corporation.";

inline std::string lowerName(const std::string& s) {
    std::string l;
    for (char c : s) l += (char)std::tolower((unsigned char)c);
    return l;
}
// Game.PlayMovie of the Extras menu's Credits entry (FMV_credits).
inline bool isCreditsMovie(const std::string& movie) { return lowerName(movie).find("credits") != std::string::npos; }
// The boot logo chain's SeqAct_MoviePlayer movies (Logo_Activision, Logo_Hasbro, Logo_HighMoon).
inline bool isStartupLogoMovie(const std::string& movie) { return lowerName(movie).rfind("logo_", 0) == 0; }

// When the credit line is up: the start-up movie, a boot logo or the credits movie (movie = the playing movie's name),
// and only in a DLSS package build.
inline bool packageCreditShown(bool package, bool startupMovieUp, bool movieUp, const std::string& movie) {
    return package && (startupMovieUp || (movieUp && (isCreditsMovie(movie) || isStartupLogoMovie(movie))));
}

} // namespace frontend
