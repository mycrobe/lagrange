/* canvasmenu_impl_SDL.m — AppKit native-menu backend for the SDL2/canvas host.

   A clean, menu-only AppKit implementation of the portable canvasmenu.h
   contract. It builds the NSApplication main menu from iMenuItem arrays,
   dispatches selections via a lightweight target that posts commands into the
   app, and handles enable/disable by command/index/key plus window-menu and
   localization.

   Unlike macos.m (which stays the legacy direct-SDL app backend), this file is
   deliberately menu-only and must NOT: swap NSApplication's delegate, install
   ScrollWheel/KeyDown local event monitors (they'd eat the scroll events and
   regress the sdlview wheel path), or touch real SDL window internals
   (nsWindow_, SDL_GetWindowWMInfo). Only the keycode/modmask constants and the
   monotonic tick come from SDL (via the stub headers in a canvas build). */

#import <AppKit/AppKit.h>
#import <SDL_keyboard.h>
#import <SDL_timer.h>

#include "canvasmenu.h"
#include "app.h"
#include "lang.h"
#include "command.h"
#include "keys.h"
#include "root.h"
#include "widget.h"
#include "window.h"
#include "render/text.h"
#include <the_Foundation/stringset.h>
#include <the_Foundation/regexp.h>

/*----------------------------------------------------------------------------------------------*/

@interface MenuCommands : NSObject {
    NSMutableDictionary<NSString *, NSString *> *commands;
    iWidget *source;
}
@end

@implementation MenuCommands

- (id)init {
    self = [super init];
    commands = [[NSMutableDictionary<NSString *, NSString *> alloc] init];
    source = NULL;
    return self;
}

- (void)setCommand:(NSString * __nonnull)command forMenuItem:(NSMenuItem * __nonnull)menuItem {
    [commands setObject:command forKey:[menuItem title]];
}

- (void)setSource:(iWidget *)widget {
    source = widget;
}

- (void)clear {
    [commands removeAllObjects];
}

- (NSString *)commandForMenuItem:(NSMenuItem *)menuItem {
    return [commands objectForKey:[menuItem title]];
}

- (void)postMenuItemCommand:(id)sender {
    NSString *command = [commands objectForKey:[(NSMenuItem *)sender title]];
    if (command) {
        const char *cstr = [command cStringUsingEncoding:NSUTF8StringEncoding];
        if (source) {
            postCommand_Widget(source, "%s", cstr);
        }
        else {
            postCommand_Root(NULL, cstr);
        }
        /* SDL ignores menu key equivalents and still posts the keydown, so drop the
           immediately following keydown to avoid double-activating shortcuts. */
        iForEach(PtrArray, w, collect_PtrArray(listWindows_App())) {
            as_Window(w.ptr)->focusGainedAt = SDL_GetTicks();
        }
    }
}

@end

/*----------------------------------------------------------------------------------------------*/

static MenuCommands *g_menuCommands_; /* lazily created; independent of the app delegate */

static NSMenuItem *makeMenuItems_(NSMenu *menu, MenuCommands *commands, int atIndex,
                                  iBool isBookmarksMenu, const iMenuItem *items, size_t n);

static MenuCommands *menuCommands_(void) {
    if (!g_menuCommands_) {
        g_menuCommands_ = [[MenuCommands alloc] init];
    }
    return g_menuCommands_;
}

static void ensureAppActivation_(void) {
    /* Make sure the app is a regular foreground app so the menu bar actually
       shows. This does not touch NSApp.delegate (SDL still owns it). */
    static BOOL activated;
    if (!activated) {
        activated = YES;
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app activateIgnoringOtherApps:YES];
    }
}

static void appearanceChanged_(NSString *name) {
    const iBool isDark = [name containsString:@"Dark"];
    const iBool isHighContrast = [name containsString:@"HighContrast"];
    postCommandf_App("~os.theme.changed dark:%d contrast:%d", isDark ? 1 : 0, isHighContrast ? 1 : 0);
}

