/* =============================================================================
 *  RexOS - Minimal float math for stb_truetype (no libm)
 *  Pontosság: tipikus glyph-rasterizációhoz elegendő (kb. 6 számjegy).
 * ========================================================================== */

double m_floor(double x) {
    long long i = (long long)x;
    double r = (double)i;
    if (x < 0.0 && r != x) r -= 1.0;
    return r;
}

double m_ceil(double x) {
    long long i = (long long)x;
    double r = (double)i;
    if (x > 0.0 && r != x) r += 1.0;
    return r;
}

double m_fabs(double x) { return x < 0.0 ? -x : x; }

double m_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    /* Newton-Raphson 8 iterációval. */
    double r = x;
    for (int i = 0; i < 16; i++) {
        double nr = 0.5 * (r + x / r);
        if (m_fabs(nr - r) < 1e-9 * r) { r = nr; break; }
        r = nr;
    }
    return r;
}

double m_fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    double q = x / y;
    double i = m_floor(q);
    return x - i * y;
}

/* Range reduction: csökkentsük cos argumentumát [-pi, pi]-be. */
#define M_PI_D 3.14159265358979323846
#define M_TAU_D (2.0 * M_PI_D)

double m_cos(double x) {
    /* Reduce to [-pi, pi] */
    x = m_fmod(x, M_TAU_D);
    if (x > M_PI_D)  x -= M_TAU_D;
    if (x < -M_PI_D) x += M_TAU_D;

    /* Taylor series cos(x) = 1 - x^2/2! + x^4/4! - x^6/6! + x^8/8! - x^10/10! */
    double x2 = x * x;
    double term = 1.0;
    double sum = 1.0;
    /* i=2,4,6,8,10 -> -x2/2, +x2*x2/(2*3*4), ... */
    /* Iteratív: term *= -x2 / ((2i-1)*(2i)) */
    for (int n = 1; n <= 8; n++) {
        term *= -x2 / (double)((2*n - 1) * (2*n));
        sum += term;
    }
    return sum;
}

double m_sin(double x) { return m_cos(x - M_PI_D / 2.0); }

double m_acos(double x) {
    /* acos clamping */
    if (x >= 1.0)  return 0.0;
    if (x <= -1.0) return M_PI_D;
    /* Newton iteration: solve cos(y) = x for y in [0, pi]. */
    double y = M_PI_D / 2.0 - x; /* kezdőbecslés (kb. linear approx 0 körül) */
    if (y < 0.0) y = 0.0;
    if (y > M_PI_D) y = M_PI_D;
    for (int i = 0; i < 24; i++) {
        double s = m_sin(y);
        if (m_fabs(s) < 1e-12) break;
        double dy = (m_cos(y) - x) / s;
        y += dy;
        if (y < 0.0) y = 0.0;
        if (y > M_PI_D) y = M_PI_D;
        if (m_fabs(dy) < 1e-9) break;
    }
    return y;
}

/* Egyszerű exp/log Taylor sorral, csak pow-hoz. */
static double m_exp(double x) {
    /* e^x = e^(k * ln2) * e^r = 2^k * e^r, ahol r in [-ln2/2, ln2/2] körül. */
    if (x > 700.0) return 1e300;
    if (x < -700.0) return 0.0;
    /* Range reduction nélkül, durván: */
    double sum = 1.0;
    double term = 1.0;
    for (int i = 1; i < 30; i++) {
        term *= x / (double)i;
        sum += term;
        if (m_fabs(term) < 1e-14 * m_fabs(sum)) break;
    }
    return sum;
}

static double m_ln(double x) {
    if (x <= 0.0) return -1e300;
    /* x = 2^k * m, m in [1,2). Find k. */
    int k = 0;
    while (x >= 2.0) { x *= 0.5; k++; }
    while (x < 1.0)  { x *= 2.0; k--; }
    /* Now x in [1,2). y = (x-1)/(x+1). ln(x) = 2 * (y + y^3/3 + y^5/5 + ...) */
    double y = (x - 1.0) / (x + 1.0);
    double y2 = y * y;
    double sum = 0.0;
    double term = y;
    for (int i = 0; i < 40; i++) {
        sum += term / (double)(2*i + 1);
        term *= y2;
        if (m_fabs(term) < 1e-15) break;
    }
    return 2.0 * sum + (double)k * 0.6931471805599453; /* ln(2) */
}

double m_pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    /* Egész kitevő: gyors útvonal */
    long long iy = (long long)y;
    if ((double)iy == y && iy > -64 && iy < 64) {
        double r = 1.0;
        double base = x;
        long long n = iy < 0 ? -iy : iy;
        while (n) {
            if (n & 1) r *= base;
            base *= base;
            n >>= 1;
        }
        return iy < 0 ? 1.0 / r : r;
    }
    /* Általános: exp(y * ln(x)). x>0 kötelező. */
    if (x < 0.0) return 0.0;
    return m_exp(y * m_ln(x));
}
