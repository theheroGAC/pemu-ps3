//
// Created by cpasjuste on 01/06/18.
//

#include "burner.h"
#include "burnint.h"

#include "skeleton/pemu.h"
#include "pfbneo_ui_emu.h"
#include "pfbneo_ui_video.h"
#include "pfbneo_utility.h"
#include "pemu_snd_probe.h"
#include "retro_input_wrapper.h"

using namespace c2d;
using namespace pemu;

int nVidFullscreen = 0;
INT32 bVidUseHardwareGamma = 1;

UINT32 (__cdecl *VidHighCol)(INT32 r, INT32 g, INT32 b, INT32 i);

INT32 VidRecalcPal() { return BurnRecalcPal(); }

#ifdef __PS3__
extern void boot_log(const char *msg);

extern "C" {
unsigned long long pemu_z80_calls = 0;
unsigned long long pemu_z80_cycles = 0;
unsigned long long pemu_m6809_calls = 0;
unsigned long long pemu_m6809_cycles = 0;
}

struct PemuSndProbeReg {
    const char *tag;
    unsigned long long calls;
    unsigned long long lastPeak;
    unsigned long long next;
    unsigned long long interval;
};

static PemuSndProbeReg pemu_snd_regs[24];
static int pemu_snd_count = 0;

static void pemu_snd_log(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    boot_log(buf);
}

static unsigned long long pemu_peak16(const INT16 *data, long count) {
    unsigned long long peak = 0;
    if (data) {
        for (long i = 0; i < count; i++) {
            long v = data[i] < 0 ? -(long) data[i] : (long) data[i];
            if ((unsigned long long) v > peak) peak = (unsigned long long) v;
        }
    }
    return peak;
}

static PemuSndProbeReg *pemu_snd_reg(const char *tag) {
    for (int i = 0; i < pemu_snd_count; i++) {
        if (pemu_snd_regs[i].tag == tag) {
            return &pemu_snd_regs[i];
        }
    }
    if (pemu_snd_count < 24) {
        PemuSndProbeReg *reg = &pemu_snd_regs[pemu_snd_count++];
        reg->tag = tag;
        reg->calls = 0;
        reg->lastPeak = 0;
        reg->next = 600;
        reg->interval = 600;
        return reg;
    }
    return nullptr;
}

void pemu_snd_probe(const char *tag, const void *bufp, long len) {
    PemuSndProbeReg *reg = pemu_snd_reg(tag);
    if (!reg) {
        return;
    }
    reg->calls++;
    unsigned long long peak = pemu_peak16((const INT16 *) bufp, len * 2);
    if (bufp) {
        reg->lastPeak = peak;
    }
    if (reg->calls >= reg->next) {
        pemu_snd_log("SNDPROBE %s: calls=%llu peak=%llu len=%ld buf=%p",
                     tag, reg->calls, reg->lastPeak, len, bufp);
        reg->interval *= 2;
        if (reg->interval > 60000) {
            reg->interval = 60000;
        }
        reg->next = reg->calls + reg->interval;
    }
}

void pemu_frame_probe(long frame, const void *bufp, long len) {
    static unsigned long long z80_last_calls = 0, z80_last_cycles = 0;
    static unsigned long long m6809_last_calls = 0, m6809_last_cycles = 0;
    static long next = 1;

    if (frame == 0) {
        z80_last_calls = pemu_z80_calls;
        z80_last_cycles = pemu_z80_cycles;
        m6809_last_calls = pemu_m6809_calls;
        m6809_last_cycles = pemu_m6809_cycles;
        next = 1;
    }

    if (frame < next) {
        return;
    }
    next = frame + 300;

    unsigned long long peak = pemu_peak16((const INT16 *) bufp, len * 2);
    unsigned long long z80_dc = pemu_z80_calls - z80_last_calls;
    unsigned long long z80_dcz = pemu_z80_cycles - z80_last_cycles;
    unsigned long long m6809_dc = pemu_m6809_calls - m6809_last_calls;
    unsigned long long m6809_dcz = pemu_m6809_cycles - m6809_last_cycles;
    z80_last_calls = pemu_z80_calls;
    z80_last_cycles = pemu_z80_cycles;
    m6809_last_calls = pemu_m6809_calls;
    m6809_last_cycles = pemu_m6809_cycles;

    pemu_snd_log("FRAMEPROBE f=%ld rate=%d len=%d peak=%llu buf=%p "
                 "z80(+%llu c:%llu tri) m6809(+%llu c:%llu tri)",
                 frame, (int) nBurnSoundRate, (int) nBurnSoundLen, peak, bufp,
                 z80_dc, z80_dcz, m6809_dc, m6809_dcz);
}
#else

