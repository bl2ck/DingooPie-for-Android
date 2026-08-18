#include "frontend/menu/menu_model.h"

static_assert(!androidSaveStateActionReturnsToGame(true, true),
    "saving a state must keep the save-state menu open");
static_assert(!androidSaveStateActionReturnsToGame(true, false),
    "a failed save must keep the save-state menu open");
static_assert(!androidSaveStateActionReturnsToGame(false, false),
    "a failed load must keep the save-state menu open");
static_assert(androidSaveStateActionReturnsToGame(false, true),
    "a successful load must return to the running game");

int main()
{
    return 0;
}
