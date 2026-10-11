#pragma once
// Where the AI assets (.oceqs, .ocso, .ocst, .ocbt named from them) are looked up. Paths in components,
// assets and graph attributes are content-relative with '/'. One copy per module image: the wiring sets
// both the host's and the Synapse DLL's (GameSystemsWiring.hpp setAiContentRoot).
#include <string>

namespace aver::synapse {

void setAiContentRoot(std::string dir);
const std::string& aiContentRoot();
// Absolute paths and paths that exist as given come back unchanged, otherwise root + "/" + path.
std::string resolveAiAsset(const std::string& path);

} // namespace aver::synapse
