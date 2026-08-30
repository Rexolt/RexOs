/* ============================================================================
 *  RexOS - vektoros path rajzolás a NetSurf framebuffer frontendjéhez
 *
 *  A framebuffer frontend `path` plottere az eredeti forrásban csonk volt
 *  ("path unimplemented"), ezért a NetSurf EGYETLEN SVG-t sem jelenített meg:
 *  a libsvgtiny szépen felparsolta a rajzot, aztán a shape-eket a semmibe
 *  rajzolta. Ez a fájl pótolja a hiányzó raszterizálót.
 *
 *  Menete:
 *    1. a NetSurf path-parancsait (MOVE / LINE / BEZIER / CLOSE) végigjárjuk,
 *    2. minden pontra alkalmazzuk a kapott affin transzformációt,
 *    3. a köbös Bézier-íveket szakaszokká lapítjuk,
 *    4. a poligont NONZERO WINDING szabállyal kitöltjük (ez az SVG alapértel-
 *       mezése, és ettől lesznek jók a több-alútvonalas alakzatok, pl. a
 *       lyukas "O" betű vagy egy fánk-alakú logó),
 *    5. végül a körvonalat kihúzzuk.
 *
 *  Miért #include-dal kerül be a framebuffer.c-be, és nem külön fordítási
 *  egységként? Mert a rajzoló hívások a framebuffer.c fájl-szintű statikus
 *  `nsfb` mutatóját használják.
 * ========================================================================== */

/* Egy köbös ív maximum/minimum hány szakaszra bomoljon. */
#define RXP_SEG_MIN 3
#define RXP_SEG_MAX 24

typedef struct { float x, y; } rxp_pt;

/* Egy alútvonal (subpath): a pontlistán belüli tartomány. */
typedef struct { unsigned start, count; bool closed; } rxp_sub;

typedef struct {
	rxp_pt   *pt;
	unsigned  npt, ptcap;
	rxp_sub  *sub;
	unsigned  nsub, subcap;
	bool      oom;
} rxp_poly;

static float rxp_absf(float v) { return v < 0.0f ? -v : v; }

static void rxp_poly_free(rxp_poly *g)
{
	free(g->pt);
	free(g->sub);
	g->pt = NULL; g->sub = NULL;
	g->npt = g->ptcap = g->nsub = g->subcap = 0;
}

static bool rxp_add_pt(rxp_poly *g, float x, float y)
{
	if (g->npt == g->ptcap) {
		unsigned cap = g->ptcap ? g->ptcap * 2 : 64;
		rxp_pt *np = realloc(g->pt, cap * sizeof(*np));
		if (np == NULL) { g->oom = true; return false; }
		g->pt = np; g->ptcap = cap;
	}
	g->pt[g->npt].x = x;
	g->pt[g->npt].y = y;
	g->npt++;
	if (g->nsub > 0) g->sub[g->nsub - 1].count++;
	return true;
}

static bool rxp_new_sub(rxp_poly *g)
{
	if (g->nsub == g->subcap) {
		unsigned cap = g->subcap ? g->subcap * 2 : 8;
		rxp_sub *ns = realloc(g->sub, cap * sizeof(*ns));
		if (ns == NULL) { g->oom = true; return false; }
		g->sub = ns; g->subcap = cap;
	}
	g->sub[g->nsub].start = g->npt;
	g->sub[g->nsub].count = 0;
	g->sub[g->nsub].closed = false;
	g->nsub++;
	return true;
}

/* Köbös Bézier lapítása. A szegmensszámot a kontrollpoligon hosszából
 * becsüljük, hogy a kis ikonok se legyenek szögletesek, a nagy ívek meg ne
 * termeljenek fölösleges pontot. */
static bool rxp_bezier(rxp_poly *g, rxp_pt p0, rxp_pt c1, rxp_pt c2, rxp_pt p1)
{
	float d = rxp_absf(c1.x - p0.x) + rxp_absf(c1.y - p0.y) +
	          rxp_absf(c2.x - c1.x) + rxp_absf(c2.y - c1.y) +
	          rxp_absf(p1.x - c2.x) + rxp_absf(p1.y - c2.y);
	int seg = (int)(d / 3.0f);
	int i;

	if (seg < RXP_SEG_MIN) seg = RXP_SEG_MIN;
	if (seg > RXP_SEG_MAX) seg = RXP_SEG_MAX;

	for (i = 1; i <= seg; i++) {
		float t  = (float)i / (float)seg;
		float mt = 1.0f - t;
		float a = mt * mt * mt;
		float b = 3.0f * mt * mt * t;
		float c = 3.0f * mt * t * t;
		float e = t * t * t;
		if (!rxp_add_pt(g,
		                a * p0.x + b * c1.x + c * c2.x + e * p1.x,
		                a * p0.y + b * c1.y + c * c2.y + e * p1.y)) {
			return false;
		}
	}
	return true;
}