static NSString *currentSystemAppearance_(void) {
    if ([NSApp respondsToSelector:@selector(effectiveAppearance)]) {
        return [[NSApp effectiveAppearance] name];
    }
    return @"NSAppearanceNameAqua";
}

/*----------------------------------------------------------------------------------------------*/

void insertMenuItems_MacOS(const char *menuLabel, int atIndex, int firstItemIndex,
                           const iMenuItem *items, size_t count) {
    ensureAppActivation_();
    NSApplication *app  = [NSApplication sharedApplication];
    NSMenu        *appMenu = [app mainMenu];
    if (!appMenu) return;
    menuLabel = translateCStr_Lang(menuLabel);
    NSMenuItem *mainItem;
    NSMenu     *menu;
    if (firstItemIndex == 0) {
        mainItem = [appMenu insertItemWithTitle:[NSString stringWithUTF8String:menuLabel]
                                         action:nil
                                  keyEquivalent:@""
                                        atIndex:atIndex];
        menu = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:menuLabel]];
        [mainItem setSubmenu:menu];
    }
    else {
        mainItem = [appMenu itemAtIndex:atIndex];
        menu = mainItem.submenu;
    }
    if (!menu) return;
    [menu setAutoenablesItems:NO];
    makeMenuItems_(menu, menuCommands_(), firstItemIndex, atIndex == 4, items, count);
    if (firstItemIndex == 0) {
        [menu release];
    }
}

void updateMenuItems_MacOS(int atIndex, const iMenuItem *items, size_t count) {
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *menu = [[app mainMenu] itemAtIndex:atIndex].submenu;
    if (!menu) return;
    [menu removeAllItems];
    makeMenuItems_(menu, menuCommands_(), 0, atIndex == 4, items, count);
}

void removeMenu_MacOS(int atIndex) {
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *appMenu = [app mainMenu];
    if (!appMenu || atIndex >= [appMenu numberOfItems]) return;
    [appMenu removeItemAtIndex:atIndex];
}

void removeMenuItems_MacOS(int atIndex, int firstItem, int numItems) {
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *menu = [[app mainMenu] itemAtIndex:atIndex].submenu;
    if (!menu) return;
    for (int i = 0; i < numItems; i++) {
        [menu removeItemAtIndex:firstItem];
    }
}

void enableMenu_MacOS(const char *menuLabel, iBool enable) {
    menuLabel = translateCStr_Lang(menuLabel);
    NSApplication *app  = [NSApplication sharedApplication];
    NSMenu        *appMenu = [app mainMenu];
    NSString      *label = [NSString stringWithUTF8String:menuLabel];
    NSMenuItem    *menuItem = [appMenu itemAtIndex:[appMenu indexOfItemWithTitle:label]];
    [menuItem setEnabled:enable];
}

void enableMenuIndex_MacOS(int index, iBool enable) {
    NSApplication *app  = [NSApplication sharedApplication];
    NSMenu        *appMenu = [app mainMenu];
    NSMenuItem    *menuItem = [appMenu itemAtIndex:index];
    [menuItem setEnabled:enable];
}

void enableMenuItem_MacOS(const char *menuItemCommand, iBool enable) {
    NSApplication *app     = [NSApplication sharedApplication];
    NSMenu        *appMenu = [app mainMenu];
    MenuCommands  *cmds    = menuCommands_();
    for (NSMenuItem *mainMenuItem in appMenu.itemArray) {
        NSMenu *menu = mainMenuItem.submenu;
        if (menu) {
            for (NSMenuItem *menuItem in menu.itemArray) {
                NSString *command = [cmds commandForMenuItem:menuItem];
                if (command &&
                    !iCmpStr([command cStringUsingEncoding:NSUTF8StringEncoding], menuItemCommand)) {
                    [menuItem setEnabled:enable];
                    return;
                }
            }
        }
    }
}

