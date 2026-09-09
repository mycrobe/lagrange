/* touch_stub.c — dead-end touch layer for the canvas host (Phase 0).

   The canvas build excludes src/ui/touch.c: finger/gesture tracking is an
   iOS/Android concept and stays out of the Aqua and Toolbox ports. Everything
   below is a no-op that satisfies the touch.h linkage surface. */

#include "ui/touch.h"
#include <the_Foundation/vec2.h>

iBool processEvent_Touch(const SDL_Event *ev) {
    (void) ev;
    return iFalse;
}

void update_Touch(void) {}
void clear_Touch(void) {}

float stopWidgetMomentum_Touch(const iWidget *widget) {
    (void) widget;
    return 0.0f;
}

enum iWidgetTouchMode widgetMode_Touch(const iWidget *widget) {
    (void) widget;
    return none_WidgetTouchMode;
}

void widgetDestroyed_Touch(iWidget *widget) { (void) widget; }
void transferAffinity_Touch(iWidget *src, iWidget *dst) { (void) src; (void) dst; }

iBool hasAffinity_Touch(const iWidget *widget) {
    (void) widget;
    return iFalse;
}

iInt2 latestPosition_Touch(void)    { return init1_I2(0); }
iInt2 latestTapPosition_Touch(void) { return init1_I2(0); }
iInt2 fingerPosition_Touch(void)    { return init1_I2(0); }

iBool isHovering_Touch(void)    { return iFalse; }
iBool isInteracting_Touch(void) { return iFalse; }

size_t numFingers_Touch(void) { return 0; }
