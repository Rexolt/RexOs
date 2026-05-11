# RexOS Asztali Környezet (GUI) Grafikus Redesign Terv

A jelenlegi asztali környezet (Desktop v2) már funkcionálisan remekül működik (ablakkezelés, fókusz, minimalista design), de a megjelenése inkább a '90-es éveket vagy a korai retro rendszereket idézi. 

A "prémium" és modern felhasználói élmény elérése érdekében az alábbi vizuális és technológiai frissítésekre van szükség.

---

## 1. Fázis: Modern Grafikai Alapok (Renderer Upgrade)

A jelenlegi GUI csak tömör (solid) téglalapokat és pixeleket rajzol. A modern dizájnhoz fejlettebb renderelő funkciók kellenek.

### 1.1. Alpha Blending (Átlátszóság)
- Képpontok keverése (Alpha Channel: ARGB 8888 támogatás).
- Cél: A tálca (Taskbar) enyhén átlátszó, "Glassmorphism" vagy "Aero" stílusú lehessen, hogy a háttérkép elmosódva átlátszódjon rajta.
- Cél: Valós, elmosott vetett árnyékok (Drop Shadow) az ablakok körül a jelenlegi szimpla fekete/sötétkék keret helyett.

### 1.2. Lekerekített Sarkok (Rounded Corners)
- A `bb_fill_rect` mellé egy `bb_fill_rounded_rect` függvény megírása (Bresenham-körrajzoló algoritmussal vagy egyszerűbb távolság-alapú maszkkal).
- Az ablakok, gombok és a start menü sarkainak lekerekítése modern, barátságos megjelenést ad (pl. macOS vagy Windows 11 stílus).

### 1.3. Színátmenetek (Gradients)
- Lineáris és radiális színátmenetek támogatása gombokon és az ablakok fejlécén, hogy megszűnjön a "lapos" (flat) 2D hatás, és térbelibb, prémium érzetet adjon.

---

## 2. Fázis: Tipográfia és Ikonok (Assets)

A vizuális minőséget leginkább a betűtípus és a képek határozzák meg.

### 2.1. Anti-Aliased (Élsimított) Betűtípusok
- A jelenlegi fix 5x7-es bitmap betűtípus cseréje.
- **Megoldás:** Egy egyszerű (pl. PSF vagy BDF formátumú) TrueType-hoz hasonló vektoros vagy már élsimított bitmap font beolvasása.
- Cél: Tiszta, olvasható, modern tipográfia (pl. Inter vagy Roboto stílusú fontok), több méretben (kis szöveg az ikonoknak, nagy vastag a címeknek és az órának).

### 2.2. Képi Erőforrások (Wallpapers & Icons)
- **Háttérkép:** Mivel már van VFS és lemezolvasás, egy nagy felbontású `.bmp` háttérkép betöltése a memóriába és kirajzolása a procedurális gradient/csillag háttér helyett.
- **Ikonok:** A színes téglalapok helyett apró, 32x32-es ikonfájlok betöltése. Színes, modern ikonok az asztalon és a fájlkezelőben.

---

## 3. Fázis: Felhasználói Élmény (UX) és Animációk

Egy modern felület nem csak szép, de "él" is.

### 3.1. Mikro-animációk
- Amikor az egeret egy asztali ikon fölé vagy egy gomb fölé húzod, az ne egyből váltson színt (0 frame alatt), hanem egy rövid, 100-200ms-os átmenettel világosodjon ki (Fade-in).
- A Start menü "felcsúszva" jelenjen meg.
- Az ablakok megnyitáskor egy gyors nagyító animációval (Scale-up) ugorjanak a képernyőre.

### 3.2. Újragondolt Színtéma (Color Palette)
- Egy sokkal kifinomultabb, modern Dark Mode paletta:
  - Háttér: Mélykék / Sötétszürke (pl. `#0F172A`)
  - Ablakok: Fél-átlátszó sötétkék (pl. `#1E293B` 80% opacitással)
  - Kiemelés (Accent): Élénk neonkék vagy lila (pl. `#8B5CF6` vagy `#3B82F6`)
  - Szöveg: Teljesen fehér helyett kellemes szürkésfehér (`#F1F5F9`) a jobb olvashatóságért.

---

## 4. Fázis: Optimalizáció

A sok grafikai effekt komoly CPU terhelést okozhat, amíg nincs hardveres (GPU) gyorsítás.
- **Dirty Rectangles (Csak a változott területek frissítése):** A `flush_all()` helyett csak azokat a képernyő-részeket szabad a framebufferbe másolni, amik ténylegesen megváltoztak (pl. ahol a kurzor mozog, vagy ahol az óra ketyeg). Ez drasztikusan felgyorsítja az OS-t és megszünteti az egér "laggolását" a bonyolult árnyékok esetén.
- **VSync (Függőleges szinkronizálás):** A képfrissítés szinkronizálása a monitorhoz (tearing megelőzése).

---

### Mivel kezdjük a kódolást?
A leglátványosabb ugrást az **Új Színtéma**, az **Átlátszóság (Alpha blending) bevezetése az árnyékokhoz**, és a **Dirty Rectangle renderelés** megírása hozza el. Utána pedig betölthetjük a legelső `.bmp` háttérképünket!
