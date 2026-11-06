// menu_model.cpp — see menu_model.h.

#include "menu_model.h"

#include "text_table.h"

#include <algorithm>

namespace comet {
using namespace input;
namespace {

int axisStep(float cur, float prev) {
    if (cur > 0.55f && prev <= 0.55f) return 1;
    if (cur < -0.55f && prev >= -0.55f) return -1;
    return 0;
}

MenuItem actionItem(int labelStr, int action) {
    MenuItem it;
    it.labelStr = labelStr;
    it.action = action;
    return it;
}

} // namespace

MenuModel::MenuModel() {
    Screen title;
    title.id = ScreenId::Title;
    title.titleLiteral = "COMET CRASH";
    stack_.push_back(title);
}

FloatRect MenuModel::itemRect(float viewW, float viewH, int index) {
    const float w = viewW * 0.46f;
    const float h = viewH * 0.068f;
    return {viewW * 0.5f - w * 0.5f, viewH * 0.30f + index * (h + viewH * 0.012f), w, h};
}

FloatRect MenuModel::panelRect(float viewW, float viewH) {
    const float w = viewW * 0.56f;
    const float h = viewH * 0.52f;
    return {viewW * 0.5f - w * 0.5f, viewH * 0.24f, w, h};
}

bool MenuModel::arenaBehind() const {
    for (const Screen& s : stack_) {
        if (s.id == ScreenId::Arena) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Screen builders
// ---------------------------------------------------------------------------

Screen MenuModel::buildMainMenu() {
    Screen s;
    s.id = ScreenId::MainMenu;
    s.titleLiteral = "COMET CRASH";
    s.items = {actionItem(text::Training, 1), actionItem(text::Campaign, 2),
               actionItem(text::Battle, 3),    actionItem(text::Settings, 4),
               actionItem(text::OnlineScores, 5), actionItem(text::Credits, 6)};
    return s;
}

Screen MenuModel::buildCampaign() {
    Screen s;
    s.id = ScreenId::Campaign;
    s.titleStr = text::Campaign;
    const int missions = text::MissionBriefingLast - text::MissionBriefingFirst + 1;
    for (int i = 0; i < missions; i++) {
        MenuItem it = actionItem(text::Area, i);
        it.suffixNumber = i + 1;
        s.items.push_back(it);
    }
    return s;
}

Screen MenuModel::buildMissionBrief(int mission) {
    Screen s;
    s.id = ScreenId::MissionBrief;
    s.titleStr = text::Campaign;
    s.bodyStr = text::MissionBriefingFirst + mission;
    s.items = {actionItem(text::StartGame, 1)};
    return s;
}

Screen MenuModel::buildBattleSetup() {
    Screen s;
    s.id = ScreenId::BattleSetup;
    s.titleStr = text::Battle;

    MenuItem players;
    players.labelStr = text::NumberOfPlayers;
    players.kind = MenuItem::Kind::Cycle;
    players.cycleLiterals = {"1", "2", "3", "4"};
    players.value = battlePlayers_ - 1;
    players.action = 1;
    s.items.push_back(players);

    MenuItem difficulty;
    difficulty.labelStr = text::Difficulty;
    difficulty.kind = MenuItem::Kind::Cycle;
    difficulty.cycleStrs = {text::Beginner, text::Casual, text::Expert};
    difficulty.value = battleDifficulty_;
    difficulty.action = 2;
    s.items.push_back(difficulty);

    MenuItem maxHits;
    maxHits.labelStr = text::MaxHits;
    maxHits.kind = MenuItem::Kind::Slider;
    maxHits.minValue = 1;
    maxHits.maxValue = 20;
    maxHits.value = battleMaxHits_;
    maxHits.action = 3;
    s.items.push_back(maxHits);

    MenuItem map;
    map.labelStr = text::ChangeMap;
    map.kind = MenuItem::Kind::Cycle;
    map.cycleStrs = {text::Gaia, text::Kronos};
    map.value = battleMap_;
    map.action = 4;
    s.items.push_back(map);

    s.items.push_back(actionItem(text::StartGame, 99));

    static const int kTeams[4] = {text::TeamKreios, text::TeamHathor,
                                  text::TeamAether, text::TeamHelios};
    for (int p = 0; p < battlePlayers_; p++) {
        MenuItem info;
        info.kind = MenuItem::Kind::Info;
        info.label = std::string(text::text(text::Player)) + " " +
                     std::to_string(p + 1) + " - " +
                     text::text(text::Team) + " " + text::text(kTeams[p]) +
                     " - " + text::text(text::PressStartToJoin);
        s.items.push_back(info);
    }
    return s;
}

Screen MenuModel::buildSettings() {
    Screen s;
    s.id = ScreenId::Settings;
    s.titleStr = text::Settings;

    MenuItem music;
    music.labelStr = text::MusicVolume;
    music.kind = MenuItem::Kind::Slider;
    music.minValue = 0;
    music.maxValue = 10;
    music.value = musicVol_;
    music.action = 1;
    s.items.push_back(music);

    MenuItem effects;
    effects.labelStr = text::EffectsVolume;
    effects.kind = MenuItem::Kind::Slider;
    effects.minValue = 0;
    effects.maxValue = 10;
    effects.value = effectsVol_;
    effects.action = 2;
    s.items.push_back(effects);

    MenuItem record;
    record.labelStr = text::VideoRecord;
    record.kind = MenuItem::Kind::Toggle;
    record.value = videoRecord_;
    record.action = 3;
    s.items.push_back(record);

    MenuItem zoom;
    zoom.labelStr = text::DisplayZoom;
    zoom.kind = MenuItem::Kind::Slider;
    zoom.minValue = 0;
    zoom.maxValue = 10;
    zoom.value = displayZoom_;
    zoom.helpStr = text::DisplayZoomHelp;
    zoom.action = 4;
    s.items.push_back(zoom);

    MenuItem language;
    language.labelStr = text::Language;
    language.kind = MenuItem::Kind::Cycle;
    language.cycleStrs = {text::English};
    language.value = 0;
    language.action = 5;
    s.items.push_back(language);

    s.items.push_back(actionItem(text::Controls, 6));
    return s;
}

Screen MenuModel::buildControls() {
    Screen s;
    s.id = ScreenId::Controls;
    s.titleStr = text::Controls;
    std::string body;
    for (int i = 196; i <= 208; i++) {
        body += text::text(i);
        body += "\n";
    }
    s.bodyLiteral = body;
    return s;
}

Screen MenuModel::buildScores() {
    Screen s;
    s.id = ScreenId::Scores;
    s.titleStr = text::OnlineScores;
    s.bodyStr = text::NotConnected;
    return s;
}

Screen MenuModel::buildCredits() {
    Screen s;
    s.id = ScreenId::Credits;
    s.titleStr = text::Credits;
    s.bodyLiteral = std::string(text::text(text::CreditsBy)) + "\n" +
                    text::text(text::CreditsMusic) + "\n\n" +
                    text::text(text::CreditsLong);
    return s;
}

Screen MenuModel::buildArena(int titleStr, int bodyStr) {
    Screen s;
    s.id = ScreenId::Arena;
    s.titleStr = titleStr;
    s.bodyStr = bodyStr;
    return s;
}

Screen MenuModel::buildPause() {
    Screen s;
    s.id = ScreenId::Pause;
    s.titleStr = text::Paused;
    s.items = {actionItem(text::Resume, 1), actionItem(text::RestartGame, 2),
               actionItem(text::HideMenu, 3), actionItem(text::EndGame, 4)};
    return s;
}

Screen MenuModel::buildConfirm(int promptStr, int confirmKind) {
    Screen s;
    s.id = ScreenId::Confirm;
    s.titleStr = promptStr;
    s.confirmKind = confirmKind;
    s.items = {actionItem(text::Yes, 1), actionItem(text::No, 2)};
    return s;
}

void MenuModel::rebuildTop() {
    Screen& top = stack_.back();
    const int selected = top.selected;
    if (top.id == ScreenId::BattleSetup) {
        top = buildBattleSetup();
    } else if (top.id == ScreenId::Settings) {
        top = buildSettings();
    }
    top.selected = std::min(selected, (int)top.items.size() - 1);
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

void MenuModel::enterArena(int titleStr, int bodyStr, MenuAction action, int param) {
    arenaTitleStr_ = titleStr;
    arenaBodyStr_ = bodyStr;
    arenaUiHidden_ = false;
    pendingAction_ = action;
    actionParam_ = param;
    stack_.clear();
    stack_.push_back(buildArena(titleStr, bodyStr));
}

void MenuModel::moveSelection(int delta) {
    Screen& s = stack_.back();
    const int n = (int)s.items.size();
    if (n == 0) return;
    int idx = s.selected;
    for (int step = 0; step < n; step++) {
        idx += delta;
        if (idx < 0 || idx >= n) return;  // clamp at the ends, no wrap
        if (s.items[idx].kind != MenuItem::Kind::Info) {
            s.selected = idx;
            return;
        }
    }
}

void MenuModel::adjustSelected(int dir) {
    Screen& s = stack_.back();
    if (s.items.empty()) return;
    MenuItem& it = s.items[s.selected];
    switch (it.kind) {
        case MenuItem::Kind::Slider:
            it.value = std::clamp(it.value + dir, it.minValue, it.maxValue);
            break;
        case MenuItem::Kind::Toggle:
            it.value = it.value ? 0 : 1;
            break;
        case MenuItem::Kind::Cycle: {
            const int count = (int)(it.cycleStrs.size() + it.cycleLiterals.size());
            if (count > 0) it.value = (it.value + dir + count) % count;
            break;
        }
        default:
            return;
    }
    // Persist editable state the builders read back.
    if (s.id == ScreenId::BattleSetup) {
        if (it.action == 1) battlePlayers_ = it.value + 1;
        if (it.action == 2) battleDifficulty_ = it.value;
        if (it.action == 3) battleMaxHits_ = it.value;
        if (it.action == 4) battleMap_ = it.value;
        if (it.action == 1) rebuildTop();
    } else if (s.id == ScreenId::Settings) {
        if (it.action == 1) musicVol_ = it.value;
        if (it.action == 2) effectsVol_ = it.value;
        if (it.action == 3) videoRecord_ = it.value;
        if (it.action == 4) displayZoom_ = it.value;
    }
}

void MenuModel::activateSelected() {
    Screen& s = stack_.back();
    if (s.items.empty()) return;
    const MenuItem& it = s.items[s.selected];
    switch (s.id) {
        case ScreenId::MainMenu:
            switch (it.action) {
                case 1: enterArena(text::Training, text::TrainingWelcome,
                                   MenuAction::StartTraining, 0); break;
                case 2: stack_.push_back(buildCampaign()); break;
                case 3: stack_.push_back(buildBattleSetup()); break;
                case 4: stack_.push_back(buildSettings()); break;
                case 5: stack_.push_back(buildScores()); break;
                case 6: stack_.push_back(buildCredits()); break;
            }
            break;
        case ScreenId::Campaign:
            stack_.push_back(buildMissionBrief(it.action));
            break;
        case ScreenId::MissionBrief:
            enterArena(text::Campaign, s.bodyStr, MenuAction::StartMission,
                       s.bodyStr - text::MissionBriefingFirst);
            break;
        case ScreenId::BattleSetup:
            if (it.action == 99) {
                enterArena(text::Battle, -1, MenuAction::StartBattle, 0);
            }
            break;
        case ScreenId::Settings:
            if (it.action == 6) stack_.push_back(buildControls());
            break;
        case ScreenId::Pause:
            switch (it.action) {
                case 1: stack_.pop_back(); break;  // Resume
                case 2: stack_.push_back(buildConfirm(text::RestartGameQ, 1)); break;
                case 3:  // Hide Menu
                    stack_.pop_back();
                    arenaUiHidden_ = true;
                    break;
                case 4: stack_.push_back(buildConfirm(text::EndGameQ, 2)); break;
            }
            break;
        case ScreenId::Confirm:
            if (it.action == 1) {  // Yes
                if (s.confirmKind == 1) {
                    pendingAction_ = MenuAction::RestartRun;
                    stack_.pop_back();  // Confirm
                    stack_.pop_back();  // Pause
                } else if (s.confirmKind == 2) {
                    pendingAction_ = MenuAction::ExitToMainMenu;
                    stack_.clear();
                    stack_.push_back(buildMainMenu());
                }
            } else {
                stack_.pop_back();  // No
            }
            break;
        default:
            break;
    }
}

void MenuModel::goBack() {
    if (stack_.size() > 1) {
        stack_.pop_back();
    } else if (stack_.back().id == ScreenId::MainMenu) {
        stack_.clear();
        Screen title;
        title.id = ScreenId::Title;
        title.titleLiteral = "COMET CRASH";
        stack_.push_back(title);
    }
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

void MenuModel::update(const InputFrame& frame, float viewW, float viewH) {
    const int stepY = axisStep(frame.moveY, prevMoveY_);
    const int stepX = axisStep(frame.moveX, prevMoveX_);
    prevMoveX_ = frame.moveX;
    prevMoveY_ = frame.moveY;

    Screen& s = stack_.back();

    if (s.id == ScreenId::Title) {
        if ((frame.pressed & (BtnConfirm | BtnStart)) != 0) {
            stack_.clear();
            stack_.push_back(buildMainMenu());
        }
        return;
    }

    if (s.id == ScreenId::Arena) {
        if (arenaUiHidden_) {
            if (frame.pressed != 0) arenaUiHidden_ = false;
            return;
        }
        if ((frame.pressed & BtnStart) != 0) {
            stack_.push_back(buildPause());
        }
        return;
    }

    if (!s.items.empty()) {
        // Mouse hover selects (the original is a cursor-driven game).
        if (frame.mouseActive) {
            for (int i = 0; i < (int)s.items.size(); i++) {
                if (s.items[i].kind == MenuItem::Kind::Info) continue;
                if (itemRect(viewW, viewH, i).contains(frame.cursorX, frame.cursorY)) {
                    s.selected = i;
                }
            }
        }
        if (stepY != 0) moveSelection(stepY);
        if (stepX != 0) adjustSelected(stepX);
        if ((frame.pressed & BtnConfirm) != 0) {
            activateSelected();
            return;
        }
        if ((frame.pressed & BtnBack) != 0) {
            goBack();
            return;
        }
    } else {
        // Text screens: the move vector scrolls, Back leaves.
        if (stepY != 0) {
            s.scroll = std::max(0, s.scroll + stepY);
        }
        if ((frame.pressed & BtnBack) != 0) {
            goBack();
            return;
        }
    }
}

} // namespace comet
