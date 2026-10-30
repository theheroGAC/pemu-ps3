#include <algorithm>
#include "pgen_romlist.h"

using namespace c2d;
using namespace pemu;

static std::string getFileExt(const std::string &path) {
    size_t pos = path.find_last_of('.');
    if (pos == std::string::npos || pos + 1 >= path.size()) {
        return "";
    }
    return Utility::toLower(path.substr(pos + 1));
}

ss_api::System PGENRomList::getFileSystem(const std::string &path, const ss_api::System &fallback) {
    std::string ext = getFileExt(path);
    if (ext == "sms") {
        return {2, 0, "Master System"};
    }
    if (ext == "gg") {
        return {21, 0, "Game Gear"};
    }
    if (ext == "sg") {
        return {109, 0, "SG-1000"};
    }
    if (ext == "cue" || ext == "chd" || ext == "iso") {
        return {20, 0, "Mega-CD"};
    }
    return fallback;
}

void PGENRomList::build(const ss_api::GameList::GameAddedCb &cb) {
    RomList::build([this, &cb](Game *game) {
        ss_api::System system = getFileSystem(game->path, game->system);
        if (system.id != game->system.id) {
            game->system = system;
            if (!gameList->systemList.find(system.id)) {
                gameList->systemList.systems.emplace_back(system);
            }
        }
        if (cb) {
            cb(game);
        }
    });

    std::sort(gameList->systemList.systems.begin(), gameList->systemList.systems.end(),
              ss_api::Api::sortSystemByName);

    auto opt = ui->getConfig()->get(PEMUConfig::OptId::UI_FILTER_SYSTEM);
    if (opt != nullptr) {
        opt->setArray(gameList->systemList.getNames());
    }

    ui->getConfig()->load();
}
