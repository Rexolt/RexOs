#!/usr/bin/env python3
"""libdom javítás: a class-gyorsítótár frissítése attribútum-értékváltozáskor.

A libdom minden elemen külön tömbben tartja a class-neveket (`ele->classes`),
mert a CSS-illesztés ezt kérdezi (`dom_element_has_class`). Ez a tömb viszont
CSAK az attribútum LÉTREHOZÁSAKOR épül fel: ha a class attribútum már létezik
és az értéke változik meg -- amit a JavaScript `elem.className = "..."`,
`elem.classList.add(...)` és `elem.setAttribute("class", ...)` mind így csinál
--, a gyorsítótár a régi értéket őrzi tovább.

Következmény: a stíluscsere JavaScriptből nem működik; a DOM-ban már az új
osztály van, de a CSS-illesztő a régit látja.

Idempotens: a 'rexos-class-cache' marker jelzi a megpatchelt fájlt.
"""
import sys

path = sys.argv[1] + "/src/core/element.c"
src = open(path).read()

if 'rexos-class-cache' in src:
    print("[patch-libdom-class] mar be van patchelve")
    sys.exit(0)

ANCHOR = """		err = dom_attr_set_value(match->attr, value);
		if (err != DOM_NO_ERR)
			return err;

		success = true;"""

FIXED = """		err = dom_attr_set_value(match->attr, value);
		if (err != DOM_NO_ERR)
			return err;

		/* rexos-class-cache: a class-gyorsítótár csak az attribútum
		 * létrehozásakor épült fel, értékváltozáskor nem frissült. */
		if (namespace == NULL &&
				dom_string_isequal(name, doc->class_string)) {
			err = _dom_element_create_classes(element,
					value != NULL ?
						dom_string_data(value) : "");
			if (err != DOM_NO_ERR)
				return err;
		}

		success = true;"""

if ANCHOR not in src:
    sys.exit("[patch-libdom-class] HIBA: a set_attr ertekcsere aga nem talalhato")

open(path, "w").write(src.replace(ANCHOR, FIXED, 1))
print("[patch-libdom-class] kesz")
