/* Drives the on-screen "make the artwork" flow under the stub: make_test <game.zip> */
#include "apeiron.h"
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    if (!plat_init("t")) return 1;
    char out[1200] = "";
    bool offered = getenv("OFFER") ? assets_offer_zip(argv[1]) : false;
    bool ok = assets_make_from_zip(argv[1], out, sizeof out);
    printf("offer=%d make=%d out=%s\n", offered, ok, out);
    return ok ? 0 : 1;
}
