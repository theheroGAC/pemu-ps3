//
// Created by cpasjuste on 29/05/18.
//

#include "skeleton/pemu.h"
#include "pgen_config.h"

using namespace c2d;
using namespace pemu;

PGENConfig::PGENConfig(Renderer *renderer, const int version) : PEMUConfig(renderer, "PGEN", version) {
    c2d::Io *io = renderer->getIo();

    // no need for auto-scaling mode
    getOption(EMU_SCALING_MODE)->setArray({"ASPECT", "INTEGER"}, 0);

    // "romlist.cpp" (RomList::build) will also reload config, but we need new roms paths
    PEMUConfig::load();

    // add custom rom path
    addRomPath("MEGADRIVE", io->getDataPath() + "megadrive/", {1, 0, "Megadrive"});
    addRomPath("MASTERSYSTEM", io->getDataPath() + "mastersystem/", {2, 0, "Master System"});
    addRomPath("GAMEGEAR", io->getDataPath() + "gamegear/", {21, 0, "Game Gear"});
    addRomPath("MEGACD", io->getDataPath() + "megacd/", {20, 0, "Mega-CD"});
    addRomPath("SG1000", io->getDataPath() + "sg1000/", {109, 0, "SG-1000"});
#ifdef __PS3__
    addRomPath("HDD0_ROMS", "/dev_hdd0/roms/", {1, 0, "Megadrive"});
    addRomPath("HDD0_MD", "/dev_hdd0/roms/megadrive/", {1, 0, "Megadrive"});
    for (int i = 0; i < 8; i++) {
        char name[32], path[64];
        snprintf(name, sizeof(name), "USB%d_ROMS", i);
        snprintf(path, sizeof(path), "/dev_usb%03d/roms/", i);
        addRomPath(name, path, {1, 0, "Megadrive"});
        snprintf(name, sizeof(name), "USB%d_MD", i);
        snprintf(path, sizeof(path), "/dev_usb%03d/roms/megadrive/", i);
        addRomPath(name, path, {1, 0, "Megadrive"});
    }
#endif
    PEMUConfig::save();

    const auto paths = getRomPaths();
    for (const auto &path: paths) {
        if (path.path.rfind(io->getDataPath(), 0) == 0) {
            io->create(path.path);
        }
    }
}
