/* canvasmenu_impl_aqua.m — 10.4-era AppKit native-menu backend for the Aqua host.

   Implements the portable canvasmenu.h contract against AppKit for the Aqua
   (Tiger/Leopard PPC) canvas host, using only Mac OS X 10.4 APIs (no fast
   enumeration, no modern literals/generics, no properties/blocks — the Tiger
   ObjC runtime).  It builds the NSApplication main menu from iMenuItem arrays,
   posts selections into the app (postCommand_Widget / _Root), and handles
   enable/disable by command/index/key.

   The app menu (index 0) and Window menu (index 6) are NOT inserted by the
   widget kit (window.c inserts File/Edit/View/Bookmarks/Identity/Help at
   1..5,7), so this backend creates them; that is also what gives the Cmd-key
   equivalents (quit / preferences / close / minimize) somewhere to land.

   Menu-only and deliberately clean: it does NOT swap NSApplication's delegate
   (the Aqua host owns it) nor install local event monitors (they would eat the
   scroll/key events the shim relies on).

   Copyright 2026 the lagrange port.  Distributed under the BSD-2-Clause license
   of the core it drives (see LICENSE.md). */

#import <AppKit/AppKit.h>
#import <SDL_keyboard.h>
#import <SDL_timer.h>

#include "app.h"
#include "lang.h"
#include "ui/canvasmenu.h"
#include "ui/command.h"
#include "ui/root.h"
#include "ui/widget.h"
#include "ui/window.h"
#include "ui/util.h"

static void ensureAppMenu_(void);
static void ensureWindowMenu_(void);

/* Pre-SnowLeopard (Tiger/Leopard) AppKit does NOT automatically identify the
   first item of a programmatically-set main menu as the application menu: a
   nibless app gets AppKit's own synthesised bold app-name menu prepended, so the
   app menu the native backend builds at index 0 shows up as a *second*, plain
   menu beside it.  AppKit still implements the (long-unannounced)
   `setAppleMenu:` selector — it is what the nib loader calls to nominate the
   app menu — so declare it in a category and point it at our index-0 submenu.
   Snow Leopard (10.6) dropped the need for this (it identifies mainMenu[0]
   itself); it is a no-op / unnecessary there, and on the 10.6+ runtime the
   selector still responds, so a single guarded call is safe on both. */
@interface NSApplication (AquaAppleMenu)
- (void)setAppleMenu:(NSMenu *)menu;
@end

/* --------------------------------------------------------------- globals -- */

@interface AquaMenuItemTarget : NSObject {
    NSString *command_;
    iWidget  *source_;
}
- (id)initWithCommand:(const char *)command;
- (void)setSource:(iWidget *)source;
- (const char *)command;
- (void)post:(id)sender;
@end

@implementation AquaMenuItemTarget

- (id)initWithCommand:(const char *)command {
    self = [super init];
    command_ = [[NSString stringWithCString:command encoding:NSUTF8StringEncoding] retain];
    source_ = NULL;
    return self;
}

- (void)dealloc {
    [command_ release];
    [super dealloc];
}

- (void)setSource:(iWidget *)source {
    source_ = source;
}

- (const char *)command {
    return [command_ cStringUsingEncoding:NSUTF8StringEncoding];
}

- (void)post:(id)sender {
    (void) sender;
    const char *cmd = [command_ cStringUsingEncoding:NSUTF8StringEncoding];
    if (cmd && *cmd) {
        if (source_) {
            postCommand_Widget(source_, "%s", cmd);
        }
        else {
            postCommand_Root(NULL, cmd);
        }
        iForEach(PtrArray, w, collect_PtrArray(listWindows_App())) {
            as_Window(w.ptr)->focusGainedAt = SDL_GetTicks();
        }
    }
}

@end

static NSMutableArray *g_menuTargets_;  /* retained AquaMenuItemTargets */

static NSMutableArray *menuTargets_(void) {
    if (!g_menuTargets_) {
        g_menuTargets_ = [[NSMutableArray alloc] init];
    }
    return g_menuTargets_;
}

