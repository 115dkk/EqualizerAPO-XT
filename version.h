#define MAJOR 2
#define MINOR 35
#define REVISION 0

// Audit #250 F019: the "MAJOR.MINOR, append REVISION when non-zero" display
// rule used to be copied into four binaries; in the UpdateChecker that string
// feeds the update decision, so a divergent copy changes the verdict. This is
// the one implementation. (Guarded: version.h is also included from resource
// scripts.)
#ifdef __cplusplus
#include <string>
inline std::wstring eapoDisplayVersionW()
{
	std::wstring version =
		std::to_wstring(MAJOR) + L"." + std::to_wstring(MINOR);
	if (REVISION != 0)
		version += L"." + std::to_wstring(REVISION);
	return version;
}

inline std::string eapoDisplayVersion()
{
	std::string version =
		std::to_string(MAJOR) + "." + std::to_string(MINOR);
	if (REVISION != 0)
		version += "." + std::to_string(REVISION);
	return version;
}
#endif

// Canonical GitHub repository for release and update URLs, consumed by the
// Editor's Velopack bootstrap, the UpdateChecker, and the auto-detect
// installer so the location is written down once.
#define EAPO_REPO_SLUG "115dkk/EqualizerAPO-XT"
#define EAPO_REPO_URL "https://github.com/" EAPO_REPO_SLUG
#define EAPO_REPO_SLUG_W L"115dkk/EqualizerAPO-XT"
#define EAPO_REPO_URL_W L"https://github.com/" EAPO_REPO_SLUG_W
