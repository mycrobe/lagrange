/*
 * darwin8 (Tiger PPC) archive-read proof -- the_Foundation iFile/iArchive.
 *
 * Cross-built by osx/CMakeLists.txt (d8_archive_smoke target) and run ON the
 * target (petal, 10.4.11).  This is the on-device proof that the_Foundation's
 * ZIP/archive reading (readDirectory_Archive_ / seekToCentralEnd_ / iFile)
 * works on a big-endian PPC.  The full Aqua app aborts at resource loading
 * because openFile_Archive()->readDirectory_Archive_() failed on the PPC
 * target while the host loaded the same resources.lgr fine -- root cause: the
 * stream byte-order logic keyed on the wrong macro (iBigEndian) so the little-
 * endian ZIP central directory was read byte-swapped.  This smoke is the
 * minimal reproducer: open the archive, report the entry count, and drain a
 * signature entry's bytes so both the directory parse and a zlib inflate path
 * are exercised on-device.
 *
 * Usage:  d8_archive_smoke <resources.lgr> [entryToInspect]
 */
#include "the_Foundation/archive.h"
#include "the_Foundation/string.h"

#include <stdio.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "resources.lgr";
    const char *inspect = argc > 2 ? argv[2] : NULL;

    setbuf(stdout, NULL); /* unbuffered: crash logs must not eat the output */
    init_Foundation();

    int rc = 1;
    iArchive *arch = new_Archive();
    iString pathStr, inspectStr;
    initCStr_String(&pathStr, path);
    initCStr_String(&inspectStr, "");

    if (!openFile_Archive(arch, &pathStr)) {
        printf("[d8_archive_smoke] FAIL: openFile_Archive(%s) returned iFalse\n",
               path);
        goto done;
    }

    const size_t count = numEntries_Archive(arch);
    printf("[d8_archive_smoke] OK: opened %s (%zu entries, source bytes=%zu)\n",
           path, count, sourceSize_Archive(arch));
    if (count == 0) {
        printf("[d8_archive_smoke] FAIL: no entries parsed\n");
        goto done;
    }

    /* Dump the first few paths so a run's artifact names real entries. */
    const size_t shown = count < 5 ? count : 5;
    for (size_t i = 0; i < shown; i++) {
        const iArchiveEntry *entry = entryAt_Archive(arch, i);
        printf("[d8_archive_smoke]   entry[%zu] '%s' size=%zu comp=%d\n",
               i, cstr_String(&entry->path), entry->size, entry->compression);
    }

    /* Drain a requested entry (exercises the local-header seek + inflate). */
    if (inspect) {
        initCStr_String(&inspectStr, inspect);
        const iBlock *data = data_Archive(arch, &inspectStr);
        if (!data) {
            printf("[d8_archive_smoke] FAIL: entry '%s' not found or empty\n",
                   inspect);
            goto done;
        }
        const char *sig = constData_Block(data);
        const size_t len = size_Block(data);
        printf("[d8_archive_smoke] OK: inspected '%s' (%zu bytes, head '%.*s')\n",
               inspect, len, (int) (len < 24 ? len : 24), sig);
    }

    rc = 0;

done:
    iRelease(arch);
    deinit_String(&inspectStr);
    deinit_String(&pathStr);
    deinit_Foundation();
    return rc;
}
