/* ============================================================================
 *  RexOS - dobozfa újraépítése DOM-változás után
 *
 *  A NetSurf 3.11 a dobozfát (box tree) EGYSZER építi fel, a dokumentum
 *  beparse-olása után. Ha ezután a JavaScript hozzányúl a DOM-hoz -- új elemet
 *  szúr be, szöveget cserél, attribútumot állít --, a DOM ugyan megváltozik,
 *  de a dobozfa nem: a képernyőn semmi sem történik. Emiatt tűnt úgy, hogy a
 *  RexOS-en "nem működik a JavaScript", holott az időzítők és a kódfuttatás
 *  rendben voltak, csak a látható eredmény hiányzott.
 *
 *  Ez a modul pótolja a hiányzó lépést: DOM-változásra késleltetve újraépíti a
 *  dobozfát a jelenlegi DOM-ból, majd újralayoutol.
 *
 *  A LEGFONTOSABB SZABÁLY: a RÉGI FA VÉGIG ÉLETBEN MARAD.
 *
 *  A `dom_to_box()` szakaszosan dolgozik (10 elem, majd visszaad az ütemezőnek),
 *  úgyhogy egy nagy oldal újraépítése sok tized másodperc. Ez alatt a böngésző
 *  többi része tovább fut: rajzol, egeret/billentyűt kezel, kijelölést követ --
 *  és mindegyik ág a `html->layout`-ra épül (22 helyen). Ha az újraépítés
 *  elejére eldobnánk a régi fát, ez az egész időszak felszabadított vagy NULL
 *  mutatókkal telne; a legkönnyebben a beérkező képek rajzolása üt be
 *  ("assertion box failed" a html_redraw-ban), de a lyuk ennél tágabb.
 *
 *  Ezért: építés közben a `layout` a RÉGI fára mutat (a képernyőn a régi
 *  tartalom marad), az új fa külön talloc-kontextusban épül, és csak akkor
 *  cseréljük ki, amikor teljesen kész -- egyetlen ponton, ütemezői
 *  visszaadás nélkül. A régi fát és a hozzá tartozó szerkezeteket ekkor
 *  engedjük el.
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>

#include "utils/config.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "utils/talloc.h"
#include "utils/corestrings.h"
#include "netsurf/misc.h"
#include "content/content.h"
#include "content/content_protected.h"
#include "desktop/gui_internal.h"
#include "desktop/selection.h"

#include "html/private.h"
#include "html/box.h"
#include "html/box_construct.h"
#include "html/imagemap.h"
#include "html/object.h"
#include "html/form_internal.h"
#include "css/select.h"
#include "html/relayout_rexos.h"

/** Ennyi ms tétlenség után építjük újra a dobozfát. */
#define RELAYOUT_DEBOUNCE_MS 120

/** Ilyen sűrűn nézzük meg, hogy az újratöltött képek megérkeztek-e. */
#define RELAYOUT_SETTLE_MS   150

/**
 * A folyamatban lévő csere állapota.
 *
 * Egyszerre egy újraépítés futhat; az `owner` egyben a "fut-e most" jelző is.
 */
static struct {
	html_content *owner;

	/* a régi, még használatban lévő szerkezetek */
	void                        *old_bctx;
	struct box                  *old_layout;
	struct form                 *old_forms;
	struct content_html_object  *old_objects;
	unsigned int                 old_num_objects;

	/* jött-e újabb DOM-változás, amíg az építés tartott */
	bool dirty;
} swap;

static void html_rexos_relayout_run(void *pw);
static void html_rexos_settle(void *pw);


/**
 * Szabad-e most újraépíteni ennek a tartalomnak a dobozfáját?
 */
static bool relayout_allowed(html_content *htmlc)
{
	if (htmlc == NULL) {
		return false;
	}
	/* Betöltés közben a parser úgyis felépíti majd a fát. */
	if (content__get_status(&htmlc->base) != CONTENT_STATUS_DONE) {
		return false;
	}
	/* Épp fut egy konverzió. */
	if (htmlc->box_conversion_context != NULL) {
		return false;
	}
	/* Layout közben nem nyúlunk a fához. */
	if (htmlc->reflowing) {
		return false;
	}
	if (htmlc->aborted) {
		return false;
	}
	/* Frameset/iframe esetén a böngészőablak külön gyerekablakokat tart a
	 * dobozokhoz kötve; ezek újraépítése külön kezelést kívánna. */
	if (htmlc->frameset != NULL || htmlc->iframe != NULL) {
		return false;
	}
	if (htmlc->document == NULL || htmlc->layout == NULL) {
		return false;
	}
	/* Csak akkor van értelme, ha volt már layout. */
	if (!htmlc->had_initial_layout) {
		return false;
	}
	return true;
}


