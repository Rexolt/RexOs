/* ============================================================================
 *  RexOS - dobozfa újraépítése DOM-változás után (NetSurf kiegészítés)
 * ========================================================================== */
#ifndef NETSURF_HTML_RELAYOUT_REXOS_H
#define NETSURF_HTML_RELAYOUT_REXOS_H

struct html_content;

/**
 * Jelzés, hogy a DOM megváltozott a betöltés BEFEJEZŐDÉSE után.
 *
 * Csak ütemez: a tényleges újraépítés késleltetve fut, hogy egy JS-blokk
 * sok apró módosítása egyetlen layoutba fusson össze.
 */
void html_rexos_dom_mutated(struct html_content *htmlc);

/**
 * Függőben lévő újraépítés visszavonása (a tartalom megszűnésekor kell).
 */
void html_rexos_relayout_cancel(struct html_content *htmlc);

#endif
