#include "content.h"
#include "font.h"

#include <SDL.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace launcher;
enum class View { Home, Iwads, Addons, Order, Renderer, Profiles, Name };
enum class Action { None, Up, Down, Left, Right, Confirm, Back, Delete, Space, Save, Earlier, Later, Refresh };
struct Row { std::string label, value, detail; };
constexpr SDL_Color background{15,19,25,255}, panel{26,32,41,255}, accent{243,156,68,255};
constexpr SDL_Color ink{232,234,226,255}, muted{143,158,177,255}, selected{54,43,35,255};

std::string fit(std::string value, std::size_t width)
{
    if (value.size() > width) value = value.substr(0, width - 3) + "...";
    return value;
}

class App {
    fs::path state, output;
    std::vector<fs::path> roots;
    std::string default_renderer, capture;
    Library library;
    Profile profile;
    std::vector<fs::path> profiles;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    std::vector<SDL_GameController *> controllers;
    View view = View::Home;
    int cursor = 0, home_cursor = 0, keyboard_cursor = 0;
    std::string status, name;
    bool save_as_new = false, finished = false, back_held = false, start_held = false;
    int result = 10;
    Action repeating = Action::None;
    Uint32 repeat_at = 0;
    const std::string keyboard = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_. ";

    void color(SDL_Color value) { SDL_SetRenderDrawColor(renderer, value.r, value.g, value.b, value.a); }
    void rectangle(int x, int y, int width, int height, SDL_Color value)
    {
        color(value);
        SDL_Rect rect{x,y,width,height};
        SDL_RenderFillRect(renderer, &rect);
    }
    void text(int x, int y, const std::string &value, SDL_Color value_color = ink, int scale = 2)
    {
        color(value_color);
        for (unsigned char character : value) {
            const auto bitmap = glyph(character);
            for (int row = 0; row < 7; ++row)
                for (int column = 0; column < 5; ++column)
                    if (bitmap[row] & (1 << (4 - column))) {
                        SDL_Rect rect{x + column * scale, y + row * scale, scale, scale};
                        SDL_RenderFillRect(renderer, &rect);
                    }
            x += 6 * scale;
        }
    }
    void change(View next)
    {
        if (view == View::Home) home_cursor = cursor;
        if (view == View::Name) SDL_StopTextInput();
        view = next;
        cursor = next == View::Home ? home_cursor : 0;
        status.clear();
    }
    void rescan()
    {
        library = scan(roots);
        status = library.notices.empty() ? "Library refreshed" : library.notices.front();
    }
    void scan_profiles()
    {
        profiles.clear();
        const auto directory = state / "profiles";
        fs::create_directories(directory);
        for (const auto &entry : fs::directory_iterator(directory))
            if (entry.is_regular_file() && entry.path().extension() == ".profile") profiles.push_back(entry.path());
        std::sort(profiles.begin(), profiles.end());
    }
    std::vector<Row> rows() const
    {
        std::vector<Row> items;
        switch (view) {
        case View::Home:
            return {{"PLAY", "", "Launch this combination"},
                {"BASE GAME", profile.iwad.empty() ? "Choose WAD / PK3" : profile.iwad.filename().string(), profile.iwad.string()},
                {"ADD-ONS", std::to_string(profile.addons.size()) + " selected", "Select WAD and PK3 files"},
                {"LOAD ORDER", "Top to bottom", "Later add-ons can override earlier files"},
                {"RENDERER", profile.renderer == "software" ? "Software" : "GL4ES", "Software passes native-30 baseline; GL4ES has higher peaks"},
                {"PROFILES", profile.id.empty() ? "Unsaved" : "Saved", "Save or load named combinations"},
                {"EXIT", "Ports menu", "Return to frontend"}};
        case View::Iwads:
            for (const auto &path : library.bases)
                items.push_back({(path == profile.iwad ? "> " : "  ") + path.filename().string(), "", path.string()});
            break;
        case View::Addons:
            for (const auto &path : library.addons) {
                const auto found = std::find(profile.addons.begin(), profile.addons.end(), path);
                const auto prefix = path == profile.iwad ? "[BASE] " : found == profile.addons.end() ? "[ ] " : "[" + std::to_string(found - profile.addons.begin() + 1) + "] ";
                items.push_back({prefix + path.filename().string(), "", path.string()});
            }
            break;
        case View::Order:
            for (std::size_t i = 0; i < profile.addons.size(); ++i)
                items.push_back({std::to_string(i + 1) + ". " + profile.addons[i].filename().string(), "", profile.addons[i].string()});
            break;
        case View::Renderer:
            return {{"GL4ES", "Higher peaks", "Existing GL saves remain available with GL4ES"},
                {"SOFTWARE", "Native-30 tested", "Software saves remain separate from GL saves"}};
        case View::Profiles:
            items = {{"SAVE CURRENT", "", "Update current named profile"},
                {"SAVE AS NEW", "", "Keep existing profile and save another combination"},
                {"NEW COMBINATION", "", "Start fresh; existing profiles and saves stay available"}};
            for (const auto &path : profiles) {
                try {
                    const auto saved = read_profile(path);
                    items.push_back({saved.name, "", saved.iwad.filename().string() + " + " + std::to_string(saved.addons.size()) + " add-ons"});
                } catch (const std::exception &) {
                    items.push_back({"[UNREADABLE] " + path.stem().string(), "", "Profile needs repair before it can load"});
                }
            }
            break;
        case View::Name: break;
        }
        return items;
    }
    std::string title() const
    {
        switch (view) {
        case View::Home: return "ZANDRONUM";
        case View::Iwads: return "BASE GAME";
        case View::Addons: return "ADD-ONS";
        case View::Order: return "LOAD ORDER";
        case View::Renderer: return "RENDERER";
        case View::Profiles: return "PROFILES";
        case View::Name: return "NAME PROFILE";
        }
        return {};
    }
    void draw()
    {
        color(background);
        SDL_RenderClear(renderer);
        rectangle(0,0,640,5,accent);
        text(28,24,title(),ink,4);
        text(548,35,"R36S",accent,2);
        text(28,70,fit(view == View::Name ? "Controller keyboard" : profile.name, 47),muted);
        rectangle(28,96,584,1,panel);
        if (view == View::Name) {
            rectangle(28,112,584,44,panel);
            text(42,127,fit(name + "_", 44),accent);
            for (int i = 0; i < static_cast<int>(keyboard.size()); ++i) {
                const int x = 28 + (i % 10) * 59, y = 177 + (i / 10) * 47;
                rectangle(x,y,53,38,i == keyboard_cursor ? selected : panel);
                if (i == keyboard_cursor) rectangle(x,y,53,3,accent);
                text(x+17,y+10,keyboard[i] == ' ' ? "_" : std::string(1,keyboard[i]),ink,3);
            }
            text(28,391,status.empty() ? "START: SAVE PROFILE" : fit(status,48),accent);
            text(28,438,"A: ADD  X: DELETE  Y: SPACE  B: CANCEL",muted);
        } else {
            const auto items = rows();
            const int first = std::max(0,cursor - 6);
            if (items.empty()) {
                text(36,144,view == View::Order ? "NO ADD-ONS SELECTED" : "NO FILES FOUND",ink,3);
                text(36,196,"Copy local files to these folders:",muted);
                int y = 225;
                for (const auto &root : roots) { text(36,y,fit(root.string(),46),muted); y += 28; }
                text(36,340,"Y: RESCAN    B: BACK",accent);
            }
            for (int i = first; i < static_cast<int>(items.size()) && i < first + 7; ++i) {
                const int y = 108 + (i - first) * 40;
                if (i == cursor) { rectangle(28,y,584,36,selected); rectangle(28,y,4,36,accent); }
                text(44,y+10,fit(items[i].label,items[i].value.empty() ? 45 : 18),i == cursor ? ink : muted);
                if (!items[i].value.empty()) text(283,y+10,fit(items[i].value,26),i == cursor ? accent : muted);
            }
            if (!items.empty()) {
                auto detail = status.empty() ? items[std::min(cursor,static_cast<int>(items.size())-1)].detail : status;
                if (detail.size() > 48) {
                    detail += "     ";
                    const auto offset = (SDL_GetTicks()/350) % detail.size();
                    detail = (detail + detail).substr(offset,48);
                }
                text(28,405,fit(detail,48),status.empty() ? muted : accent);
                text(28,459,std::to_string(cursor+1) + "/" + std::to_string(items.size()),muted,1);
            } else if (!status.empty()) text(28,405,fit(status,48),accent);
            const auto help = view == View::Order ? "L1/R1: MOVE  X: REMOVE  B: DONE" :
                view == View::Addons ? "A: TOGGLE  B: DONE  Y: RESCAN" :
                view == View::Home ? "A: SELECT  B: EXIT  Y: RESCAN" : "A: SELECT  B: BACK  Y: RESCAN";
            text(28,438,help,muted);
        }
    }
    void begin_name(bool as_new)
    {
        save_as_new = as_new;
        name = profile.id.empty() ? "" : profile.name;
        keyboard_cursor = 0;
        change(View::Name);
        SDL_StartTextInput();
    }
    void persist_last() { write_profile(state / "last.profile",profile); }
    void play()
    {
        validate(profile);
        persist_last();
        write_selection(output,profile);
        std::cout << "Launcher selected IWAD=" << profile.iwad << " Renderer=" << profile.renderer
                  << " SaveSet=" << save_key(profile) << '\n';
        for (const auto &path : profile.addons) std::cout << "Add-on (ordered): " << path << '\n';
        result = 0;
        finished = true;
    }
    void action(Action pressed)
    {
        if (view == View::Name) {
            if (pressed == Action::Up) keyboard_cursor = (keyboard_cursor + 30) % 40;
            else if (pressed == Action::Down) keyboard_cursor = (keyboard_cursor + 10) % 40;
            else if (pressed == Action::Left) keyboard_cursor = (keyboard_cursor + 39) % 40;
            else if (pressed == Action::Right) keyboard_cursor = (keyboard_cursor + 1) % 40;
            else if (pressed == Action::Delete && !name.empty()) name.pop_back();
            else if (pressed == Action::Space && name.size() < 32) name += ' ';
            else if (pressed == Action::Confirm && name.size() < 32) name += keyboard[keyboard_cursor];
            else if (pressed == Action::Back) change(View::Profiles);
            else if (pressed == Action::Save) {
                if (name.find_first_not_of(' ') == std::string::npos) { status = "Enter a profile name"; return; }
                Profile saved = profile;
                saved.name = name;
                if (saved.id.empty() || save_as_new) saved.id = new_profile_id();
                write_profile(state / "profiles" / (saved.id + ".profile"),saved);
                profile = saved;
                persist_last();
                scan_profiles();
                change(View::Home);
                status = "Profile saved";
            }
            return;
        }
        const auto items = rows();
        if (pressed == Action::Up && !items.empty()) cursor = (cursor + static_cast<int>(items.size()) - 1) % items.size();
        else if (pressed == Action::Down && !items.empty()) cursor = (cursor + 1) % items.size();
        else if (pressed == Action::Back) {
            if (view == View::Home) { persist_last(); finished = true; }
            else change(View::Home);
        } else if (pressed == Action::Refresh) {
            rescan(); scan_profiles(); cursor = 0;
        } else if (view == View::Order && !profile.addons.empty()) {
            if (pressed == Action::Delete) profile.addons.erase(profile.addons.begin()+cursor);
            else if (pressed == Action::Earlier && cursor > 0) { move_addon(profile,cursor,-1); --cursor; }
            else if (pressed == Action::Later && cursor+1 < static_cast<int>(profile.addons.size())) { move_addon(profile,cursor,1); ++cursor; }
            cursor = std::min(cursor,std::max(0,static_cast<int>(profile.addons.size())-1));
        } else if (pressed == Action::Confirm && !items.empty()) {
            switch (view) {
            case View::Home:
                if (cursor == 0) play();
                else if (cursor == 1) change(View::Iwads);
                else if (cursor == 2) change(View::Addons);
                else if (cursor == 3) change(View::Order);
                else if (cursor == 4) { change(View::Renderer); cursor = profile.renderer == "software" ? 1 : 0; }
                else if (cursor == 5) { scan_profiles(); change(View::Profiles); }
                else { persist_last(); finished = true; }
                break;
            case View::Iwads: select_base(profile,library.bases[cursor]); change(View::Home); break;
            case View::Addons: toggle_addon(profile,library.addons[cursor]); break;
            case View::Renderer: profile.renderer = cursor == 0 ? "gl4es" : "software"; change(View::Home); break;
            case View::Profiles:
                if (cursor < 2) begin_name(cursor == 1);
                else if (cursor == 2) { profile = initial_profile(library,default_renderer); change(View::Home); }
                else { profile = read_profile(profiles[cursor-3]); change(View::Home); status = "Profile loaded"; }
                break;
            case View::Order: case View::Name: break;
            }
        }
    }
    void controller(int device)
    {
        if (!SDL_IsGameController(device)) return;
        const auto id = SDL_JoystickGetDeviceInstanceID(device);
        for (auto *pad : controllers)
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == id) return;
        if (auto *pad = SDL_GameControllerOpen(device)) {
            controllers.push_back(pad);
            std::cout << "Launcher controller: " << SDL_GameControllerName(pad) << '\n';
        }
    }
    Action button(Uint8 value)
    {
        switch (value) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return Action::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Action::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Action::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Action::Right;
        case SDL_CONTROLLER_BUTTON_A: return Action::Confirm;
        case SDL_CONTROLLER_BUTTON_B: return Action::Back;
        case SDL_CONTROLLER_BUTTON_X: return Action::Delete;
        case SDL_CONTROLLER_BUTTON_Y: return view == View::Name ? Action::Space : Action::Refresh;
        case SDL_CONTROLLER_BUTTON_BACK: return Action::Back;
        case SDL_CONTROLLER_BUTTON_START: return Action::Save;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Action::Earlier;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Action::Later;
        default: return Action::None;
        }
    }
    static bool direction(Action value)
    {
        return value == Action::Up || value == Action::Down || value == Action::Left || value == Action::Right;
    }
    void dispatch(Action value, bool repeat = false)
    {
        if (value == Action::None) return;
        if (repeat && direction(value)) { repeating = value; repeat_at = SDL_GetTicks()+350; }
        try { action(value); }
        catch (const std::exception &error) { status = error.what(); std::cerr << "Launcher: " << status << '\n'; }
    }
    void event(const SDL_Event &event)
    {
        if (event.type == SDL_QUIT) { persist_last(); finished = true; }
        else if (event.type == SDL_CONTROLLERDEVICEADDED) controller(event.cdevice.which);
        else if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
            const bool down = event.type == SDL_CONTROLLERBUTTONDOWN;
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) back_held = down;
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) start_held = down;
            if (back_held && start_held) { persist_last(); finished = true; return; }
            const auto value = button(event.cbutton.button);
            if (down) dispatch(value,true);
            else if (value == repeating) repeating = Action::None;
        } else if (event.type == SDL_CONTROLLERAXISMOTION && (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)) {
            Action value = Action::None;
            if (event.caxis.value < -18000) value = event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? Action::Left : Action::Up;
            if (event.caxis.value > 18000) value = event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? Action::Right : Action::Down;
            if (value == Action::None) repeating = Action::None;
            else if (value != repeating) dispatch(value,true);
        } else if (event.type == SDL_TEXTINPUT && view == View::Name) {
            for (const char c : std::string(event.text.text))
                if (name.size() < 32 && keyboard.find(static_cast<char>(std::toupper(static_cast<unsigned char>(c)))) != std::string::npos) name += c;
        } else if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
            case SDLK_UP: dispatch(Action::Up); break;
            case SDLK_DOWN: dispatch(Action::Down); break;
            case SDLK_LEFT: dispatch(Action::Left); break;
            case SDLK_RIGHT: dispatch(Action::Right); break;
            case SDLK_RETURN: dispatch(view == View::Name ? Action::Save : Action::Confirm); break;
            case SDLK_ESCAPE: dispatch(Action::Back); break;
            case SDLK_BACKSPACE: dispatch(Action::Delete); break;
            case SDLK_PAGEUP: dispatch(Action::Earlier); break;
            case SDLK_PAGEDOWN: dispatch(Action::Later); break;
            case SDLK_F5: dispatch(Action::Refresh); break;
            default: break;
            }
        }
    }