/* exported interface documented in html/relayout_rexos.h */
void html_rexos_dom_mutated(struct html_content *htmlc)
{
	if (htmlc == NULL) {
		return;
	}

	/* Ha épp ennek a tartalomnak az újraépítése fut, ne dobjuk el a
	 * változást: az építés végén újraindítjuk. */
	if (swap.owner == htmlc) {
		swap.dirty = true;
		return;
	}

	if (!relayout_allowed(htmlc)) {
		return;
	}

	/* Debounce: egy JS-blokk sok apró módosítása egyetlen layoutba fusson. */
	guit->misc->schedule(-1, html_rexos_relayout_run, htmlc);
	guit->misc->schedule(RELAYOUT_DEBOUNCE_MS, html_rexos_relayout_run, htmlc);
}


/**
 * A `list` objektumlistát elengedi (a NetSurf saját takarítója a tartalom
 * aktuális listáján dolgozik, ezért ide-oda cseréljük).
 */
static void free_object_list(html_content *c,
			     struct content_html_object *list,
			     unsigned int num_kept)
{
	struct content_html_object *keep = c->object_list;

	c->object_list = list;
	html_object_free_objects(c);      /* a c->object_list-et NULL-ra viszi */
	c->object_list = keep;
	c->num_objects = num_kept;
}


/**
 * Visszaállítás, ha az újraépítés nem sikerült: a félkész új fát eldobjuk, és
 * minden marad a régiben.
 */
static void restore_old(html_content *c)
{
	if (swap.owner != c) {
		return;
	}

	/* az új (fél)fa és a hozzá kért objektumok eldobása */
	free_object_list(c, c->object_list, swap.old_num_objects);
	if (c->bctx != NULL) {
		talloc_free(c->bctx);
	}

	c->bctx        = swap.old_bctx;
	c->layout      = swap.old_layout;
	c->forms       = swap.old_forms;
	c->object_list = swap.old_objects;
	c->num_objects = swap.old_num_objects;

	swap.owner = NULL;
	swap.dirty = false;
}


/* exported interface documented in html/relayout_rexos.h */
void html_rexos_relayout_cancel(struct html_content *htmlc)
{
	guit->misc->schedule(-1, html_rexos_relayout_run, htmlc);
	guit->misc->schedule(-1, html_rexos_settle, htmlc);

	/* Ha a tartalom épp újraépítés közben szűnik meg, adjuk vissza neki a
	 * konzisztens régi állapotot, hogy a html_destroy azt tudja takarítani. */
	if (swap.owner == htmlc) {
		restore_old(htmlc);
	}
}


/**
 * Az újraépítés után a képeket újra le kell kérni; amíg meg nem jönnek, a
 * dobozaik nulla méretűek. A NetSurf saját "objektum megjött -> újralayout"
 * ága ilyenkor nem segít, mert a reflow-fojtás (min_reflow_period, alapból
 * 1 másodperc) épp az imént indult újra. Ezért megvárjuk, míg elfogynak az
 * aktív letöltések, és akkor layoutolunk még egyszer.
 */
static void html_rexos_settle(void *pw)
{
	html_content *c = pw;

	if (!relayout_allowed(c)) {
		return;
	}

	if (c->base.active > 0) {
		guit->misc->schedule(RELAYOUT_SETTLE_MS, html_rexos_settle, c);
		return;
	}

	if (c->base.available_width > 0) {
		content__reformat(&c->base, false,
				  c->base.available_width,
				  c->base.available_height);
	}
}


/**
 * Az új dobozfa elkészült: EKKOR cseréljük ki, és csak ekkor engedjük el a
 * régit. A `convert_xml_to_box` közvetlenül a visszahívás előtt már beírta az
 * új fát a `c->layout`-ba, és közben nem adott vissza az ütemezőnek, így a
 * csere a böngésző szempontjából egyetlen pillanat alatt történik.
 */
