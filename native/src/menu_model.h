// menu_model.h — the game's menu flow as a pure, SDL-free state machine.
//
// Screens and labels come from the extracted language table (see
// text_table.h): the main menu is Training / Campaign / Battle /
// Settings / Online Scores / Credits in the original's order, the
// pause menu is Resume / Restart Game / Hide Menu / End Game with the
// original's Yes/No confirmations, settings carry Music Volume /
// Effects Volume / Video Record / Display Zoom / Language / Controls,
// and battle setup carries Number of Players / Difficulty / Max Hits /
// Change Map / Start Game. The campaign list is the 22 mission
// briefings (strings 150-171) as Areas 1-22.
//
// Navigation matches the original's pad-cursor model through the
// unified InputFrame: the move vector (d-pad, stick, WASD/arrows)
// steps the selection, the mouse cursor can hover-select, Confirm
// activates, Back pops a screen, Start opens the pause menu in game.

#pragma once

#include "input_types.h"

#include <string>
#include <vector>

namespace comet {

enum class ScreenId {
    Title,
    MainMenu,
    Campaign,
    MissionBrief,
    BattleSetup,
    Settings,
    Controls,
    Scores,
    Credits,
    Arena,
    Pause,
    Confirm,
};

enum class MenuAction {
    None,
    StartTraining,
    StartMission,
    StartBattle,
    RestartRun,
    ExitToMainMenu,
};

struct FloatRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct MenuItem {
    enum class Kind { Action, Slider, Toggle, Cycle, Info };

    int labelStr = -1;              // string-table index for the label
    std::string label;              // literal label (Info rows, composed)
    int suffixNumber = -1;          // e.g. "Area 3" when >= 0
    Kind kind = Kind::Action;
    int value = 0;                  // Slider/Toggle/Cycle state
    int minValue = 0;
    int maxValue = 0;
    std::vector<int> cycleStrs;         // Cycle options as string indices
    std::vector<std::string> cycleLiterals; // Cycle options as literals
    int helpStr = -1;               // help text shown while selected
    int action = 0;                 // per-screen semantic id for Actions
};

struct Screen {
    ScreenId id = ScreenId::Title;
    int titleStr = -1;
    std::string titleLiteral;
    std::vector<MenuItem> items;
    int selected = 0;
    int bodyStr = -1;               // long-text screens
    std::string bodyLiteral;
    int scroll = 0;
    int confirmKind = 0;            // Confirm screens: what Yes executes
};

class MenuModel {
public:
    MenuModel();

    // Advance the state machine by one frame of unified input.
    void update(const input::InputFrame& frame, float viewW, float viewH);

    const Screen& current() const { return stack_.back(); }
    bool arenaBehind() const;  // an Arena screen sits under the top one
    bool arenaUiHidden() const { return arenaUiHidden_; }
    int arenaTitleStr() const { return arenaTitleStr_; }
    int arenaBodyStr() const { return arenaBodyStr_; }

    // Semantic actions for the app shell (entering a run, restarting).
    MenuAction takeAction() {
        MenuAction a = pendingAction_;
        pendingAction_ = MenuAction::None;
        return a;
    }
    int actionParam() const { return actionParam_; }  // mission index

    // Shared layout: the renderer draws exactly these rects, the model
    // hit-tests the same ones for mouse hover.
    static FloatRect itemRect(float viewW, float viewH, int index);
    static FloatRect panelRect(float viewW, float viewH);

private:
    Screen buildMainMenu();
    Screen buildCampaign();
    Screen buildMissionBrief(int mission);
    Screen buildBattleSetup();
    Screen buildSettings();
    Screen buildControls();
    Screen buildScores();
    Screen buildCredits();
    Screen buildArena(int titleStr, int bodyStr);
    Screen buildPause();
    Screen buildConfirm(int promptStr, int confirmKind);
    void rebuildTop();

    void activateSelected();
    void adjustSelected(int dir);
    void moveSelection(int delta);
    void goBack();
    void enterArena(int titleStr, int bodyStr, MenuAction action, int param);

    std::vector<Screen> stack_;
    MenuAction pendingAction_ = MenuAction::None;
    int actionParam_ = 0;
    int arenaTitleStr_ = -1;
    int arenaBodyStr_ = -1;
    bool arenaUiHidden_ = false;
    int battlePlayers_ = 2;
    int battleDifficulty_ = 1;
    int battleMaxHits_ = 10;
    int battleMap_ = 0;
    int musicVol_ = 8;
    int effectsVol_ = 8;
    int videoRecord_ = 1;
    int displayZoom_ = 5;
    float prevMoveX_ = 0.0f;
    float prevMoveY_ = 0.0f;
};

} // namespace comet