void pemu_snd_probe(const char *tag, const void *bufp, long len) {
    (void) tag; (void) bufp; (void) len;
}

void pemu_frame_probe(long frame, const void *bufp, long len) {
    (void) frame; (void) bufp; (void) len;
}

#endif // __PS3__

#ifdef __PFBA_ARM__
extern int nSekCpuCore;

static bool isHardware(int hardware, int type) {
    return (((hardware | HARDWARE_PREFIX_CARTRIDGE) ^ HARDWARE_PREFIX_CARTRIDGE)
            & 0xff000000) == (unsigned int) type;
}

#endif

static UINT32 myHighCol16(int r, int g, int b, int /* i */) {
    UINT32 t;
    t = (r << 8) & 0xf800;
    t |= (g << 3) & 0x07e0;
    t |= (b >> 3) & 0x001f;
    return t;
}

static UiMain *uiInstance;

PFBAUiEmu::PFBAUiEmu(UiMain *ui) : UiEmu(ui) {
    printf("PFBAUiEmu()\n");
    uiInstance = ui;
}

#ifdef __PFBA_ARM__

int PFBAUiEmu::getSekCpuCore() {
    int sekCpuCore = 0; // SEK_CORE_C68K: USE CYCLONE ARM ASM M68K CORE

    std::vector<std::string> zipList;
    int hardware = BurnDrvGetHardwareCode();

    std::string bios = pMain->getConfig()->get(PEMUConfig::OptId::EMU_NEOBIOS, true)->getString();
    if (isHardware(hardware, HARDWARE_PREFIX_SNK) && Utility::contains(bios, "UNIBIOS")) {
        sekCpuCore = 1; // SEK_CORE_M68K: USE C M68K CORE
        pMain->getUiMessageBox()->show(
                "WARNING", "UNIBIOS DOESNT SUPPORT THE M68K ASM CORE\n"
                           "CYCLONE ASM CORE DISABLED", "OK");
    }

    if (isHardware(hardware, HARDWARE_PREFIX_SEGA_MEGADRIVE)) {
        sekCpuCore = 1; // SEK_CORE_M68K: USE C M68K CORE
    } else if (isHardware(hardware, HARDWARE_PREFIX_SEGA)) {
        if (hardware & HARDWARE_SEGA_FD1089A_ENC
            || hardware & HARDWARE_SEGA_FD1089B_ENC
            || hardware & HARDWARE_SEGA_MC8123_ENC
            || hardware & HARDWARE_SEGA_FD1094_ENC
            || hardware & HARDWARE_SEGA_FD1094_ENC_CPU2) {
            sekCpuCore = 1; // SEK_CORE_M68K: USE C M68K CORE
            pMain->getUiMessageBox()->show(
                    "WARNING", "ROM IS CRYPTED, USE DECRYPTED ROM (CLONE)\n"
                               "TO ENABLE CYCLONE ASM CORE (FASTER)", "OK");
        }
    } else if (isHardware(hardware, HARDWARE_PREFIX_TOAPLAN)) {
        zipList.emplace_back("batrider");
        zipList.emplace_back("bbakraid");
        zipList.emplace_back("bgaregga");
    } else if (isHardware(hardware, HARDWARE_PREFIX_SNK)) {
        zipList.emplace_back("kof97");
        zipList.emplace_back("kof98");
        zipList.emplace_back("kof99");
        zipList.emplace_back("kof2000");
        zipList.emplace_back("kof2001");
        zipList.emplace_back("kof2002");
        zipList.emplace_back("kf2k3pcb");
        //zipList.push_back("kof2003"); // WORKS
    }

    std::string zip = BurnDrvGetTextA(DRV_NAME);
    for (unsigned int i = 0; i < zipList.size(); i++) {
        if (zipList[i].compare(0, zip.length(), zip) == 0) {
            pMain->getUiStatusBox()->show("THIS GAME DOES NOT SUPPORT THE M68K ASM CORE\n"
                                       "CYCLONE ASM CORE DISABLED");
            sekCpuCore = 1; // SEK_CORE_M68K: USE C M68K CORE
            break;
        }
    }

    zipList.clear();

    return sekCpuCore;
}

