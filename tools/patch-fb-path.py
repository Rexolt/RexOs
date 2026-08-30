#!/usr/bin/env python3
"""A NetSurf framebuffer frontend csonk path-plotterét cseréli le a RexOS
scanline raszterizálójára (frontends/framebuffer/fb_plot_path.c)."""
import sys

path = sys.argv[1]
src = open(path).read()

STUB = '''static nserror
framebuffer_plot_path(const struct redraw_context *ctx,
\t\tconst plot_style_t *pstyle,
\t\tconst float *p,
\t\tunsigned int n,
\t\tconst float transform[6])
{
\tNSLOG(netsurf, INFO, "path unimplemented");
\treturn NSERROR_OK;
}'''

REPLACEMENT = '''/* rexos-plot-path: a valódi raszterizáló külön fájlban van.
 * #include-dal jön be, mert a framebuffer.c fájl-szintű statikus `nsfb`
 * mutatójára van szüksége. */
#include "fb_plot_path.c"'''

if 'rexos-plot-path' in src:
    print("[patch-fb-path] mar be van patchelve")
    sys.exit(0)

if STUB not in src:
    sys.exit("[patch-fb-path] HIBA: a framebuffer.c path-csonkja nem talalhato")

open(path, "w").write(src.replace(STUB, REPLACEMENT, 1))
print("[patch-fb-path] kesz")
