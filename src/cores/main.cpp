//
// Created by cpasjuste on 19/09/23.
//

#include "main.h"

#ifdef __PS3__
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/file.h>
#if defined(__PSNES__)
#define PS3_GAME_ID "PSNE00001"
#elif defined(__PNES__)
#define PS3_GAME_ID "PNES00001"
#elif defined(__PFBA__)
#define PS3_GAME_ID "PFBN00001"
#elif defined(__PGBA__)
#define PS3_GAME_ID "PGBA00001"
#else
#define PS3_GAME_ID "PGEN00001"
#endif
#define PS3_GAME_DIR "/dev_hdd0/game/" PS3_GAME_ID "/USRDIR"
static void boot_write_raw(const char *msg, bool truncate) {
    s32 fd = -1;
    const int flags = SYS_O_WRONLY | SYS_O_CREAT | (truncate ? SYS_O_TRUNC : SYS_O_APPEND);
    if (sysLv2FsOpen(PS3_GAME_DIR "/boot.log", flags, &fd, 0666, nullptr, 0) == 0) {
        u64 written = 0;
        sysLv2FsWrite(fd, msg, strlen(msg), &written);
        sysLv2FsClose(fd);
    }
}
__attribute__((constructor(1))) static void boot_log_ctor_first() {
    boot_write_raw(PS3_GAME_ID " ctor first\n", true);
}
void boot_log(const char *msg) {
    boot_write_raw(msg, false);
    boot_write_raw("\n", false);
}
__attribute__((constructor(101))) static void boot_log_static_begin() {
    boot_write_raw(PS3_GAME_ID " static init begin\n", false);
}
#else
void boot_log(const char *msg) { (void) msg; }
#endif

using namespace c2d;
using namespace pemu;

PEMUUiMain *pemu_ui;

int main(int argc, char **argv) {
#ifdef __PS3__
    boot_log(PS3_GAME_ID " main entered");
    errno = 0;
    const int chdir_rc = chdir(PS3_GAME_DIR);
    const int chdir_errno = errno;
    char cwd[512] = {0};
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(cwd, sizeof(cwd), "getcwd failed");
    }
    char chdir_msg[640];
    snprintf(chdir_msg, sizeof(chdir_msg), "chdir rc=%d errno=%d cwd=%s",
             chdir_rc, chdir_errno, cwd);
    boot_log(chdir_msg);
    boot_log(PS3_GAME_ID " boot start");
#endif
    // command line game info
    Game game;

    // custom io
    const auto io = new PEMUIo();
    boot_log("io created");
    boot_log(("io data path: " + io->getDataPath()).c_str());
    boot_log(("io romfs path: " + io->getRomFsPath()).c_str());

    // create main ui/renderer
    // NOTE: size must stay {0, 0} on non-switch platforms (PS4, PS5, Vita, Linux, Windows...).
    // libcross2d's SDL2Renderer only sets SDL_WINDOW_FULLSCREEN_DESKTOP when the requested
    // size is <= 0 (see SDL2Renderer::SDL2Renderer in libcross2d/source/platforms/sdl2/sdl2_renderer.cpp).
    // Passing a fixed size like {1280, 720} here creates a small, non-fullscreen SDL window,
    // which on PS4 (no windowing system / no desktop compositor) never actually gets
    // presented to the TV output -> black screen, even though audio keeps working fine
    // since it's an independent subsystem. Switch ignores this parameter entirely and
    // forces its own {1280, 720} internally (see UiMain's __SWITCH__ constructor), so it's
    // safe to leave this at {0, 0} for every platform.
#if defined(__LINUX__) || defined(__WINDOWS__) || defined(__SWITCH__) || defined(__PS3__)
    pemu_ui = new PEMUUiMain(Vector2f{1280, 720});
#else
    pemu_ui = new PEMUUiMain(Vector2f{0, 0});
#endif
    pemu_ui->setIo(io);
    boot_log("ui created");

    // load configuration
    constexpr int version = (__PEMU_VERSION_MAJOR__ * 100) + __PEMU_VERSION_MINOR__;
    boot_log("cfg pre");
    const auto cfg = new PEMUConfig(pemu_ui, version);
    boot_log("cfg ok");
    pemu_ui->setConfig(cfg);
    boot_log("setConfig ok");

    // load skin configuration
    boot_log("skin pre");
    const auto skin = new PEMUSkin(pemu_ui);
    boot_log("skin ok");
    pemu_ui->setSkin(skin);
    boot_log("setSkin ok");

    // parse command line
    if (argc > 1) {
        if (io->exist(argv[1])) {
            game.path = Utility::baseName(argv[1]);
            game.name = Utility::removeExt(game.path);
            game.romsPath = Utility::remove(argv[1], game.path);
        } else {
            printf("main: file provided as console argument does not exist (%s)\n", argv[1]);
            delete (skin);
            delete (cfg);
            delete (pemu_ui);
            return 1;
        }
    }

    // ui
    boot_log("romlist pre");
    const auto romList = new PEMURomList(pemu_ui, cfg->getCoreVersion(), cfg->getCoreSupportedExt());
    boot_log("romlist ok");
    if (game.path.empty()) {
        boot_log("romlist build pre");
        romList->build();
        boot_log("romlist build ok");
        romList->initFav();
        boot_log("romlist initFav ok");
    } else {
        delete (romList->rect);
    }
    const auto uiRomList = new PEMUUiRomList(pemu_ui, romList, pemu_ui->getSize());
    boot_log("uiRomList ok");
    const auto uiMenu = new PEMUUiMenu(pemu_ui);
    boot_log("uiMenu ok");
    const auto uiEmu = new PEMUUiEmu(pemu_ui);
    boot_log("uiEmu ok");
    const auto uiState = new PEMUUiMenuState(pemu_ui);
    boot_log("uiState ok");
    pemu_ui->init(uiRomList, uiMenu, uiEmu, uiState);
    boot_log("pemu init ok");

    // load specified game from command line if requested
    if (!game.path.empty()) {
        uiRomList->setVisibility(Visibility::Hidden);
        uiRomList->setGames({game});
        cfg->loadGame(game);
        uiEmu->setExitOnStop(true);
        uiEmu->load(game);
    }

    boot_log("enter main loop");
    int s_loop_count = 0;
    int s_flip_count = 0;
    while (!pemu_ui->done) {
        if (s_loop_count < 5) {
            char fbuf[32];
            snprintf(fbuf, sizeof(fbuf), "main loop frame %d", s_loop_count++);
            boot_log(fbuf);
        }
        pemu_ui->flip();
        if (s_flip_count < 5) {
            char fbuf[32];
            snprintf(fbuf, sizeof(fbuf), "main loop flip %d ok", s_flip_count++);
            boot_log(fbuf);
        }
    }

    delete (skin);
    delete (cfg);
    delete (pemu_ui);

#ifdef  __PS4__
    sceSystemServiceLoadExec((char *) "exit", nullptr);
    while (true) {
    }
#endif

    return 0;
}
