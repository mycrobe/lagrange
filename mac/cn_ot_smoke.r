/*
 * cn_ot_smoke resources -- memory partition ONLY.  A console app pulling
 * mbedTLS-ppc + the OT/TLS slice exceeds the toolchain template's 1 MB
 * SIZE ("not enough memory available" at guest launch).  Same shape as
 * starscape's guest_suite.r / the app's gemini.r.in SIZE block: later
 * resources REPLACE earlier ones in the final fork, so listing this .r
 * after the template is the sanctioned override (add_application appends
 * custom .r args after the template).
 */
#include "Processes.r"

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground,
    doesActivateOnFGSwitch, backgroundAndForeground, dontGetFrontClicks,
    ignoreChildDiedEvents, is32BitCompatible, isHighLevelEventAware,
    onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    32768 * 1024,   /* preferred 32 MB */
    16384 * 1024    /* minimum   16 MB */
};
