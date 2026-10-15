#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace launcher {
namespace fs = std::filesystem;

struct Profile {
    std::string id;
    std::string name = "NEW COMBINATION";
    fs::path iwad;
    std::string renderer = "gl4es";
    std::vector<fs::path> addons;
};

struct Library {
    std::vector<fs::path> bases;
    std::vector<fs::path> addons;
    std::vector<std::string> notices;
};

std::string lower(std::string value);
Library scan(const std::vector<fs::path> &roots);
Profile initial_profile(const Library &library, const std::string &renderer);
Profile read_profile(const fs::path &path);
void write_profile(const fs::path &path, const Profile &profile);
void validate(const Profile &profile);
void select_base(Profile &profile, const fs::path &path);
void toggle_addon(Profile &profile, const fs::path &path);
void move_addon(Profile &profile, std::size_t index, int direction);
std::string save_key(const Profile &profile);
void write_selection(const fs::path &path, const Profile &profile);
std::string new_profile_id();
} // namespace launcher