static BOOL g_activated_;
static BOOL g_appleMenuSet_;

static void activateApp_(void) {
    if (!g_activated_) {
        g_activated_ = YES;
        [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
    }
}

/* The Aqua host uses a bare NSApplication (no nib, no SDL cocoa backend), so it
   has no main menu by default.  Create + install one the first time it is
   needed; the menu items are added to this same menu afterwards. */
static NSMenu *mainMenu_(void) {
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *menu = [app mainMenu];
    if (!menu) {
        menu = [[NSMenu alloc] initWithTitle:@"MainMenu"];
        [app setMainMenu:menu];
    }
    return menu;
}

/* Bounds-checked: the widget kit drives menu inserts before menus are complete. */
static NSMenuItem *itemAt_Menu_(NSMenu *menu, int index) {
    if (index < 0 || index >= [menu numberOfItems]) {
        return nil;
    }
    return [menu itemAtIndex:index];
}

static AquaMenuItemTarget *postingTarget_(const char *command) {
    AquaMenuItemTarget *target = [[[AquaMenuItemTarget alloc] initWithCommand:command] autorelease];
    [menuTargets_() addObject:target]; /* kept alive for the app's lifetime */
    return target;
}

/* ------------------------------------------------------- key equivalents -- */

static NSString *keyEquivalent_(int key) {
    if (key > 0) {
        switch (key) {
            case SDLK_LEFT:  return @"\u2190";
            case SDLK_RIGHT: return @"\u2192";
            case SDLK_UP:    return @"\u2191";
            case SDLK_DOWN:  return @"\u2193";
            default: break;
        }
        if (key < 0x2500) {
            unichar ch = (unichar) key;
            return [NSString stringWithCharacters:&ch length:1];
        }
    }
    return @"";
}

static unsigned int modMask_(int kmods) {
    unsigned int m = 0;
    if (kmods & KMOD_GUI)   m |= NSCommandKeyMask;
    if (kmods & KMOD_ALT)   m |= NSAlternateKeyMask;
    if (kmods & KMOD_CTRL)  m |= NSControlKeyMask;
    if (kmods & KMOD_SHIFT) m |= NSShiftKeyMask;
    return m;
}

static void setShortcut_(NSMenuItem *item, int key, int kmods) {
    if (key < 0) {
        return;
    }
    [item setKeyEquivalentModifierMask:modMask_(kmods)];
    [item setKeyEquivalent:keyEquivalent_(key)];
}

/* ------------------------------------------------------ menu item building -- */

static NSMenuItem *makeItem_(NSString *title, const iMenuItem *entry, BOOL isDisabled) {
    const char *command = (entry->command && entry->command[0]) ? entry->command : NULL;
    if (command && startsWith_CStr(command, "submenu id:")) {
        NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:title action:nil
                                               keyEquivalent:@""] autorelease];
        NSMenu *sub = [[[NSMenu alloc] initWithTitle:title] autorelease];
        [item setSubmenu:sub];
        [item setEnabled:!isDisabled];
        return item;
    }
    NSMenuItem *item;
    if (command) {
        item = [[[NSMenuItem alloc] initWithTitle:title action:@selector(post:)
                                   keyEquivalent:@""] autorelease];
        [item setTarget:postingTarget_(command)];
        setShortcut_(item, entry->key, entry->kmods);
    }
    else {
        item = [[[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""] autorelease];
    }
    [item setEnabled:!isDisabled];
    return item;
}

static void populateMenu_(NSMenu *menu, const iMenuItem *items, size_t n, int at) {
    for (size_t i = 0; i < n && items[i].label; ++i) {
        const char *label = items[i].label;
        if (equal_CStr(label, "---")) {
            [menu insertItem:[NSMenuItem separatorItem] atIndex:at++];
            continue;
        }
        iBool isDisabled = iFalse;
        if (startsWith_CStr(label, "///") || startsWith_CStr(label, "```")) {
            isDisabled = iTrue;
            label += 3;
        }
        NSMenuItem *item = makeItem_(
            [NSString stringWithCString:translateCStr_Lang(label)
                               encoding:NSUTF8StringEncoding],
            &items[i], isDisabled);
        [menu insertItem:item atIndex:at++];
    }
}

