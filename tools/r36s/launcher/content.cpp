#include "content.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace launcher {
namespace {
constexpr std::size_t max_addons = 128;

bool content_type(const fs::path &path)
{
    const auto extension = lower(path.extension().string());
    return extension == ".wad" || extension == ".pk3";
}

void safe_string(const std::string &value)
{
    if (value.find('\0') != std::string::npos)
        throw std::runtime_error("File name contains a zero byte");
}

void atomic_write(const fs::path &path, const std::string &data)
{
    fs::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp." + std::to_string(getpid());
    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.close();
        fs::rename(temporary, path);
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
} // namespace

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

Library scan(const std::vector<fs::path> &roots)
{
    Library result;
    std::set<fs::path> seen;
    std::size_t visited = 0;
    for (const auto &root : roots) {
        std::error_code error;
        if (!fs::is_directory(root, error)) continue;
        fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, error);
        const fs::recursive_directory_iterator end;
        while (iterator != end && visited < 20000) {
            const auto entry = *iterator;
            ++visited;
            const auto name = entry.path().filename().string();
            if (!name.empty() && name.front() == '.') {
                if (entry.is_directory(error)) iterator.disable_recursion_pending();
            } else if (entry.is_directory(error)) {
                if (iterator.depth() >= 6) iterator.disable_recursion_pending();
            } else if (entry.is_regular_file(error) && content_type(entry.path())) {
                const auto path = fs::canonical(entry.path(), error);
                if (!error && seen.insert(path).second) {
                    // Owner chooses each file's role; engine checks compatibility.
                    result.bases.push_back(path);
                    result.addons.push_back(path);
                }
            }
            iterator.increment(error);
            if (error) {
                result.notices.push_back("Some folders could not be read");
                error.clear();
            }
        }
    }
    if (visited >= 20000) result.notices.push_back("Library limit reached; use smaller content folders");
    const auto sort_paths = [](auto &paths) {
        std::sort(paths.begin(), paths.end(), [](const auto &a, const auto &b) {
            const auto left = lower(a.filename().string()), right = lower(b.filename().string());
            return left == right ? a.string() < b.string() : left < right;
        });
    };
    sort_paths(result.bases);
    sort_paths(result.addons);
    return result;
}

Profile initial_profile(const Library &library, const std::string &renderer)
{
    Profile profile;
    profile.renderer = renderer;
    if (!library.bases.empty()) profile.iwad = library.bases.front();
    return profile;
}

Profile read_profile(const fs::path &path)
{
    if (fs::file_size(path) > 1024 * 1024) throw std::runtime_error("Profile is too large");
    std::ifstream stream(path, std::ios::binary);
    std::string header;
    std::getline(stream, header);
    if (header != "ZANDRONUM_LAUNCH_PROFILE 1") throw std::runtime_error("Unsupported profile format");
    Profile profile;
    std::set<std::string> fields;
    std::string field, value;
    while (stream >> field) {
        stream >> std::ws;
        if (stream.peek() != '"' || !(stream >> std::quoted(value)))
            throw std::runtime_error("Incomplete profile entry");
        safe_string(value);
        if (field != "addon" && !fields.insert(field).second)
            throw std::runtime_error("Duplicate profile entry");
        if (field == "id") profile.id = value;
        else if (field == "name") profile.name = value;
        else if (field == "iwad") profile.iwad = value;
        else if (field == "renderer") profile.renderer = value;
        else if (field == "addon" && profile.addons.size() < max_addons) profile.addons.emplace_back(value);
        else throw std::runtime_error("Unknown or excessive profile entry");
    }
    if (!stream.eof() || fields != std::set<std::string>{"id", "name", "iwad", "renderer"})
        throw std::runtime_error("Incomplete profile");
    if (!profile.id.empty() && (profile.id.rfind("profile-", 0) != 0 || profile.id.size() > 64
        || profile.id.size() <= 8 || profile.id.find_first_not_of("0123456789", 8) != std::string::npos))
        throw std::runtime_error("Invalid profile identifier");
    if (profile.renderer != "gl4es" && profile.renderer != "software" && profile.renderer != "gles")
        throw std::runtime_error("Unknown renderer");
    return profile;
}

