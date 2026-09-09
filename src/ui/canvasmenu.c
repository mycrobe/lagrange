/* canvasmenu.c — default null backend for the portable native-menu contract.

   This is the portable no-op implementation used by builds that do not provide
   a native menu (headless hosts, non-desktop targets). It satisfies the
   canvasmenu.h contract with empty functions so that code gated on
   LAGRANGE_NATIVE_MENU can be linked without an OS menu backend. */

#include "canvasmenu.h"

iBool hasNativeMenu_Platform(void) {
    return iFalse;
}

void insertMenuItems_MacOS(const char *menuLabel, int atIndex, int firstItemIndex,
                           const iMenuItem *items, size_t count) {
    iUnused(menuLabel, atIndex, firstItemIndex, items, count);
}

void updateMenuItems_MacOS(int atIndex, const iMenuItem *items, size_t count) {
    iUnused(atIndex, items, count);
}

void removeMenu_MacOS(int atIndex) {
    iUnused(atIndex);
}

void removeMenuItems_MacOS(int atIndex, int firstItem, int numItems) {
    iUnused(atIndex, firstItem, numItems);
}

void enableMenu_MacOS(const char *menuLabel, iBool enable) {
    iUnused(menuLabel, enable);
}

void enableMenuIndex_MacOS(int index, iBool enable) {
    iUnused(index, enable);
}

void enableMenuItem_MacOS(const char *menuItemCommand, iBool enable) {
    iUnused(menuItemCommand, enable);
}

void enableMenuItemsByKey_MacOS(int key, int kmods, iBool enable) {
    iUnused(key, kmods, enable);
}

void enableMenuItemsOnHomeRow_MacOS(iBool enable) {
    iUnused(enable);
}

void handleCommand_MacOS(const char *cmd) {
    iUnused(cmd);
}

void localizeApplicationMenu_MacOS(void) {
}

void showPopupMenu_MacOS(iWidget *source, iInt2 windowCoord,
                         const iMenuItem *items, size_t n) {
    iUnused(source, windowCoord, items, n);
}