void insertMenuItems_MacOS(const char *menuLabel, int atIndex, int firstItemIndex,
                           const iMenuItem *items, size_t count) {
    activateApp_();
    ensureAppMenu_();
    /* The widget kit inserts File/Edit/View/Bookmarks/Identity (1..5) then Help
       (7), leaving index 6 free for the Window menu.  Slot it in when the last
       top-level menu (Help, atIndex 7) is being created. */
    if (firstItemIndex == 0 && atIndex >= 7) {
        ensureWindowMenu_();
    }
    NSMenu *appMenu = mainMenu_();
    if (!appMenu) return;
    NSString *label = [NSString stringWithCString:translateCStr_Lang(menuLabel)
                                         encoding:NSUTF8StringEncoding];
    NSMenuItem *mainItem;
    NSMenu     *menu;
    if (firstItemIndex == 0) {
        mainItem = [[NSMenuItem alloc] initWithTitle:label action:nil keyEquivalent:@""];
        [appMenu insertItem:mainItem atIndex:atIndex];
        [mainItem release];
        menu = [[NSMenu alloc] initWithTitle:label];
        [mainItem setSubmenu:menu];
        [menu release];
    }
    else {
        mainItem = itemAt_Menu_(appMenu, atIndex);
        menu = [mainItem submenu];
    }
    if (!menu) return;
    [menu setAutoenablesItems:NO];
    int at = (firstItemIndex == 0) ? [menu numberOfItems] : firstItemIndex;
    populateMenu_(menu, items, count, at);
}

void updateMenuItems_MacOS(int atIndex, const iMenuItem *items, size_t count) {
    NSMenu *appMenu = mainMenu_();
    NSMenuItem *mainItem = itemAt_Menu_(appMenu, atIndex);
    NSMenu *menu = [mainItem submenu];
    if (!menu) return;
    /* 10.4 NSMenu has no removeAllItems; drop them one at a time. */
    while ([menu numberOfItems] > 0) {
        [menu removeItemAtIndex:0];
    }
    populateMenu_(menu, items, count, 0);
}

void removeMenu_MacOS(int atIndex) {
    NSMenu *appMenu = mainMenu_();
    if (!appMenu || atIndex >= [appMenu numberOfItems]) return;
    [appMenu removeItemAtIndex:atIndex];
}

void removeMenuItems_MacOS(int atIndex, int firstItem, int numItems) {
    NSMenu *appMenu = mainMenu_();
    NSMenuItem *mainItem = itemAt_Menu_(appMenu, atIndex);
    NSMenu *menu = [mainItem submenu];
    if (!menu) return;
    for (int i = 0; i < numItems; i++) {
        [menu removeItemAtIndex:firstItem];
    }
}

void enableMenu_MacOS(const char *menuLabel, iBool enable) {
    NSMenu *appMenu = mainMenu_();
    NSString *label = [NSString stringWithCString:translateCStr_Lang(menuLabel)
                                         encoding:NSUTF8StringEncoding];
    int idx = [appMenu indexOfItemWithTitle:label];
    if (idx == NSNotFound) return;
    [itemAt_Menu_(appMenu, idx) setEnabled:enable];
}

void enableMenuIndex_MacOS(int index, iBool enable) {
    [itemAt_Menu_(mainMenu_(), index) setEnabled:enable];
}

void enableMenuItem_MacOS(const char *menuItemCommand, iBool enable) {
    NSMenu *appMenu = mainMenu_();
    NSEnumerator *mainEnum = [[appMenu itemArray] objectEnumerator];
    NSMenuItem *mainItem;
    while ((mainItem = [mainEnum nextObject])) {
        NSMenu *menu = [mainItem submenu];
        if (menu) {
            NSEnumerator *enu = [[menu itemArray] objectEnumerator];
            NSMenuItem *item;
            while ((item = [enu nextObject])) {
                id target = [item target];
                if (target && [target respondsToSelector:@selector(command)] &&
                    !iCmpStr([(AquaMenuItemTarget *) target command], menuItemCommand)) {
                    [item setEnabled:enable];
                    return;
                }
            }
        }
    }
}