public:
    App(fs::path game, std::vector<fs::path> content_roots, fs::path selection, std::string preferred, std::string screenshot)
        : state(game/"config/launcher"), output(std::move(selection)), roots(std::move(content_roots)),
          default_renderer(std::move(preferred)), capture(std::move(screenshot))
    {
        roots.push_back(game/"iwads"); roots.push_back(game/"mods");
        std::ifstream paths(state/"paths.txt");
        for (std::string line; std::getline(paths,line);)
            if (!line.empty() && line.front() != '#' && fs::path(line).is_absolute()) roots.emplace_back(line);
        rescan();
        profile = initial_profile(library,default_renderer);
        if (fs::exists(state/"last.profile")) {
            try { profile = read_profile(state/"last.profile"); }
            catch (const std::exception &error) { status = "Last selection could not load; choose again"; std::cerr << error.what() << '\n'; }
        }
        scan_profiles();
    }
    ~App()
    {
        for (auto *pad : controllers) SDL_GameControllerClose(pad);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
    int run()
    {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
        if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER) != 0) throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("Zandronum profiles",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,640,480,SDL_WINDOW_FULLSCREEN);
        if (!window) throw std::runtime_error(SDL_GetError());
        renderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) renderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        SDL_RenderSetLogicalSize(renderer,640,480);
        SDL_ShowCursor(SDL_DISABLE);
        for (int i = 0; i < SDL_NumJoysticks(); ++i) controller(i);
        SDL_RendererInfo info{};
        SDL_GetRendererInfo(renderer,&info);
        std::cout << "Launcher renderer=" << info.name << " controllers=" << controllers.size() << '\n' << std::flush;
        if (!capture.empty()) {
            draw();
            int width = 0, height = 0;
            if (SDL_GetRendererOutputSize(renderer,&width,&height) != 0) throw std::runtime_error(SDL_GetError());
            auto *surface = SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
            if (!surface) throw std::runtime_error(SDL_GetError());
            const auto cleanup = std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)>(surface,SDL_FreeSurface);
            if (SDL_RenderReadPixels(renderer,nullptr,surface->format->format,surface->pixels,surface->pitch) != 0 || SDL_SaveBMP(surface,capture.c_str()) != 0)
                throw std::runtime_error(SDL_GetError());
            return 0;
        }
        while (!finished) {
            SDL_Event incoming;
            while (!finished && SDL_PollEvent(&incoming)) event(incoming);
            if (repeating != Action::None && SDL_TICKS_PASSED(SDL_GetTicks(),repeat_at)) {
                dispatch(repeating); repeat_at = SDL_GetTicks()+120;
            }
            draw();
            SDL_RenderPresent(renderer);
            SDL_Delay(15);
        }
        return result;
    }
};
} // namespace

int main(int argc, char **argv)
{
    try {
        fs::path game, selection;
        std::vector<fs::path> roots;
        std::string renderer = "gl4es", capture;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--help") {
                std::cout << "r36s-launcher --game-dir PATH --output PATH [--content-root PATH] [--renderer gl4es|software] [--capture BMP]\n";
                return 0;
            }
            if (++i == argc) throw std::runtime_error("Missing option value");
            if (argument == "--game-dir") game = fs::absolute(argv[i]);
            else if (argument == "--output") selection = fs::absolute(argv[i]);
            else if (argument == "--content-root") roots.emplace_back(fs::absolute(argv[i]));
            else if (argument == "--renderer") renderer = argv[i];
            else if (argument == "--capture") capture = argv[i];
            else throw std::runtime_error("Unknown option: " + argument);
        }
        if (game.empty() || selection.empty() || (renderer != "gl4es" && renderer != "software"))
            throw std::runtime_error("Specify game directory, selection output and valid renderer");
        return App(game,std::move(roots),selection,renderer,capture).run();
    } catch (const std::exception &error) {
        std::cerr << "Launcher error: " << error.what() << '\n';
        return 1;
    }
}
