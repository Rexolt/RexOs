#!/usr/bin/env python3
"""A NetSurf html tartalomkezelőjét köti rá a RexOS dobozfa-újraépítőre:

  - dom_event.c: a DOM-mutációs callbackek jelzik a változást,
  - html.c:      a tartalom megszűnésekor lemondja a függő újraépítést,
  - Makefile:    az új forrásfájl bekerül az S_HTML listába.

Idempotens: a 'rexos-relayout' marker jelzi a már megpatchelt fájlt.
"""
import sys

NS = sys.argv[1]  # a netsurf forrásfa gyökere

def patch(path, fn):
    src = open(path).read()
    if 'rexos-relayout' in src:
        print(f"[patch-html-relayout] {path}: mar be van patchelve")
        return
    out = fn(src)
    open(path, "w").write(out)
    print(f"[patch-html-relayout] {path}: kesz")


def do_dom_event(s):
    inc = '#include "html/dom_event.h"'
    assert inc in s, "dom_event.h include nem talalhato"
    s = s.replace(inc, inc + '\n#include "html/relayout_rexos.h" /* rexos-relayout */', 1)

    # Minden mutációs callback ELSŐ utasításként jelzi a változást. Azért az
    # elejére kerül, mert a callbackek több ágon korán visszatérnek.
    for cb in ("dom_default_action_DOMNodeInserted_cb(struct dom_event *evt, void *pw)",
               "dom_default_action_DOMSubtreeModified_cb(struct dom_event *evt, void *pw)"):
        marker = cb + "\n{\n"
        assert marker in s, f"nem talalhato: {cb}"
        s = s.replace(marker, marker + "\thtml_rexos_dom_mutated(pw); /* rexos-relayout */\n", 1)

    # Az attribútum-változás (pl. elem.style / setAttribute) is layoutot igényel,
    # ehhez viszont az eredetiben nincs callback -> sajátot regisztrálunk.
    fetcher_anchor = '''	if (phase == DOM_DEFAULT_ACTION_END) {
		if (dom_string_isequal(type, corestring_dom_DOMNodeInserted)) {'''
    assert fetcher_anchor in s
    s = s.replace(fetcher_anchor,
'''	if (phase == DOM_DEFAULT_ACTION_END) {
		if (dom_string_isequal(type, corestring_dom_DOMAttrModified)) {
			return dom_rexos_attr_modified_cb; /* rexos-relayout */
		} else if (dom_string_isequal(type, corestring_dom_DOMNodeInserted)) {''', 1)

    # a saját callback definíciója a fetcher elé
    fetcher_def = "/* exported interface documented in html/dom_event.c */\ndom_default_action_callback\nhtml_dom_event_fetcher("
    assert fetcher_def in s
    s = s.replace(fetcher_def,
'''/**
 * DOMAttrModified: attribútum változott (rexos-relayout).
 */
static void
dom_rexos_attr_modified_cb(struct dom_event *evt, void *pw)
{
	(void)evt;
	html_rexos_dom_mutated(pw);
}


''' + fetcher_def, 1)
    return s


def do_html_c(s):
    inc = '#include "html/form_internal.h"'
    assert inc in s, "form_internal.h include nem talalhato a html.c-ben"
    s = s.replace(inc, inc + '\n#include "html/relayout_rexos.h" /* rexos-relayout */', 1)

    anchor = '''	/* If we're still converting a layout, cancel it */
	if (html->box_conversion_context != NULL) {'''
    assert anchor in s
    s = s.replace(anchor,
'''	/* Függőben lévő RexOS dobozfa-újraépítés lemondása (rexos-relayout) */
	html_rexos_relayout_cancel(html);

''' + anchor, 1)
    return s


def do_makefile(s):
    anchor = "	imagemap.c		\\\n"
    assert anchor in s, "imagemap.c nem talalhato az S_HTML listaban"
    return s.replace(anchor, anchor + "	relayout_rexos.c	\\\n", 1)  # rexos-relayout


def do_select_c(s):
    """A libcss node-adat gyorsítótár eldobásához kell egy publikus belépő:
    a `selection_handler` a select.c-ben statikus, kívülről nem érhető el."""
    anchor = "css_error get_libcss_node_data(void *pw, void *node, void **libcss_node_data)"
    assert anchor in s
    helper = '''/* rexos-relayout: egy DOM node libcss node-adatának eldobása.
 *
 * A dobozfa újraépítésekor a régi kiválasztási eredmények érvénytelenek. A
 * user-data rekeszt ELŐSZÖR ürítjük, és csak utána semmisítjük meg az adatot,
 * így a libcss destruktora nem hív vissza a set_libcss_node_data()-ba (ami az
 * "old_node_data == NULL" assertbe futna).
 */
void nscss_drop_node_data(struct dom_node *n)
{
\tvoid *data = NULL;
\tvoid *old = NULL;

\tif (dom_node_get_user_data(n, corestring_dom___ns_key_libcss_node_data,
\t\t\t&data) != DOM_NO_ERR || data == NULL) {
\t\treturn;
\t}

\tif (dom_node_set_user_data(n, corestring_dom___ns_key_libcss_node_data,
\t\t\tNULL, NULL, (void *) &old) != DOM_NO_ERR) {
\t\treturn;
\t}

\t(void) css_libcss_node_data_handler(&selection_handler,
\t\t\tCSS_NODE_DELETED, NULL, n, NULL, data);
}


'''
    return s.replace(anchor, helper + anchor, 1)


def do_select_h(s):
    marker = "#endif"
    assert marker in s
    decl = '''
/* rexos-relayout */
struct dom_node;
void nscss_drop_node_data(struct dom_node *n);

'''
    idx = s.rfind(marker)
    return s[:idx] + decl + s[idx:]


patch(f"{NS}/content/handlers/css/select.c", do_select_c)
patch(f"{NS}/content/handlers/css/select.h", do_select_h)
patch(f"{NS}/content/handlers/html/dom_event.c", do_dom_event)
patch(f"{NS}/content/handlers/html/html.c", do_html_c)

mkpath = f"{NS}/content/handlers/html/Makefile"
mk = open(mkpath).read()
if "relayout_rexos.c" in mk:
    print("[patch-html-relayout] Makefile: mar be van patchelve")
else:
    open(mkpath, "w").write(do_makefile(mk))
    print("[patch-html-relayout] Makefile: kesz")
