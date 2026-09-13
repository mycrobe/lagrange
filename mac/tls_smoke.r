/*
 * tls_smoke resources -- memory partition ONLY (same as cn_ot_smoke.r): the
 * the_Foundation portable core + mbedTLS-ppc + OT/TLS slice exceeds the
 * toolchain template's 1 MB SIZE.  Later resources replace earlier ones, so
 * listing this after the template overrides the template's SIZE.
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