static iString *composeKeyEquivalent_(int key, int kmods, NSEventModifierFlags *modMask) {
    iString *str = new_String();
    if (key == SDLK_LEFT) {
        appendChar_String(str, 0x2190);
    }
    else if (key == SDLK_RIGHT) {
        appendChar_String(str, 0x2192);
    }
    else if (key == SDLK_UP) {
        appendChar_String(str, 0x2191);
    }
    else if (key == SDLK_DOWN) {
        appendChar_String(str, 0x2193);
    }
    else if (key) {
        appendChar_String(str, key);
    }
    *modMask = 0;
    if (kmods & KMOD_GUI)       *modMask |= NSEventModifierFlagCommand;
    if (kmods & KMOD_ALT)       *modMask |= NSEventModifierFlagOption;
    if (kmods & KMOD_CTRL)      *modMask |= NSEventModifierFlagControl;
    if (kmods & KMOD_SHIFT)     *modMask |= NSEventModifierFlagShift;
    return str;
}

void enableMenuItemsByKey_MacOS(int key, int kmods, iBool enable) {
    NSApplication          *app     = [NSApplication sharedApplication];
    NSMenu                 *appMenu = [app mainMenu];
    NSEventModifierFlags   modMask;
    iString                *keyEquiv = composeKeyEquivalent_(key, kmods, &modMask);
    for (NSMenuItem *mainMenuItem in appMenu.itemArray) {
        NSMenu *menu = mainMenuItem.submenu;
        if (menu) {
            for (NSMenuItem *menuItem in menu.itemArray) {
                if (menuItem.keyEquivalentModifierMask == modMask &&
                    !iCmpStr([menuItem.keyEquivalent cStringUsingEncoding:NSUTF8StringEncoding],
                             cstr_String(keyEquiv))) {
                    [menuItem setEnabled:enable];
                }
            }
        }
    }
    delete_String(keyEquiv);
}

void enableMenuItemsOnHomeRow_MacOS(iBool enable) {
    iStringSet *homeRowKeys = new_StringSet();
    const char *keys[] = {
        "f", "d", "s", "a",
        "j", "k", "l",
        "r", "e", "w", "q",
        "u", "i", "o", "p",
        "v", "c", "x", "z",
        "m", "n",
        "g", "h",
        "b",
        "t", "y"
    };
    iForIndices(i, keys) {
        iString str;
        initCStr_String(&str, keys[i]);
        insert_StringSet(homeRowKeys, &str);
        deinit_String(&str);
    }
    NSApplication *app     = [NSApplication sharedApplication];
    NSMenu        *appMenu = [app mainMenu];
    for (NSMenuItem *mainMenuItem in appMenu.itemArray) {
        NSMenu *menu = mainMenuItem.submenu;
        if (menu) {
            for (NSMenuItem *menuItem in menu.itemArray) {
                if (menuItem.keyEquivalentModifierMask == 0) {
                    iString equiv;
                    initCStr_String(&equiv, [menuItem.keyEquivalent
                                                cStringUsingEncoding:NSUTF8StringEncoding]);
                    if (contains_StringSet(homeRowKeys, &equiv)) {
                        [menuItem setEnabled:enable];
                        [menu setAutoenablesItems:NO];
                    }
                    deinit_String(&equiv);
                }
            }
        }
    }
    iRelease(homeRowKeys);
}

static void setShortcut_NSMenuItem_(NSMenuItem *item, int key, int kmods) {
    NSEventModifierFlags modMask;
    iString *str = composeKeyEquivalent_(key, kmods, &modMask);
    [item setKeyEquivalentModifierMask:modMask];
    [item setKeyEquivalent:[NSString stringWithUTF8String:cstr_String(str)]];
    delete_String(str);
}

static NSString *cleanString_(const iString *ansiEscapedText) {
    iString mod;
    initCopy_String(&mod, ansiEscapedText);
    iRegExp *ansi = makeAnsiEscapePattern_Text(iTrue /* with ESC */);
    replaceRegExp_String(&mod, ansi, "", NULL, NULL);
    iRelease(ansi);
    NSString *clean = [NSString stringWithUTF8String:cstr_String(&mod)];
    deinit_String(&mod);
    return clean;
}

