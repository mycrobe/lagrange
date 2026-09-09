/* canvasmenu.h — portable native-menu contract (Phase 0/2 seam).

   Native menus are one of the seams that escape SDL (see docs/arcana.md →
   "Architecture — escaping SDL"). SDL2 has no native-menu API, so menus talk
   straight to the OS UI: AppKit NSMenu on mac/OS X, Menu Manager
   (InsertMenu/SetMenuItemText/SetMenuItemCmdKey/CheckItem) on Classic Mac OS.

   Every target implements these exact symbols (all Apple-family targets share
   the `_MacOS` name — the classic T/M split). A target supplies its own
   implementation; either:

     - canvasmenu_impl_SDL.m   (AppKit backend, SDL2/canvas host; clean,
                                no delegate swap, no event monitors, no SDL
                                window coupling)
     - canvasmenu_impl_toolbox.c (future, Classic Menu Manager)
     - macos.m                 (legacy direct-SDL app, unchanged)
     - canvasmenu.c            (default null backend, portable C no-ops)

   A single compile-time marker, LAGRANGE_NATIVE_MENU, gates all menu call
   sites so a build can opt into native menus without flipping every macOS
   platform behavior (iPlatformAppleDesktop also toggles fonts/DPI/layout).

   Note: submenuRoot_MacOS() is an app-level service (app.c provides it for the
   offscreen submenu root), so it is declared in app.h, not here. */

#pragma once

#include "util.h" /* iMenuItem, iBool, iInt2, iWidget */

iBool    hasNativeMenu_Platform         (void);

void     insertMenuItems_MacOS          (const char *menuLabel, int atIndex, int firstItemIndex,
                                         const iMenuItem *items, size_t count);
void     updateMenuItems_MacOS          (int atIndex, const iMenuItem *items, size_t count);
void     removeMenu_MacOS               (int atIndex);
void     removeMenuItems_MacOS          (int atIndex, int firstItem, int numItems);
void     enableMenu_MacOS               (const char *menuLabel, iBool enable);
void     enableMenuIndex_MacOS          (int index, iBool enable);
void     enableMenuItem_MacOS           (const char *menuItemCommand, iBool enable);
void     enableMenuItemsByKey_MacOS     (int key, int kmods, iBool enable);
void     enableMenuItemsOnHomeRow_MacOS (iBool enable);
void     handleCommand_MacOS            (const char *cmd);
void     localizeApplicationMenu_MacOS  (void);
void     showPopupMenu_MacOS            (iWidget *source, iInt2 windowCoord,
                                         const iMenuItem *items, size_t n);