/* Egy metszéspont a pásztázósoron: hol, és milyen irányban lépjük át az élt. */
typedef struct { float x; int dir; } rxp_cross;

/* Kitöltés nonzero winding szabállyal. */
static void rxp_fill(const rxp_poly *g, nsfb_colour_t col)
{
	rxp_cross *cr;
	unsigned max_cross = g->npt + g->nsub; /* élek száma felső korlát */
	float fymin, fymax;
	int y, ymin, ymax;
	unsigned s, i;

	if (g->npt < 3 || max_cross == 0) return;

	cr = malloc(max_cross * sizeof(*cr));
	if (cr == NULL) return;

	fymin = fymax = g->pt[0].y;
	for (i = 1; i < g->npt; i++) {
		if (g->pt[i].y < fymin) fymin = g->pt[i].y;
		if (g->pt[i].y > fymax) fymax = g->pt[i].y;
	}
	ymin = (int)fymin;
	ymax = (int)fymax + 1;

	for (y = ymin; y <= ymax; y++) {
		float sy = (float)y + 0.5f;
		unsigned nc = 0;

		/* metszéspontok gyűjtése: minden alútvonal minden éle, a záró
		 * éllel együtt (kitöltésnél az SVG mindig zártnak tekinti) */
		for (s = 0; s < g->nsub; s++) {
			unsigned st = g->sub[s].start;
			unsigned cn = g->sub[s].count;
			unsigned k;
			if (cn < 2) continue;
			for (k = 0; k < cn; k++) {
				const rxp_pt *a = &g->pt[st + k];
				const rxp_pt *b = &g->pt[st + ((k + 1) % cn)];
				float t;
				int dir;
				/* félig nyitott intervallum: minden metszés
				 * pontosan egyszer számít */
				if ((a->y <= sy) == (b->y <= sy)) continue;
				dir = (b->y > a->y) ? 1 : -1;
				t = (sy - a->y) / (b->y - a->y);
				if (nc < max_cross) {
					cr[nc].x = a->x + t * (b->x - a->x);
					cr[nc].dir = dir;
					nc++;
				}
			}
		}
		if (nc < 2) continue;

		/* rendezés x szerint (kevés elem, beszúrásos rendezés elég) */
		for (i = 1; i < nc; i++) {
			rxp_cross key = cr[i];
			unsigned j = i;
			while (j > 0 && cr[j - 1].x > key.x) { cr[j] = cr[j - 1]; j--; }
			cr[j] = key;
		}

		{
			int wind = 0;
			float span_x0 = 0.0f;
			for (i = 0; i < nc; i++) {
				int prev = wind;
				wind += cr[i].dir;
				if (prev == 0 && wind != 0) {
					span_x0 = cr[i].x;
				} else if (prev != 0 && wind == 0) {
					nsfb_bbox_t r;
					r.x0 = (int)(span_x0 + 0.5f);
					r.x1 = (int)(cr[i].x + 0.5f);
					r.y0 = y;
					r.y1 = y + 1;
					if (r.x1 <= r.x0) r.x1 = r.x0 + 1;
					nsfb_plot_rectangle_fill(nsfb, &r, col);
				}
			}
		}
	}

	free(cr);
}