/* returns the selected (checked) item, if any */
static NSMenuItem *makeMenuItems_(NSMenu *menu, MenuCommands *commands, int atIndex,
                                  const iBool isBookmarksMenu,
                                  const iMenuItem *items, size_t n) {
    if (atIndex == 0) {
        atIndex = menu.numberOfItems;
    }
    atIndex = iMin(atIndex, menu.numberOfItems);
    NSMenuItem *selectedItem = nil;
    for (size_t i = 0; i < n && items[i].label; ++i) {
        if (!checkDevice_MenuItem(&items[i])) {
            continue;
        }
        const char *label = translateCStr_Lang(items[i].label);
        if (equal_CStr(label, "---")) {
            [menu insertItem:[NSMenuItem separatorItem] atIndex:atIndex++];
        }
        else {
            const iBool hasCommand = (items[i].command && items[i].command[0]);
            iBool isChecked = iFalse;
            iBool isDisabled = iFalse;
            if (startsWith_CStr(label, "###")) {
                isChecked = iTrue;
                label += 3;
            }
            else if (startsWith_CStr(label, "///") || startsWith_CStr(label, "```")) {
                isDisabled = iTrue;
                label += 3;
            }
            iString itemTitle;
            initCStr_String(&itemTitle, label);
            removeIconPrefix_String(&itemTitle);   /* strip any leading icon glyph */
            removeColorEscapes_String(&itemTitle); /* strip embedded color escapes */
            NSMenuItem *item = [[NSMenuItem alloc] init];
            NSAttributedString *title = [[NSAttributedString alloc] initWithString:cleanString_(&itemTitle)];
            item.attributedTitle = title;
            [title release];
            if (hasCommand && startsWith_CStr(items[i].command, "submenu id:")) {
                NSMenu *sub = [[NSMenu alloc] init];
                sub.autoenablesItems = YES;
                const char *submenuId = cstr_String(string_Command(items[i].command, "id"));
                iWidget *subwidget = findChild_Widget(submenuRoot_MacOS()->widget, submenuId);
                if (!subwidget) {
                    subwidget = findWidget_Root(submenuId);
                }
                if (subwidget) {
                    const iArray *items = userData_Object(subwidget);
                    iAssert(items);
                    makeMenuItems_(sub, commands, 0, isBookmarksMenu, constData_Array(items),
                                   size_Array(items));
                    [item setSubmenu:sub];
                }
                else {
                    [sub release];
                    [item release];
                    continue;
                }
                [sub release];
            }
            else {
                item.action = (hasCommand ? @selector(postMenuItemCommand:) : nil);
            }
            [menu insertItem:item atIndex:atIndex++];
            deinit_String(&itemTitle);
            [item setTarget:commands];
            if (isChecked) {
#if defined (__MAC_10_13)
                [item setState:NSControlStateValueOn];
#else
                [item setState:NSOnState];
#endif
                selectedItem = item;
            }
            [item setEnabled:!isDisabled];
            int key = items[i].key;
            int kmods = items[i].kmods;
            if (hasCommand) {
                [commands setCommand:[NSString stringWithUTF8String:items[i].command]
                         forMenuItem:item];
                /* Bindings may have a different key. -1 disables the shortcut. */
                const iBinding *bind = findCommand_Keys(items[i].command);
                if (bind && bind->id < builtIn_BindingId) {
                    key = bind->key;
                    kmods = bind->mods;
                }
            }
            if (items[i].key >= 0) {
                setShortcut_NSMenuItem_(item, key, kmods);
            }
            [item release];
        }
    }
    return selectedItem;
}

