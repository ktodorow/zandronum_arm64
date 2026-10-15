#include "content.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <unistd.h>

using namespace launcher;

namespace {
void file(const fs::path &path, const std::string &bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary) << bytes;
}

template<class Function> void rejects(Function operation)
{
    bool rejected = false;
    try { operation(); } catch (const std::exception &) { rejected = true; }
    assert(rejected);
}

std::string bytes(const fs::path &path)
{
    std::ifstream stream(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
}

int main()
{
    const auto root = fs::temp_directory_path()/ ("zandronum-launcher-tests-"+std::to_string(getpid()));
    assert(!fs::exists(root));
    try {
        const auto library = root/"content";
        const auto doom = library/"DOOM2.WAD";
        const auto standalone = library/"megagame.wad";
        const auto patch = library/"nested"/"map pack.wad";
        const auto pk3 = library/"weapons; $(touch nope) \"edition\".PK3";
        file(doom,"IWADtest");
        file(standalone,"PWADtest");
        file(patch,"PWADtest");
        file(pk3,"PK\003\004test");
        file(library/"._DOOM2.WAD","IWADignored");
        file(library/".hidden"/"private.wad","IWADignored");
        file(library/"readme.txt","not content");
        fs::create_symlink(doom,library/"duplicate.wad");
        auto found = scan({library,library/"nested"});
        assert(found.bases.size()==4);
        assert(std::find(found.bases.begin(),found.bases.end(),fs::canonical(standalone))!=found.bases.end());
        assert(std::find(found.bases.begin(),found.bases.end(),fs::canonical(patch))!=found.bases.end());
        assert(found.addons==found.bases);
        assert(initial_profile(Library{{standalone,doom},{},{}},"gl4es").iwad==standalone);
        // Every discovered format/header can take either role. Engine owns
        // compatibility; launcher only rejects missing/duplicate content.
        for (const auto &base : found.bases) {
            Profile combination;
            for (const auto &path : found.addons) toggle_addon(combination,path);
            select_base(combination,base);
            assert(combination.addons.size()==3);
            validate(combination);
            rejects([&]{toggle_addon(combination,base);});
            const auto selection=root/"generic-selection.bin";
            write_selection(selection,combination);
            std::string arguments;
            for(const auto &value:{combination.renderer,base.string(),save_key(combination)}) arguments+=value+'\0';
            for(const auto &path:combination.addons) arguments+=path.string()+'\0';
            assert(bytes(selection)==arguments);
            write_profile(root/"generic.profile",combination);
            const auto restored=read_profile(root/"generic.profile");
            assert(restored.iwad==base && restored.addons==combination.addons);
        }
        auto profile = initial_profile(found,"gl4es");
        assert(profile.iwad==fs::canonical(doom));
        assert(save_key(profile)=="base");
        toggle_addon(profile,standalone);
        select_base(profile,standalone);
        assert(profile.iwad==standalone && profile.addons.empty());
        validate(profile); // PWAD-header standalone bases must not be blocked.
        rejects([&]{toggle_addon(profile,standalone);});
        assert(save_key(profile)!="base");
        profile=initial_profile(found,"gl4es");
        toggle_addon(profile,pk3);
        toggle_addon(profile,patch);
        assert(profile.addons==std::vector<fs::path>({pk3,patch}));
        const auto original_key = save_key(profile);
        move_addon(profile,1,-1);
        assert(profile.addons==std::vector<fs::path>({patch,pk3}));
        assert(save_key(profile)!=original_key);
        move_addon(profile,0,-1);
        assert(profile.addons.front()==patch);
        profile.name="My \"mod\" \\ collection";
        profile.id=new_profile_id();
        profile.renderer="software";
        const auto saved=root/"profiles"/"test.profile";
        write_profile(saved,profile);
        auto loaded=read_profile(saved);
        assert(loaded.id==profile.id && loaded.name==profile.name && loaded.iwad==profile.iwad);
        assert(loaded.addons==profile.addons && loaded.renderer=="software");
        const auto selected=root/"selection.bin";
        write_selection(selected,loaded);
        std::string expected;
        for(const auto &value:{loaded.renderer,loaded.iwad.string(),save_key(loaded),patch.string(),pk3.string()}) expected+=value+'\0';
        assert(bytes(selected)==expected); // Exact boundaries; no shell parsing of file names.
        auto broken=loaded;
        broken.addons.push_back(patch);
        rejects([&]{write_selection(selected,broken);});
        assert(bytes(selected)==expected); // Failed launch leaves previous atomic output intact.
        fs::remove(patch);
        rejects([&]{validate(loaded);});
        loaded=read_profile(saved); // Missing files stay editable, but cannot launch.
        assert(loaded.addons.size()==2);
        toggle_addon(loaded,patch);
        assert(loaded.addons==std::vector<fs::path>({pk3}));
        file(root/"bad.profile","ZANDRONUM_LAUNCH_PROFILE 1\nname \"unfinished");
        rejects([&]{read_profile(root/"bad.profile");});
        file(root/"bad.profile",bytes(saved)+"renderer \"gl4es\"\n");
        rejects([&]{read_profile(root/"bad.profile");});
        auto traversal=bytes(saved);
        const auto id_position=traversal.find(profile.id);
        traversal.replace(id_position,profile.id.size(),"../../outside");
        file(root/"bad.profile",traversal);
        rejects([&]{read_profile(root/"bad.profile");});
        file(root/"bad.profile","ZANDRONUM_LAUNCH_PROFILE 99\n");
        rejects([&]{read_profile(root/"bad.profile");});
        assert(!fs::exists(root/"nope"));
        fs::remove_all(root);
        std::cout<<"PASS: generic WAD/PK3 base/add-on roles, IWAD/PWAD headers, discovery, dedup, ordered selection, profile round-trip, atomic handoff, missing/duplicate content and malformed profiles\n";
    } catch (...) {
        fs::remove_all(root);
        throw;
    }
}