#endif

int PFBAUiEmu::load(const ss_api::Game &game) {
    currentGame = game;
    audioPeak = 0;
    audioFrames = 0;

    PFBNEOUtility::setDriverActive(game);
    if (nBurnDrvActive >= nBurnDrvCount) {
        printf("PFBAUiEmu::load: driver not found\n");
        pMain->getUiProgressBox()->setVisibility(Visibility::Hidden);
        pMain->getUiMessageBox()->show("ERROR", "THIS GAME IS NOT SUPPORTED BY FBNEO...", "OK");
        return -1;
    }

#ifdef __PFBA_ARM__
    nSekCpuCore = getSekCpuCore();
    printf("nSekCpuCore: %s\n", nSekCpuCore > 0 ? "M68K" : "C68K (ASM)");
#endif

    int audio_freq = pMain->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_FREQ, true)->getInteger();
#ifdef __PS3__
    audio_freq = 48000;
#endif
    nInterpolation = pMain->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_INTERPOLATION, true)->getInteger();
    nFMInterpolation = pMain->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_FMINTERPOLATION, true)->getInteger();
    bForce60Hz = pMain->getConfig()->get(PEMUConfig::OptId::EMU_FORCE_60HZ, true)->getInteger();
    if (bForce60Hz) {
        nBurnFPS = 6000;
    }

#ifdef __PS3__
    {
        char lbuf[512];
        const char *lname = BurnDrvGetTextA(DRV_FULLNAME);
        snprintf(lbuf, sizeof(lbuf),
                 "PFBA driver: name=%s hw=%08x flags=%08x roms=%s full=%.96s",
                 BurnDrvGetTextA(DRV_NAME) ? BurnDrvGetTextA(DRV_NAME) : "?",
                 (unsigned) BurnDrvGetHardwareCode(), (unsigned) BurnDrvGetFlags(),
                 game.path.c_str(), lname ? lname : "?");
        boot_log(lbuf);
    }
#endif

    ///////////////
    // FBA DRIVER
    ///////////////
    EnableHiscores = 1;

    printf("PFBAUiEmu::load: initialize driver...\n");
    nBurnSoundRate = audio_freq;
    nBurnSoundLen = audio_freq / 60;
    pBurnSoundOut = (INT16 *) malloc((audio_freq / 10) * 2 * (int) sizeof(INT16));
    if (DrvInit((int) nBurnDrvActive, false) != 0) {
        printf("\nPFBAUiEmu::load: driver initialisation failed\n");
        free(pBurnSoundOut);
        pBurnSoundOut = nullptr;
        pMain->getUiProgressBox()->setVisibility(Visibility::Hidden);
        pMain->getUiMessageBox()->show("ERROR", "DRIVER INIT FAILED", "OK");
        stop();
        return -1;
    }
    free(pBurnSoundOut);
    pBurnSoundOut = nullptr;
    nFramesEmulated = 0;
    nFramesRendered = 0;
    nCurrentFrame = 0;
    ///////////////
    // FBA DRIVER
    ///////////////

    ///////////
    // AUDIO
    //////////
    addAudio(audio_freq, Audio::toSamples(audio_freq, (float) nBurnFPS / 100.0f));
    char bbuf[256];
    snprintf(bbuf, sizeof(bbuf), "PFBA load: freq=%d, fps=%d, audio avail=%d", audio_freq, (int) nBurnFPS, audio ? (int) audio->isAvailable() : 0);
    boot_log(bbuf);
    if (audio->isAvailable()) {
        nBurnSoundRate = audio->getSampleRate();
        nBurnSoundLen = audio->getSamples();
        pBurnSoundOut = (INT16 *) malloc(audio->getSamplesSize());
        snprintf(bbuf, sizeof(bbuf), "PFBA load: rate=%d, len=%d, buf=%p", nBurnSoundRate, nBurnSoundLen, pBurnSoundOut);
        boot_log(bbuf);
    }
    audio_sync = !bForce60Hz;
