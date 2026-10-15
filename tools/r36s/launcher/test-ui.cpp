// Drive actual SDL menu with controller events; verify persisted/public output.
#define main launcher_program_main
#include "main.cpp"
#undef main

#include <cassert>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace {
using Steps = std::vector<SDL_GameControllerButton>;
constexpr auto A = SDL_CONTROLLER_BUTTON_A, B = SDL_CONTROLLER_BUTTON_B;
constexpr auto Down = SDL_CONTROLLER_BUTTON_DPAD_DOWN, Up = SDL_CONTROLLER_BUTTON_DPAD_UP;

int drive(const fs::path &game, const fs::path &output, Steps steps)
{
    App app(game,{},output,"gl4es",{});
    std::thread input([steps] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        for (auto button : steps) {
            SDL_Event event{};
            event.type = SDL_CONTROLLERBUTTONDOWN;
            event.cbutton.button = button;
            SDL_PushEvent(&event);
            event.type = SDL_CONTROLLERBUTTONUP;
            SDL_PushEvent(&event);
            std::this_thread::sleep_for(std::chrono::milliseconds(70));
        }
    });
    try { const auto result = app.run(); input.join(); return result; }
    catch (...) { input.join(); throw; }
}

std::string read(const fs::path &path)
{
    std::ifstream stream(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
}

int main()
{
    const auto root = fs::temp_directory_path()/ ("zandronum-menu-tests-"+std::to_string(getpid()));
    assert(!fs::exists(root));
    try {
        fs::create_directories(root/"iwads"); fs::create_directories(root/"mods");
        std::ofstream(root/"iwads/doom.wad") << "IWAD";
        std::ofstream(root/"iwads/doom2.wad") << "IWAD";
        const auto first = root/"mods/first map.wad", second = root/"mods/second; $(touch nope).pk3";
        std::ofstream(first) << "PWAD"; std::ofstream(second) << "PK";
        const auto output = root/"selection.bin", state = root/"config/launcher";
        // Pick Doom, toggle both add-ons, move second earlier, select software,
        // name profile AB using grid, save, then Play.
        const Steps create = {Down,A,A, Down,A,Down,Down,A,Down,A,B, Down,A,Down,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER,B, Down,A,Down,A, Down,A,Down,A,
            A,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,A,SDL_CONTROLLER_BUTTON_START,
            Up,Up,Up,Up,Up,A};
        assert(drive(root,output,create)==0);
        const auto saved = read_profile(state/"last.profile");
        assert(saved.name=="AB" && saved.iwad==root/"iwads/doom.wad" && saved.renderer=="software");
        assert(saved.addons==std::vector<fs::path>({second,first}));
        assert(read_profile(state/"profiles"/(saved.id+".profile")).addons==saved.addons);
        const auto selection = read(output);
        assert(!selection.empty());
        // Fresh current selection, then load named profile after complete SDL shutdown/restart.
        write_profile(state/"last.profile",initial_profile(scan({root/"iwads"}),"gl4es"));
        fs::remove(output);
        const Steps reload = {Down,Down,Down,Down,Down,A, Down,Down,Down,A, Up,Up,Up,Up,Up,A};
        assert(drive(root,output,reload)==0);
        assert(read(output)==selection);
        // Standalone PWAD base is selectable, never duplicated as an add-on.
        const auto standalone = root/"iwads/megagame.wad";
        std::ofstream(standalone) << "PWAD";
        write_profile(state/"last.profile",initial_profile(scan({root/"iwads"}),"gl4es"));
        fs::remove(output);
        const Steps custom = {Down,A,Down,Down,Down,A, Down,A,A,Down,Down,A,Down,A,Down,A,B, Up,Up,A};
        assert(drive(root,output,custom)==0);
        const auto custom_profile = read_profile(state/"last.profile");
        assert(custom_profile.iwad==standalone);
        assert(custom_profile.addons==std::vector<fs::path>({root/"iwads/doom.wad",first,second}));
        // Switch selected PK3 add-on into base role, then add PWAD alongside
        // existing IWAD and WAD add-ons. Base selection removes duplicate.
        fs::remove(output);
        const Steps pk3_base = {Down,A,Down,Down,Down,Down,A, Down,A,Down,Down,Down,A,B, Up,Up,A};
        assert(drive(root,output,pk3_base)==0);
        const auto pk3_profile = read_profile(state/"last.profile");
        assert(pk3_profile.iwad==second);
        assert(pk3_profile.addons==std::vector<fs::path>({root/"iwads/doom.wad",first,standalone}));
        // Missing add-on must keep menu editable and cancel without launch output.
        write_profile(state/"last.profile",saved);
        fs::remove(first); fs::remove(output);
        assert(drive(root,output,{A,B})==10);
        assert(!fs::exists(output));
        assert(!fs::exists(root/"nope"));
        fs::remove_all(root);
        std::cout << "PASS: controller menu selects WAD/PK3 bases and IWAD/PWAD/PK3 add-ons, avoids duplicate base, orders files, names and reloads profile, missing-file recovery and clean cancel\n";
    } catch (...) { fs::remove_all(root); throw; }
}