void enableMenuItemsByKey_MacOS(int key, int kmods, iBool enable) {
    NSMenu *appMenu = mainMenu_();
    NSString *equiv = keyEquivalent_(key);
    unsigned int mask = modMask_(kmods);
    NSEnumerator *mainEnum = [[appMenu itemArray] objectEnumerator];
    NSMenuItem *mainItem;
    while ((mainItem = [mainEnum nextObject])) {
        NSMenu *menu = [mainItem submenu];
        if (menu) {
            NSEnumerator *enu = [[menu itemArray] objectEnumerator];
            NSMenuItem *item;
            while ((item = [enu nextObject])) {
                if ([item keyEquivalentModifierMask] == mask &&
                    [[item keyEquivalent] isEqualToString:equiv]) {
                    [item setEnabled:enable];
                }
            }
        }
    }
}

void enableMenuItemsOnHomeRow_MacOS(iBool enable) {
    static const char *homeKeys[] = {
        "f", "d", "s", "a", "j", "k", "l", "r", "e", "w", "q",
        "u", "i", "o", "p", "v", "c", "x", "z", "m", "n", "g", "h", "b", "t", "y"
    };
    NSMenu *appMenu = mainMenu_();
    NSEnumerator *mainEnum = [[appMenu itemArray] objectEnumerator];
    NSMenuItem *mainItem;
    while ((mainItem = [mainEnum nextObject])) {
        NSMenu *menu = [mainItem submenu];
        if (menu) {
            NSEnumerator *enu = [[menu itemArray] objectEnumerator];
            NSMenuItem *item;
            while ((item = [enu nextObject])) {
                if ([item keyEquivalentModifierMask] == 0) {
                    NSString *equiv = [item keyEquivalent];
                    for (size_t i = 0; i < sizeof(homeKeys) / sizeof(homeKeys[0]); ++i) {
                        if ([equiv isEqualToString:[NSString stringWithUTF8String:homeKeys[i]]]) {
                            [item setEnabled:enable];
                            break;
                        }
                    }
                }
            }
        }
    }
}

void handleCommand_MacOS(const char *cmd) {
    NSApplication *app = [NSApplication sharedApplication];
    if (equal_Command(cmd, "lang.changed")) {
        localizeApplicationMenu_MacOS();
    }
    else if (equal_Command(cmd, "emojipicker")) {
        [app orderFrontCharacterPalette:nil];
    }
}

/* The application menu (About / Preferences / Hide / Quit) at index 0.  It must
   exist at init (the widget kit inserts the other menus at 1..7, so index 0
   must be present), and its submenu holds the app-level items.  AppKit also
   synthesises an app-process menu labelled with the app name, so a separate
   "app name" menu bar element is unavoidable on this host. */