static void html_rexos_rebuild_done(html_content *c, bool success)
{
	c->box_conversion_context = NULL;

	if (swap.owner != c) {
		return;          /* nem tőlünk indult (elvileg nem fordul elő) */
	}

	if (success == false || c->layout == NULL ||
	    c->layout == swap.old_layout) {
		NSLOG(netsurf, WARNING,
		      "DOM-valtozas utani dobozfa-ujraepites sikertelen (%p)", c);
		restore_old(c);
		return;
	}

	/* A régi fára mutató szerkezetek elengedése -- FIGYELEM: a talloc
	 * kontextus eldobása ELŐTT, mert mind a régi dobozokra hivatkoznak. */
	free_object_list(c, swap.old_objects, c->num_objects);

	{
		struct form *f, *g;
		for (f = swap.old_forms; f != NULL; f = g) {
			g = f->prev;
			form_free(f);
		}
	}

	imagemap_destroy(c);                 /* a régi imagemap régi dobozokra mutat */

	if (c->sel != NULL) {
		selection_init(c->sel);      /* a kijelölés is */
	}
	c->selection_type = HTML_SELECTION_NONE;

	if (swap.old_bctx != NULL) {
		talloc_free(swap.old_bctx);  /* az egész régi fa egy lépésben */
	}

	swap.owner = NULL;

	if (imagemap_extract(c) != NSERROR_OK) {
		NSLOG(netsurf, WARNING, "imagemap ujraolvasas sikertelen");
	}

	/* Újralayout a jelenlegi ablakmérettel, majd a böngészőablak
	 * újrarajzolja magát a CONTENT_MSG_REFORMAT hatására. */
	if (c->base.available_width > 0) {
		content__reformat(&c->base, false,
				  c->base.available_width,
				  c->base.available_height);
	}

	/* A képek most töltődnek újra; ha megjöttek, kell még egy layout. */
	if (c->base.active > 0) {
		guit->misc->schedule(RELAYOUT_SETTLE_MS, html_rexos_settle, c);
	}

	/* Építés közben érkezett újabb változás? Akkor még egy kör. */
	if (swap.dirty) {
		swap.dirty = false;
		html_rexos_dom_mutated(c);
	}
}


/**
 * A DOM bejárása: minden node-ról leszedjük a régi dobozfához kötött
 * gyorsítótárakat.
 *
 *  - __ns_key_box_node_data: a node-hoz tartozó doboz mutatója. Az új
 *    konstrukció felülírja azoknál, amikhez doboz készül; a többinél a régi
 *    érték érvénytelenné válna, ezért itt nullázzuk. (A layout mindkét
 *    olvasási helye ellenőrzi a NULL-t.)
 *  - libcss node data: a korábbi kiválasztási eredmény. Ha bennmarad, a
 *    libcss a set_libcss_node_data() "old_node_data == NULL" assertjébe fut,
 *    és a régi stílus ragadna be (pl. a JS-ből cserélt class nem hatna).
 */
static void drop_node_caches(dom_node *node)
{
	dom_node *child = NULL;
	dom_node *next = NULL;
	void *old = NULL;

	(void) dom_node_set_user_data(node,
			corestring_dom___ns_key_box_node_data,
			NULL, NULL, (void *) &old);
	nscss_drop_node_data(node);

	if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
		return;
	}
	while (child != NULL) {
		drop_node_caches(child);
		if (dom_node_get_next_sibling(child, &next) != DOM_NO_ERR) {
			next = NULL;
		}
		dom_node_unref(child);
		child = next;
	}
}


/**
 * A tényleges újraépítés indítása (a debounce lejárta után fut).
 */
static void html_rexos_relayout_run(void *pw)
{
	html_content *htmlc = pw;
	dom_exception exc;
	dom_node *html;
	nserror error;

	if (!relayout_allowed(htmlc)) {
		return;
	}

	/* Egyszerre csak egy újraépítés. */
	if (swap.owner != NULL) {
		return;
	}

	/* Ne indítsunk újat, amíg az előző kör képei töltődnek: a régi és az új
	 * objektumlista keveredése fölöslegesen bonyolult lenne. */
	if (htmlc->base.active > 0) {
		guit->misc->schedule(RELAYOUT_SETTLE_MS,
				     html_rexos_relayout_run, htmlc);
		return;
	}

	exc = dom_document_get_document_element(htmlc->document, (void *) &html);
	if (exc != DOM_NO_ERR || html == NULL) {
		NSLOG(netsurf, WARNING, "nincs html elem a DOM-ban");
		return;
	}

	/* Az eddigi állapot félretétele. A `layout` SZÁNDÉKOSAN marad a régi
	 * fán: amíg az új épül, a böngésző abból rajzol. */
	swap.owner           = htmlc;
	swap.dirty           = false;
	swap.old_bctx        = htmlc->bctx;
	swap.old_layout      = htmlc->layout;
	swap.old_forms       = htmlc->forms;
	swap.old_objects     = htmlc->object_list;
	swap.old_num_objects = htmlc->num_objects;

	htmlc->forms       = NULL;
	htmlc->object_list = NULL;
	htmlc->num_objects = 0;
	htmlc->bctx        = NULL;   /* a dom_to_box újat hoz létre */

	drop_node_caches((dom_node *) htmlc->document);

	error = dom_to_box(html, htmlc, html_rexos_rebuild_done,
			   &htmlc->box_conversion_context);
	if (error != NSERROR_OK) {
		NSLOG(netsurf, WARNING, "dobozfa-ujraepites nem indult el");
		restore_old(htmlc);
	}

	dom_node_unref(html);
}
