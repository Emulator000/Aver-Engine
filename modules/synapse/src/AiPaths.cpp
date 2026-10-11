#include "aver/synapse/AiPaths.hpp"

#include <filesystem>
#include <system_error>

namespace aver::synapse {
namespace {
std::string& root() {
    static std::string r;
    return r;
}
} // namespace

void setAiContentRoot(std::string dir) {
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    root() = std::move(dir);
}

const std::string& aiContentRoot() { return root(); }

std::string resolveAiAsset(const std::string& path) {
    if (path.empty() || root().empty()) return path;
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.is_absolute() || std::filesystem::exists(p, ec)) return path;
    return root() + "/" + path;
}

} // namespace aver::synapse
