#pragma once
// Estado de edição dos ajustes de UM wallpaper, partilhado pelo menu da bandeja e pelo
// launcher: o manifest original, os ajustes do utilizador (um "tema" sem base) e o resultado.

#include <filesystem>
#include <string>

#include "core/Manifest.h"
#include "ui/AppControl.h"

namespace ap::ui {

struct WallpaperEditor {
    std::filesystem::path path;
    Manifest base;      // wallpaper + tema, sem ajustes
    Manifest effective; // com os ajustes
    Theme draft;        // os ajustes do utilizador
    std::string error;
    bool valid = false;

    void load(AppControl& app, const std::filesystem::path& p) {
        path = p;
        error.clear();
        valid = app.inspect(p, base, effective, error);
        draft = valid ? app.overrides(p) : Theme{};
    }

    // Recalcula o efetivo e envia para a app (ao vivo se for o ativo). persist = gravar.
    void apply(AppControl& app, bool persist) {
        effective = base;
        applyTheme(effective, draft);
        app.setOverrides(path, draft, persist);
    }

    void reset(AppControl& app) {
        draft = Theme{};
        apply(app, true);
    }

    float param(const std::string& name) const {
        const ParamSpec* p = effective.findParam(name);
        return p ? p->value : 0.0f;
    }
    void setParam(const std::string& name, float value) {
        for (auto& [n, v] : draft.params) {
            if (n == name) {
                v = value;
                return;
            }
        }
        draft.params.emplace_back(name, value);
    }
};

} // namespace ap::ui