void write_profile(const fs::path &path, const Profile &profile)
{
    std::ostringstream stream;
    stream << "ZANDRONUM_LAUNCH_PROFILE 1\nid " << std::quoted(profile.id)
           << "\nname " << std::quoted(profile.name)
           << "\niwad " << std::quoted(profile.iwad.string())
           << "\nrenderer " << std::quoted(profile.renderer) << '\n';
    for (const auto &addon : profile.addons) stream << "addon " << std::quoted(addon.string()) << '\n';
    atomic_write(path, stream.str());
}

void validate(const Profile &profile)
{
    safe_string(profile.iwad.string());
    if (!fs::is_regular_file(profile.iwad) || !content_type(profile.iwad))
        throw std::runtime_error("Choose an existing base WAD or PK3");
    if (profile.renderer != "gl4es" && profile.renderer != "software" && profile.renderer != "gles")
        throw std::runtime_error("Choose native GLES, GL4ES or software renderer");
    if (profile.addons.size() > max_addons) throw std::runtime_error("Too many add-ons (maximum 128)");
    std::set<fs::path> seen{fs::canonical(profile.iwad)};
    for (const auto &path : profile.addons) {
        safe_string(path.string());
        if (!path.is_absolute() || !fs::is_regular_file(path) || !content_type(path))
            throw std::runtime_error("Missing add-on: " + path.filename().string());
        if (!seen.insert(fs::canonical(path)).second) throw std::runtime_error("Duplicate content file");
    }
    if (!profile.iwad.is_absolute()) throw std::runtime_error("Base game path must be absolute");
}

void select_base(Profile &profile, const fs::path &path)
{
    profile.iwad = path;
    profile.addons.erase(std::remove(profile.addons.begin(), profile.addons.end(), path), profile.addons.end());
}

void toggle_addon(Profile &profile, const fs::path &path)
{
    if (path == profile.iwad) throw std::runtime_error("This file is already the base game");
    const auto found = std::find(profile.addons.begin(), profile.addons.end(), path);
    if (found != profile.addons.end()) profile.addons.erase(found);
    else if (profile.addons.size() < max_addons) profile.addons.push_back(path);
    else throw std::runtime_error("Too many add-ons (maximum 128)");
}

void move_addon(Profile &profile, std::size_t index, int direction)
{
    const auto next = static_cast<std::ptrdiff_t>(index) + direction;
    if (index < profile.addons.size() && next >= 0 && static_cast<std::size_t>(next) < profile.addons.size())
        std::swap(profile.addons[index], profile.addons[static_cast<std::size_t>(next)]);
}

std::string save_key(const Profile &profile)
{
    const auto name = lower(profile.iwad.filename().string());
    if (profile.addons.empty() && (name == "doom.wad" || name == "doom2.wad")) return "base";
    // Length-delimited paths preserve order and avoid ambiguous concatenation.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto append = [&hash](const fs::path &path) {
        const auto text = path.string();
        const auto value = std::to_string(text.size()) + ":" + text;
        for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ULL; }
    };
    append(profile.iwad);
    for (const auto &path : profile.addons) append(path);
    std::ostringstream stream;
    stream << "set-" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

void write_selection(const fs::path &path, const Profile &profile)
{
    validate(profile);
    std::string data;
    for (const auto &value : {profile.renderer, profile.iwad.string(), save_key(profile)})
        data += value + '\0';
    for (const auto &addon : profile.addons) data += addon.string() + '\0';
    atomic_write(path, data);
}

std::string new_profile_id()
{
    return "profile-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
}
} // namespace launcher
