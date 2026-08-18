#include "frontend/menu/menu_model.h"

static_assert(!androidSaveStateActionReturnsToGame(true, true),
    "saving a state must keep the save-state menu open");
static_assert(!androidSaveStateActionReturnsToGame(true, false),
    "a failed save must keep the save-state menu open");
static_assert(!androidSaveStateActionReturnsToGame(false, false),
    "a failed load must keep the save-state menu open");
static_assert(androidSaveStateActionReturnsToGame(false, true),
    "a successful load must return to the running game");
static_assert(androidDirectionalSelectionRow(2, 4, 1, false) == 0,
    "the first down press must select the first row");
static_assert(androidDirectionalSelectionRow(0, 4, -1, false) == 3,
    "the first up press must select the last row");
static_assert(androidDirectionalSelectionRow(3, 4, 1, true) == 0,
    "down must wrap from the last row to the first row");
static_assert(androidDirectionalSelectionRow(0, 4, -1, true) == 3,
    "up must wrap from the first row to the last row");
static_assert(androidDirectionalSelectionRow(0, 0, 1, false) == -1,
    "an empty list must not produce a selection");
static_assert(!androidNavigationCanActivate(true, false, false),
    "confirmation must stay blocked while a direction is held");
static_assert(!androidNavigationCanActivate(false, true, false),
    "confirmation must stay blocked while dragging a list");
static_assert(!androidNavigationCanActivate(false, false, true),
    "confirmation must stay blocked while scrolling is moving");
static_assert(androidNavigationCanActivate(false, false, false),
    "confirmation must resume after navigation stops");

int main()
{
    return 0;
}
