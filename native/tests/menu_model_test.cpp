// menu_model_test.cpp — headless checks for the menu state machine.
//
// Drives MenuModel with synthetic InputFrames through the real menu
// flow: title -> main menu -> campaign -> mission brief -> arena ->
// pause -> end game confirm -> main menu, plus settings adjustment
// and battle player-count behavior.

#include <cstdio>
#include <cstring>

#include "../src/menu_model.h"
#include "../src/text_table.h"

using namespace comet;
using namespace comet::input;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

InputFrame neutral() { return InputFrame{}; }

InputFrame press(std::uint32_t btn) {
    InputFrame f;
    f.pressed = btn;
    f.buttons = btn;
    return f;
}

InputFrame move(float x, float y) {
    InputFrame f;
    f.moveX = x;
    f.moveY = y;
    return f;
}

void tap(MenuModel& m, std::uint32_t btn) {
    m.update(press(btn), 960, 540);
    m.update(neutral(), 960, 540);
}

void pushMove(MenuModel& m, float x, float y) {
    m.update(move(x, y), 960, 540);
    m.update(neutral(), 960, 540);
}

void down(MenuModel& m, int times) {
    for (int i = 0; i < times; i++) pushMove(m, 0, 1);
}

} // namespace

int main() {
    // --- Title -> MainMenu -------------------------------------------------
    MenuModel m;
    check(m.current().id == ScreenId::Title, "starts at Title");
    tap(m, BtnStart);
    check(m.current().id == ScreenId::MainMenu, "Start enters MainMenu");
    check(m.current().items.size() == 6, "main menu has 6 items");
    check(std::strcmp(text::text(m.current().items[0].labelStr), "Training") == 0,
          "first main menu item is Training");
    check(std::strcmp(text::text(m.current().items[5].labelStr), "Credits") == 0,
          "last main menu item is Credits");

    // --- Campaign -> mission brief -> arena --------------------------------
    down(m, 1);
    check(m.current().selected == 1, "selection moves to Campaign");
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::Campaign, "Campaign screen");
    check(m.current().items.size() == 22, "22 campaign areas");
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::MissionBrief, "mission brief screen");
    check(m.current().bodyStr == 150, "brief shows first briefing string");
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::Arena, "Start Game enters arena");
    check(m.takeAction() == MenuAction::StartMission, "action is StartMission");
    check(m.actionParam() == 0, "mission param 0");
    check(m.arenaBodyStr() == 150, "arena carries the briefing");

    // --- Pause -> End Game -> Yes -> MainMenu ------------------------------
    tap(m, BtnStart);
    check(m.current().id == ScreenId::Pause, "Start opens pause");
    check(m.current().items.size() == 4, "pause has 4 items");
    down(m, 3);
    check(m.current().selected == 3, "End Game selected");
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::Confirm, "End Game asks to confirm");
    tap(m, BtnConfirm);  // Yes
    check(m.current().id == ScreenId::MainMenu, "Yes returns to main menu");
    check(m.takeAction() == MenuAction::ExitToMainMenu, "action ExitToMainMenu");

    // --- Settings: slider adjust + persistence ------------------------------
    down(m, 3);
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::Settings, "Settings screen");
    check(m.current().items[0].value == 8, "music volume starts at 8");
    pushMove(m, 1, 0);
    check(m.current().items[0].value == 9, "right step raises music volume");
    pushMove(m, -1, 0);
    pushMove(m, -1, 0);
    check(m.current().items[0].value == 7, "left steps lower music volume");
    tap(m, BtnBack);
    check(m.current().id == ScreenId::MainMenu, "Back leaves settings");
    // Selection persists on the main menu (still on Settings): re-enter.
    tap(m, BtnConfirm);
    check(m.current().items[0].value == 7, "music volume persists across visits");
    tap(m, BtnBack);

    // --- Battle setup: player count drives team rows ------------------------
    pushMove(m, 0, -1);  // Settings -> Battle
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::BattleSetup, "Battle setup screen");
    int infoRows = 0;
    for (const MenuItem& it : m.current().items) {
        if (it.kind == MenuItem::Kind::Info) infoRows++;
    }
    check(infoRows == 2, "2 players -> 2 team rows");
    pushMove(m, 1, 0);  // players cycle: 2 -> 3
    infoRows = 0;
    for (const MenuItem& it : m.current().items) {
        if (it.kind == MenuItem::Kind::Info) infoRows++;
    }
    check(infoRows == 3, "3 players -> 3 team rows");
    tap(m, BtnBack);

    // --- Training -> pause -> Restart -> No -> resume ------------------------
    // After Back from BattleSetup the main menu still selects Battle (2).
    pushMove(m, 0, -1);
    pushMove(m, 0, -1);
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::Arena, "Training enters arena");
    check(m.takeAction() == MenuAction::StartTraining, "action StartTraining");
    tap(m, BtnStart);
    down(m, 1);  // Restart Game
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::Confirm, "Restart asks to confirm");
    down(m, 1);  // No
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::Pause, "No returns to pause");
    tap(m, BtnBack);
    check(m.current().id == ScreenId::Arena, "Back from pause resumes arena");

    // --- Credits scroll -------------------------------------------------------
    tap(m, BtnStart);
    down(m, 3);
    tap(m, BtnConfirm);  // End Game
    tap(m, BtnConfirm);  // Yes
    check(m.current().id == ScreenId::MainMenu, "back at main menu");
    (void)m.takeAction();
    down(m, 5);
    tap(m, BtnConfirm);
    check(m.current().id == ScreenId::Credits, "Credits screen");
    check(m.current().scroll == 0, "credits start unscrolled");
    pushMove(m, 0, 1);
    check(m.current().scroll == 1, "down scrolls credits");

    if (failures == 0) {
        std::printf("menu_model_test: all checks passed\n");
        return 0;
    }
    std::printf("menu_model_test: %d failures\n", failures);
    return 1;
}
