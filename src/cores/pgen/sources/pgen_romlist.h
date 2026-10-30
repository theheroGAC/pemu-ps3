#ifndef PGEN_ROMLIST_H
#define PGEN_ROMLIST_H

#include <string>
#include <vector>
#include "skeleton/pemu.h"

class PGENRomList : public RomList {
public:
    PGENRomList(UiMain *ui, const std::string &emuVersion, const std::vector<std::string> &filters)
            : RomList(ui, emuVersion, filters) {}

    void build(const ss_api::GameList::GameAddedCb &cb = nullptr) override;

private:
    static ss_api::System getFileSystem(const std::string &path, const ss_api::System &fallback);
};

#endif