#ifdef __PS3__
    audio_sync = true;
#endif
    targetFps = (float) nBurnFPS / 100.0f;
    printf("PFBAUiEmu::load: FORCE_60HZ: %i, AUDIO_SYNC: %i, FPS: %f\n", bForce60Hz, audio_sync, targetFps);
    printf("PFBAUiEmu::load: audio available: %i, rate: %i, samples: %i, buffer: %s\n",
           audio->isAvailable(), nBurnSoundRate, nBurnSoundLen, pBurnSoundOut ? "ok" : "null");
    ///////////
    // AUDIO
    //////////

    //////////
    // VIDEO
    //////////
    Vector2i size, aspect;
    BurnDrvGetFullSize(&size.x, &size.y);
    BurnDrvGetAspect(&aspect.x, &aspect.y);
    nBurnBpp = 2;
    BurnHighCol = myHighCol16;
    BurnRecalcPal();
    // video may already be initialized from fbneo driver (Reinitialise)
    if (!video) {
        auto v = new PFBAVideo(pMain, &pBurnDraw, &nBurnPitch, size, aspect);
        addVideo(v);
        printf("PFBAUiEmu::load: size: %i x %i, aspect: %i x %i, pitch: %i\n",
               size.x, size.y, aspect.x, aspect.y, nBurnPitch);
    } else {
        printf("PFBAUiEmu::load: video already initialized, skipped\n");
    }
    //////////
    // VIDEO
    //////////

    return UiEmu::load(game);
}

// need for some games
void Reinitialise(void) {
    Vector2i size, aspect;
    BurnDrvGetFullSize(&size.x, &size.y);
    BurnDrvGetAspect(&aspect.x, &aspect.y);
    auto v = new PFBAVideo(uiInstance, &pBurnDraw, &nBurnPitch, size, aspect);
    uiInstance->getUiEmu()->addVideo(v);
    printf("PFBAUiEmu::Reinitialise: size: %i x %i, aspect: %i x %i\n",
           size.x, size.y, aspect.x, aspect.y);
}

void PFBAUiEmu::stop() {
    DrvExit();
    if (pBurnSoundOut) {
        free(pBurnSoundOut);
        pBurnSoundOut = nullptr;
    }
    UiEmu::stop();
}

bool PFBAUiEmu::onInput(c2d::Input::Player *players) {
    if (pMain->getUiMenu()->isVisible() || pMain->getUiStateMenu()->isVisible()) {
        pMain->getInput()->setRotation(Input::Rotation::R0, Input::Rotation::R0);
        return UiEmu::onInput(players);
    }

    // rotation config:
    // 0 > "OFF"
    // 1 > "ON"
    // 2 > "FLIP"
    // 3 > "CAB" (vita/switch)
    int rotation = getUi()->getConfig()->get(PEMUConfig::OptId::EMU_ROTATION, true)->getArrayIndex();
    if (BurnDrvGetFlags() & BDF_ORIENTATION_VERTICAL) {
        if (rotation == 0) {
            pMain->getInput()->setRotation(Input::Rotation::R90, Input::Rotation::R0);
        } else if (rotation == 1) {
            pMain->getInput()->setRotation(Input::Rotation::R0, Input::Rotation::R0);
        } else if (rotation == 2) {
            pMain->getInput()->setRotation(Input::Rotation::R270, Input::Rotation::R0);
        } else {
            pMain->getInput()->setRotation(Input::Rotation::R270, Input::Rotation::R270);
        }
    }

    return UiEmu::onInput(players);
}