/* Körvonal kihúzása alútvonalanként. */
static void rxp_stroke(const rxp_poly *g, nsfb_colour_t col, int width)
{
	nsfb_plot_pen_t pen;
	nsfb_point_t *ip;
	unsigned s, k, maxn = 0;

	for (s = 0; s < g->nsub; s++) {
		if (g->sub[s].count + 1 > maxn) maxn = g->sub[s].count + 1;
	}
	if (maxn < 2) return;

	ip = malloc(maxn * sizeof(*ip));
	if (ip == NULL) return;

	pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
	pen.stroke_colour = col;
	pen.stroke_width = width < 1 ? 1 : width;
	pen.stroke_pattern = 0;
	pen.fill_type = NFSB_PLOT_OPTYPE_NONE;
	pen.fill_colour = 0;

	for (s = 0; s < g->nsub; s++) {
		unsigned st = g->sub[s].start;
		unsigned cn = g->sub[s].count;
		unsigned n = 0;
		if (cn < 2) continue;
		for (k = 0; k < cn; k++) {
			ip[n].x = (int)(g->pt[st + k].x + 0.5f);
			ip[n].y = (int)(g->pt[st + k].y + 0.5f);
			n++;
		}
		if (g->sub[s].closed) {
			ip[n] = ip[0];
			n++;
		}
		nsfb_plot_polylines(nsfb, (int)n, ip, &pen);
	}

	free(ip);
}

static nserror
framebuffer_plot_path(const struct redraw_context *ctx,
		const plot_style_t *pstyle,
		const float *p,
		unsigned int n,
		const float transform[6])
{
	rxp_poly g;
	rxp_pt cur = { 0.0f, 0.0f };
	unsigned int i;
	nserror res = NSERROR_OK;

	(void)ctx;

	if (n == 0) return NSERROR_OK;
	if (p[0] != PLOTTER_PATH_MOVE) {
		NSLOG(netsurf, INFO, "Path does not start with move");
		return NSERROR_INVALID;
	}

	memset(&g, 0, sizeof(g));

/* pont transzformálása: x' = a*x + c*y + e ; y' = b*x + d*y + f */
#define RXP_TX(px, py) (transform[0] * (px) + transform[2] * (py) + transform[4])
#define RXP_TY(px, py) (transform[1] * (px) + transform[3] * (py) + transform[5])

	for (i = 0; i < n; ) {
		if (p[i] == PLOTTER_PATH_MOVE) {
			if (i + 2 >= n) { res = NSERROR_INVALID; break; }
			cur.x = RXP_TX(p[i + 1], p[i + 2]);
			cur.y = RXP_TY(p[i + 1], p[i + 2]);
			if (!rxp_new_sub(&g) || !rxp_add_pt(&g, cur.x, cur.y)) break;
			i += 3;
		} else if (p[i] == PLOTTER_PATH_LINE) {
			if (i + 2 >= n) { res = NSERROR_INVALID; break; }
			cur.x = RXP_TX(p[i + 1], p[i + 2]);
			cur.y = RXP_TY(p[i + 1], p[i + 2]);
			if (!rxp_add_pt(&g, cur.x, cur.y)) break;
			i += 3;
		} else if (p[i] == PLOTTER_PATH_BEZIER) {
			rxp_pt c1, c2, e;
			if (i + 6 >= n) { res = NSERROR_INVALID; break; }
			c1.x = RXP_TX(p[i + 1], p[i + 2]);
			c1.y = RXP_TY(p[i + 1], p[i + 2]);
			c2.x = RXP_TX(p[i + 3], p[i + 4]);
			c2.y = RXP_TY(p[i + 3], p[i + 4]);
			e.x  = RXP_TX(p[i + 5], p[i + 6]);
			e.y  = RXP_TY(p[i + 5], p[i + 6]);
			if (g.nsub == 0 && !rxp_new_sub(&g)) break;
			if (!rxp_bezier(&g, cur, c1, c2, e)) break;
			cur = e;
			i += 7;
		} else if (p[i] == PLOTTER_PATH_CLOSE) {
			if (g.nsub > 0) g.sub[g.nsub - 1].closed = true;
			i++;
		} else {
			NSLOG(netsurf, INFO, "bad path command %f", p[i]);
			res = NSERROR_INVALID;
			break;
		}
	}

#undef RXP_TX
#undef RXP_TY

	if (g.oom) {
		rxp_poly_free(&g);
		return NSERROR_NOMEM;
	}
	if (res != NSERROR_OK) {
		rxp_poly_free(&g);
		return res;
	}

	if (pstyle->fill_colour != NS_TRANSPARENT) {
		rxp_fill(&g, pstyle->fill_colour);
	}
	if (pstyle->stroke_colour != NS_TRANSPARENT) {
		rxp_stroke(&g, pstyle->stroke_colour,
		           plot_style_fixed_to_int(pstyle->stroke_width));
	}

	rxp_poly_free(&g);
	return NSERROR_OK;
}