void handleCommand_MacOS(const char *cmd) {
    if (equal_Command(cmd, "prefs.ostheme.changed")) {
        if (arg_Command(cmd)) {
            appearanceChanged_(currentSystemAppearance_());
        }
    }
    else if (equal_Command(cmd, "bindings.changed")) {
        NSApplication *app     = [NSApplication sharedApplication];
        NSMenu        *appMenu = [app mainMenu];
        MenuCommands  *cmds    = menuCommands_();
        int mainIndex = 0;
        for (NSMenuItem *mainMenuItem in appMenu.itemArray) {
            NSMenu *menu = mainMenuItem.submenu;
            if (menu) {
                int itemIndex = 0;
                for (NSMenuItem *menuItem in menu.itemArray) {
                    NSString *command = [cmds commandForMenuItem:menuItem];
                    if (!command && mainIndex == 6 && itemIndex == 0) {
                        /* Window > Close */
                        command = @"tabs.close";
                    }
                    if (command) {
                        const iBinding *bind = findCommand_Keys(
                            [command cStringUsingEncoding:NSUTF8StringEncoding]);
                        if (bind && bind->id < builtIn_BindingId) {
                            setShortcut_NSMenuItem_(menuItem, bind->key, bind->mods);
                        }
                    }
                    itemIndex++;
                }
            }
            mainIndex++;
        }
    }
    else if (equal_Command(cmd, "lang.changed")) {
        localizeApplicationMenu_MacOS();
    }
    else if (equal_Command(cmd, "emojipicker")) {
        [NSApp orderFrontCharacterPalette:nil];
    }
}

void localizeApplicationMenu_MacOS(void) {
    NSMenu *appMenu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
    if (!appMenu) return;
    const int n = [appMenu numberOfItems];
    [[appMenu itemAtIndex:0]
        setTitle:[NSString stringWithUTF8String:cstr_Lang("menu.aboutapp")]];
    [[appMenu itemAtIndex:2]
        setTitle:[NSString stringWithUTF8String:cstr_Lang("menu.preferences")]];
    [[appMenu itemAtIndex:n - 7]
        setTitle:[NSString stringWithUTF8String:cstr_Lang("macos.menu.services")]];
    [[appMenu itemAtIndex:n - 5]
        setTitle:[NSString stringWithUTF8String:cstr_Lang("macos.menu.hide")]];
    [[appMenu itemAtIndex:n - 4]
        setTitle:[NSString stringWithUTF8String:cstr_Lang("macos.menu.hideothers")]];
    [[appMenu itemAtIndex:n - 3]
        setTitle:[NSString stringWithUTF8String:cstr_Lang("macos.menu.showall")]];
    [[appMenu itemAtIndex:n - 1]
        setTitle:[NSString stringWithUTF8String:cstr_Lang("menu.quit")]];
}

void showPopupMenu_MacOS(iWidget *source, iInt2 windowCoord, const iMenuItem *items, size_t n) {
    ensureAppActivation_();
    NSMenu *menu = [[NSMenu alloc] init];
    MenuCommands *commands = menuCommands_();
    iWindow *window = activeWindow_App();
    NSWindow *nsWindow = [NSApp keyWindow];
    if (!nsWindow) nsWindow = [NSApp mainWindow];
    /* View coordinates are flipped. */
    iBool isCentered = iFalse;
    if (isEqual_I2(windowCoord, zero_I2())) {
        windowCoord = divi_I2(window->size, 2);
        isCentered = iTrue;
    }
    NSPoint screenPoint;
    if (nsWindow) {
        windowCoord.y = window->size.y - windowCoord.y;
        windowCoord = divf_I2(windowCoord, window->pixelRatio);
        screenPoint = [nsWindow convertRectToScreen:(CGRect){ { windowCoord.x, windowCoord.y }, {0, 0} }].origin;
    }
    else {
        screenPoint = NSMakePoint([NSScreen mainScreen].frame.size.width / 2,
                                  [NSScreen mainScreen].frame.size.height / 2);
    }
    NSMenuItem *selectedItem = makeMenuItems_(menu, commands, 0, iFalse, items, n);
    [commands setSource:source];
    if (isCentered) {
        NSSize menuSize = [menu size];
        screenPoint.x -= menuSize.width / 2;
        screenPoint.y += menuSize.height / 2;
    }
    [menu setAutoenablesItems:NO];
    [menu popUpMenuPositioningItem:selectedItem atLocation:screenPoint inView:nil];
    [commands setSource:NULL];
    [menu release];
}