void PFBAUiEmu::onUpdate() {
    if (isPaused()) {
        return;
    }

    // update fbneo inputs
    InputMake(true);

    // handle diagnostic and reset switch
    unsigned int buttons = pMain->getInput()->getButtons();
    if (buttons & Input::Button::Select) {
        if (clock.getElapsedTime().asSeconds() > 2) {
            if (pgi_reset) {
                pMain->getUiStatusBox()->show("TIPS: PRESS START "
                                              "BUTTON 2 SECONDS FOR DIAG MENU...");
                pgi_reset->Input.nVal = 1;
                *(pgi_reset->Input.pVal) = pgi_reset->Input.nVal;
            }
            nCurrentFrame = 0;
            nFramesEmulated = 0;
            clock.restart();
        }
    } else if (buttons & Input::Button::Start) {
        if (clock.getElapsedTime().asSeconds() > 2) {
            if (pgi_diag) {
                pMain->getUiStatusBox()->show("TIPS: PRESS COIN "
                                              "BUTTON 2 SECONDS TO RESET CURRENT GAME...");
                pgi_diag->Input.nVal = 1;
                *(pgi_diag->Input.pVal) = pgi_diag->Input.nVal;
            }
            clock.restart();
        }
    } else {
        clock.restart();
    }

    // update fbneo video buffer and audio
#ifdef __VITA__
    int skip = pMain->getConfig()->get(PEMUConfig::OptId::EMU_FRAMESKIP, true)->getInteger();
#else
    int skip = 0;
#endif

    pBurnDraw = nullptr;
    frameskip++;

    if (frameskip > skip) {
        video->getTexture()->lock(&pBurnDraw, &nBurnPitch);
        nFramesRendered++;
    }

    BurnDrvFrame();
    nCurrentFrame++;

    if (frameskip > skip) {
        video->getTexture()->unlock();
        frameskip = 0;
    }

    if (audio) {
        if (audioFrames < 60 && pBurnSoundOut) {
            const INT16 *data = pBurnSoundOut;
            int count = audio->getSamples() * 2;
            for (int i = 0; i < count; i++) {
                int v = data[i] < 0 ? -(int) data[i] : (int) data[i];
                if (v > audioPeak) {
                    audioPeak = v;
                }
            }
            if (++audioFrames == 60) {
                printf("PFBAUiEmu: audio peak (60 frames): %i\n", audioPeak);
                char pbuf[128];
                snprintf(pbuf, sizeof(pbuf), "PFBA audio peak (60 frames): %d", audioPeak);
                boot_log(pbuf);
            }
        }
        static int emuFrameLog = 0;
        if (emuFrameLog < 5) {
            char fbuf[128];
            snprintf(fbuf, sizeof(fbuf), "PFBA frame %d: peak=%d, buf=%p, queued=%d",
                     emuFrameLog++, audioPeak, pBurnSoundOut, audio->getSampleBufferQueued());
            boot_log(fbuf);
        }
#if 0
        int queued = audio->getSampleBufferQueued();
        int capacity = audio->getSampleBufferCapacity();
        if (audio->getSamples() + queued > capacity) {
            printf("WARNING: samples: %i, queued: %i, capacity: %i (fps: %f)\n",
                   audio->getSamples(), queued, capacity, targetFps);
        }
#endif
        audio->play(pBurnSoundOut, audio->getSamples(),
                    audio_sync ? Audio::SyncMode::Safe : Audio::SyncMode::None);
    }

    UiEmu::onUpdate();
}