static void ensureAppMenu_(void) {
    NSMenu *appMenu = mainMenu_();
    if (!appMenu) return;

    NSMenuItem *appItem = itemAt_Menu_(appMenu, 0);
    if (!appItem) {
        appItem = [[NSMenuItem alloc] initWithTitle:@"L4" action:nil keyEquivalent:@""];
        [appMenu insertItem:appItem atIndex:0];
        [appItem release];
        NSMenu *sub = [[NSMenu alloc] initWithTitle:@"L4"];
        [appItem setSubmenu:sub];
        [sub release];
    }
    NSMenu *appSubmenu = [appItem submenu];
    /* Nominate this submenu as the application menu so Tiger/Leopard's AppKit
       uses it (bold app-name at index 0) instead of prepending its own
       synthesised app-name menu — the two-"L4" menu-bar bug. */
    if (appSubmenu && !g_appleMenuSet_) {
        [[NSApplication sharedApplication] setAppleMenu:appSubmenu];
        g_appleMenuSet_ = YES;
    }
    if (appSubmenu && [appSubmenu numberOfItems] == 0) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"About Lagrange"
                                                      action:@selector(post:)
                                               keyEquivalent:@""];
        [item setTarget:postingTarget_("!open newtab:1 switch:1 url:about:lagrange")];
        [appSubmenu addItem:item];
        [item release];
        [appSubmenu addItem:[NSMenuItem separatorItem]];
        item = [[NSMenuItem alloc] initWithTitle:@"Preferences…"
                                          action:@selector(post:)
                                   keyEquivalent:@","];
        [item setKeyEquivalentModifierMask:NSCommandKeyMask];
        [item setTarget:postingTarget_("preferences")];
        [appSubmenu addItem:item];
        [item release];
        [appSubmenu addItem:[NSMenuItem separatorItem]];
        item = [[NSMenuItem alloc] initWithTitle:@"Services" action:nil keyEquivalent:@""];
        [item setEnabled:NO];
        [appSubmenu addItem:item];
        [item release];
        [appSubmenu addItem:[NSMenuItem separatorItem]];
        item = [[NSMenuItem alloc] initWithTitle:@"Hide L4" action:@selector(hide:)
                                  keyEquivalent:@"h"];
        [item setKeyEquivalentModifierMask:NSCommandKeyMask];
        [item setTarget:[NSApplication sharedApplication]];
        [appSubmenu addItem:item];
        [item release];
        item = [[NSMenuItem alloc] initWithTitle:@"Hide Others"
                                          action:@selector(hideOtherApplications:)
                                   keyEquivalent:@"h"];
        [item setKeyEquivalentModifierMask:(NSCommandKeyMask | NSAlternateKeyMask)];
        [item setTarget:[NSApplication sharedApplication]];
        [appSubmenu addItem:item];
        [item release];
        item = [[NSMenuItem alloc] initWithTitle:@"Show All"
                                          action:@selector(unhideAllApplications:)
                                   keyEquivalent:@""];
        [item setTarget:[NSApplication sharedApplication]];
        [appSubmenu addItem:item];
        [item release];
        [appSubmenu addItem:[NSMenuItem separatorItem]];
        item = [[NSMenuItem alloc] initWithTitle:@"Quit L4" action:@selector(post:)
                                  keyEquivalent:@"q"];
        [item setKeyEquivalentModifierMask:NSCommandKeyMask];
        [item setTarget:postingTarget_("quit")];
        [appSubmenu addItem:item];
        [item release];
        [appSubmenu setAutoenablesItems:NO];
    }
}

/* The Window menu (tabs / minimize / zoom / close) at index 6.  Created only
   when the widget kit is about to insert the last top-level menu (Help at 7),
   so the bar ends up in the conventional App,File,…,Window,Help order. */
