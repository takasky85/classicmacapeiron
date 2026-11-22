/* Runs the in-game extractor (src/convert.c) on a zip from the command line, so its output can be
 * compared with tools/extract_assets.py's:  convert_test <game.zip> <out dir> */
#include "apeiron.h"
#include <stdarg.h>

Assets A;
void diag_note(const char *fmt, ...) { (void)fmt; }
static void say(void *ud, const char *line) { (void)ud; if (line) printf("%s\n", line); }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: convert_test game.zip outdir\n"); return 2; }
    printf("zip ok: %d\n", convert_zip_ok(argv[1]));
    char err[400] = "";
    bool ok = convert_zip(argv[1], argv[2], say, NULL, err, sizeof err);
    if (!ok) { fprintf(stderr, "FAILED: %s\n", err); return 1; }
    return 0;
}
