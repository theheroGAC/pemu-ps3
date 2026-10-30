//
// Created by cpasjuste on 29/05/18.
//

#include "skeleton/pemu.h"
#include "pnes_config.h"

using namespace c2d;
using namespace pemu;

PNESConfig::PNESConfig(Renderer *renderer, const int version) : PEMUConfig(renderer, "PNES", version) {
    printf("PNESConfig(%s, v%i)\n", getPath().c_str(), version);

    c2d::Io *io = renderer->getIo();

    // no need for auto-scaling mode on pnes
    getOption(EMU_SCALING_MODE)->setArray({"ASPECT", "INTEGER"}, 0);

#ifdef __SWITCH__
    // on nintendo switch invert A/B buttons
    getOption(JOY_A)->setInteger(KEY_JOY_B_DEFAULT);
    getOption(JOY_B)->setInteger(KEY_JOY_A_DEFAULT);
#endif

    // "romlist.cpp" (RomList::build) will also reload config, but we need new roms paths
    PEMUConfig::load();

    addRomPath("NES", io->getDataPath() + "roms/", {3, 0, "NES"});
#ifdef __PS3__
    addRomPath("HDD0_ROMS", "/dev_hdd0/roms/", {3, 0, "NES"});
    addRomPath("HDD0_NES", "/dev_hdd0/roms/nes/", {3, 0, "NES"});
    for (int i = 0; i < 8; i++) {
        char name[32], path[64];
        snprintf(name, sizeof(name), "USB%d_ROMS", i);
        snprintf(path, sizeof(path), "/dev_usb%03d/roms/", i);
        addRomPath(name, path, {3, 0, "NES"});
        snprintf(name, sizeof(name), "USB%d_NES", i);
        snprintf(path, sizeof(path), "/dev_usb%03d/roms/nes/", i);
        addRomPath(name, path, {3, 0, "NES"});
    }
#endif
    PEMUConfig::save();

    // create roms paths if needed
    const auto paths = getRomPaths();
    for (const auto &path: paths) {
        io->create(path.path);
    }
}