void ensureWindowMenu_(void) {
    NSMenu *appMenu = mainMenu_();
    if (!appMenu) return;
    NSMenuItem *windowItem = itemAt_Menu_(appMenu, 6);
    if (!windowItem) {
        windowItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
        [appMenu insertItem:windowItem atIndex:6];
        [windowItem release];
        NSMenu *sub = [[NSMenu alloc] initWithTitle:@"Window"];
        [windowItem setSubmenu:sub];
        [sub release];
    }
    NSMenu *windowSubmenu = [windowItem submenu];
    if (windowSubmenu && [windowSubmenu numberOfItems] == 0) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"Next Tab"
                                                      action:@selector(post:)
                                               keyEquivalent:@""];
        [item setKeyEquivalentModifierMask:NSControlKeyMask];
        [item setTarget:postingTarget_("tabs.next")];
        [windowSubmenu addItem:item];
        [item release];
        item = [[NSMenuItem alloc] initWithTitle:@"Previous Tab"
                                          action:@selector(post:)
                                   keyEquivalent:@""];
        [item setKeyEquivalentModifierMask:(NSControlKeyMask | NSShiftKeyMask)];
        [item setTarget:postingTarget_("tabs.prev")];
        [windowSubmenu addItem:item];
        [item release];
        [windowSubmenu addItem:[NSMenuItem separatorItem]];
        item = [[NSMenuItem alloc] initWithTitle:@"Minimize"
                                          action:@selector(performMiniaturize:)
                                   keyEquivalent:@"m"];
        [item setKeyEquivalentModifierMask:NSCommandKeyMask];
        [item setTarget:nil]; /* key window */
        [windowSubmenu addItem:item];
        [item release];
        item = [[NSMenuItem alloc] initWithTitle:@"Zoom"
                                          action:@selector(performZoom:)
                                   keyEquivalent:@""];
        [item setTarget:nil];
        [windowSubmenu addItem:item];
        [item release];
        [windowSubmenu addItem:[NSMenuItem separatorItem]];
        item = [[NSMenuItem alloc] initWithTitle:@"Close Window"
                                          action:@selector(post:)
                                   keyEquivalent:@"w"];
        [item setKeyEquivalentModifierMask:NSCommandKeyMask];
        [item setTarget:postingTarget_("tabs.close")];
        [windowSubmenu addItem:item];
        [item release];
        [windowSubmenu setAutoenablesItems:NO];
    }
}

void localizeApplicationMenu_MacOS(void) {
    NSMenuItem *appMenuItem = itemAt_Menu_(mainMenu_(), 0);
    NSMenu *sub;
    if (!appMenuItem || !(sub = [appMenuItem submenu])) return;
    int n = [sub numberOfItems];
    if (n >= 11) {
        [[sub itemAtIndex:0] setTitle:[NSString stringWithUTF8String:cstr_Lang("menu.aboutapp")]];
        [[sub itemAtIndex:2] setTitle:[NSString stringWithUTF8String:cstr_Lang("menu.preferences")]];
        [[sub itemAtIndex:n - 5] setTitle:[NSString stringWithUTF8String:cstr_Lang("macos.menu.hide")]];
        [[sub itemAtIndex:n - 4] setTitle:[NSString stringWithUTF8String:cstr_Lang("macos.menu.hideothers")]];
        [[sub itemAtIndex:n - 3] setTitle:[NSString stringWithUTF8String:cstr_Lang("macos.menu.showall")]];
        [[sub itemAtIndex:n - 1] setTitle:[NSString stringWithUTF8String:cstr_Lang("menu.quit")]];
    }
}

iBool hasNativeMenu_Platform(void) {
    return iTrue;
}

/* 10.4 shows context menus only through +[NSMenu popUpContextMenu:withEvent:forView:],
   which needs an NSEvent; the shim does not keep the originating mouse event, so
   context-menu popups are a follow-up (the menu is built and wired here). */
void showPopupMenu_MacOS(iWidget *source, iInt2 windowCoord, const iMenuItem *items, size_t n) {
    (void) windowCoord;
    activateApp_();
    NSMenu *menu = [[NSMenu alloc] init];
    [menu setAutoenablesItems:NO];
    int at = 0;
    for (size_t i = 0; i < n && items[i].label; ++i) {
        const char *label = items[i].label;
        if (equal_CStr(label, "---")) {
            [menu insertItem:[NSMenuItem separatorItem] atIndex:at++];
            continue;
        }
        AquaMenuItemTarget *target = NULL;
        const char *command = (items[i].command && items[i].command[0]) ? items[i].command : NULL;
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:
                                [NSString stringWithCString:translateCStr_Lang(label)
                                                   encoding:NSUTF8StringEncoding]
                                                      action:(command ? @selector(post:) : nil)
                                               keyEquivalent:@""];
        if (command) {
            target = postingTarget_(command);
            [target setSource:source];
            [item setTarget:target];
            setShortcut_(item, items[i].key, items[i].kmods);
        }
        [menu insertItem:item atIndex:at++];
        [item release];
    }
    [menu release];
}
