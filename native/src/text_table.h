// text_table.h — the game's string table, native side.
//
// The original's local_text.cpp loads language<N>.utf and fetches
// strings by index. This mirrors that exactly for the English table
// (language1): text(STR) returns the same string the PS3 build shows.
// Indices below are the ones the menus use; names follow the content.
// The full table lives in language1.inc (generated from the extracted
// analysis/language/language1.txt by scripts/gen_text_table.py).

#pragma once

namespace comet::text {

// String indices in language1 (English).
enum Str {
    Training = 0,
    Campaign = 1,
    Battle = 2,
    Settings = 3,
    OnlineScores = 4,
    Credits = 5,
    FullGame = 6,
    Resume = 7,
    HideMenu = 8,
    ShowHint = 9,
    Paused = 10,
    RestartGame = 11,
    RestartGameQ = 12,
    EndGame = 13,
    EndGameQ = 14,
    Yes = 15,
    No = 16,
    NotConnected = 17,
    Area = 23,
    NextArea = 24,
    PlayAgain = 25,
    ChangeMap = 26,
    MainMenu = 27,
    BuyGame = 28,
    Continue = 29,
    NumberOfPlayers = 30,
    Difficulty = 31,
    Beginner = 32,
    Casual = 33,
    Ranked = 34,
    MaxHits = 35,
    Controls = 36,
    VideoRecord = 37,
    MusicVolume = 38,
    EffectsVolume = 39,
    DisplayZoom = 40,
    DisplayZoomHelp = 41,
    Enable = 45,
    Disable = 46,
    Accept = 48,
    Cancel = 49,
    Ok = 51,
    Team = 52,
    TeamKreios = 53,
    TeamHathor = 54,
    TeamAether = 55,
    TeamHelios = 56,
    Ready = 58,
    Player = 59,
    CancelGameQ = 60,
    PressStartToJoin = 61,
    TrainingWelcome = 101,
    MissionBriefingFirst = 150, // 150..171: the 22 campaign briefings
    MissionBriefingLast = 171,
    SinglePlayer = 187,
    CreditsBy = 192,
    CreditsMusic = 193,
    CreditsLong = 194,
    MoveSelect = 198,
    PauseName = 200,
    Language = 209,
    English = 210,
    Expert = 212,
    Resurgence = 213,
    Gaia = 221,
    Kronos = 222,
    OnlineStore = 243,
    Options = 244,
    Manual = 245,
    PressToStart = 252,
    ChooseColor = 253,
    StartGame = 256,
};

// The string for an index, or "" when out of range.
const char* text(int index);

} // namespace comet::text
