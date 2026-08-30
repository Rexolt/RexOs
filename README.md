# RexOS

x86_64 operációs rendszer a nulláról: saját kernel, saját userspace, saját
grafikus felület, saját TCP/IP verem, és egy portolt NetSurf böngésző, ami
valódi weboldalakat tölt be HTTPS-en.

```
   kernel:    11 000 sor C + asm
   userspace: 17 000 sor C (a portolt könyvtárak nélkül)
   bootolható: BIOS és UEFI, QEMU-ban és valódi gépen
```

A projekt tanulóprojekt. A cél nem egy termék, hanem hogy a hardver és a
szoftver közötti minden réteg kézzelfoghatóvá váljon: lapozótáblák, megszakítás
kezelés, ütemezés, DMA, TCP állapotgép, és végül egy böngésző, ami mindezen
keresztül képet rajzol a képernyőre. A fejlesztés AI-asszisztált, iteratív
módon zajlik.

---

## 1. Gyorsindítás

Előfeltételek: `nasm`, `xorriso`, `qemu-system-x86_64`, `mtools`,
GNU make, és egy x86_64 host GCC.

```bash
make            # kernel + userspace + initrd + rexos.iso
make run        # q35 chipset, AHCI SATA, e1000 hálózat
```

További futtatási módok:

| Cél | Mit ad |
|---|---|
| `make run` | q35 + AHCI + e1000 (alapértelmezett) |
| `make run-legacy` | `-M pc`, IDE PIO, régi gépek szimulálása |
| `make run-uefi` | OVMF firmware, UEFI boot út |
| `make run-usb` | xHCI controller + USB billentyűzet és egér |

A böngésző a desktop Start menüjéből indul (NetSurf sor). Hálózat kell hozzá,
a QEMU user-mode NAT alapból megfelelő.

---

## 2. Rendszerkép

```
  +---------------------------------------------------------------+
  |  Ring 3 (felhasználói mód, saját címtér taszkonként)           |
  |                                                               |
  |   desktop.elf              netsurf.elf                        |
  |   ablakkezelő, tálca       NetSurf 3.11 + libcss + libdom      |
  |   9 beépített app          + Duktape JS + BearSSL TLS          |
  |        |                          |                            |
  |   user/libc.c (kicsi)      newlib 4.4 + user/newlib-glue       |
  +--------|--------------------------|----------------------------+
           |     SYSCALL / SYSRET     |
  +--------v--------------------------v----------------------------+
  |  Ring 0 (kernel)                                              |
  |                                                               |
  |  sched      mm            fs           net        drivers      |
  |  round      pmm bitmap    vfs          eth arp    pit  ps2     |
  |  robin      vmm 4 szint   tarfs        ipv4 icmp  xhci ahci    |
  |  preempt    kheap         fat32 rw     udp  dhcp  ata  nvme    |
  |  fxsave     brk           elf loader   dns  tcp   pci  e1000   |
  |                           block réteg              fb   rtc    |
  +---------------------------------------------------------------+
           |
  +--------v------------------------------------------------------+
  |  Limine bootloader (BIOS vagy UEFI)                            |
  +---------------------------------------------------------------+
```

---

## 3. Boot

A Limine 64 bit módban, saját ideiglenes lapozótáblákkal adja át a vezérlést,
és átadja a memóriatérképet, a framebuffer címét és az initrd modult.

```
  Limine
    |
    | kmain(void)
    v
  serial init        soros port 0x3F8, minden log ide megy
    |
  console init       framebuffer szöveges konzol (8x16 font)
    |
  gdt / idt init     kernel + user szegmensek, 256 IDT bejegyzés
    |
  pmm init           memóriatérkép -> bitmap allokátor
    |
  vmm init           SAJÁT PML4 építése, majd CR3 csere
    |                  - HHDM: 0..4 GB fizikai -> 0xffff8000_00000000, 2 MB lapokkal
    |                  - kernel: 0xffffffff_80000000
    |
  kheap init         1 MB kernel heap 0xffff9000_00000000-tól
    |
  irq / pic init     PIC remap, IRQ0 PIT 100 Hz
    |
  keyboard, mouse    PS/2 IRQ1 és IRQ12
    |
  vfs + tarfs        initrd.tar beolvasása memóriából, mount a / alá
    |
  pci scan           0xCF8 / 0xCFC config space, minden eszköz
    |
  e1000 + DHCP       IP, gateway, DNS megszerzése
    |
  nvme, ahci, ata    tárolók keresése, block device regisztráció
    |
  xhci               USB controller, port enumeráció, HID eszközök
    |
  fat32 mount        /mnt
    |
  sched init         kmain taszkká alakítása, reaper szál indítása
    |
  spawn desktop.elf
    |
  kmain exit         innentől az ütemező viszi a rendszert
```

---

## 4. Memória

### 4.1 Címtér

```
  0x0000_0000_0000_0000  +----------------------------------+
                         |  felhasználói terület            |
  0x0000_0000_0040_0000  |  ELF kód és adat (link cím)      |
                         |  heap (brk, felfelé nő)          |
  0x0000_0000_8000_0000  |  brk felső korlát (2 GB)         |
                         |                                  |
  0x0000_0000_A000_0000  |  framebuffer user mapping        |
                         |                                  |
  0x0000_7FFF_FF7C_0000  |  user stack alja (256 KB)        |
  0x0000_7FFF_FF80_0000  |  user stack teteje               |
                         +----------------------------------+
                         |  nem kanonikus rés               |
  0xFFFF_8000_0000_0000  +----------------------------------+
                         |  HHDM: teljes fizikai RAM        |
  0xFFFF_9000_0000_0000  |  kernel heap (1 MB)              |
  0xFFFF_FFFF_8000_0000  |  kernel kód és adat              |
                         +----------------------------------+
```

A user stack azért 256 KB, mert a libcss és a NetSurf layout kódja mélyen
rekurzív. A brk felső korlátja azért 2 GB, mert fölötte kezdődik a
framebuffer mappingje.

### 4.2 PMM

Bitmap allokátor, 4 KB-os frame-enként egy bit. A bitmap maga is a fizikai
RAM-ban ül, az első elég nagy szabad területen. `pmm_alloc_frame()` az első
szabad bitet keresi, `pmm_alloc_frames(n)` összefüggő tartományt.

### 4.3 VMM

Négyszintű lapozás: PML4 -> PDPT -> PD -> PT. Minden felhasználói taszknak
saját PML4-e van, a kontextusváltás a CR3 írásával jár.

Egy fontos részlet, ami sokáig rejtett hibát okozott: a lapleképezés után
**TLB-t kell üríteni, ha az érintett címtér épp be van töltve a CR3-ba**. A
`vmm_map_page_pml4()` korábban csak akkor hívott `invlpg`-t, ha a kernel
PML4-be írt. A `sys_brk`, a `sys_get_fb` és a user stack mappelése viszont az
éppen futó taszk tábláit írja, tehát ezek kimaradtak. Most a függvény
összehasonlítja a cél PML4-et a CR3 tartalmával.

### 4.4 A user heap (brk)

```
  sbrk(n)  a newlib-ből
     |
     v
  sys_brk(new_brk)
     |
     +-- new_brk == 0        -> az aktuális brk visszaadása
     |
     +-- lefelé (trim)       -> csak a brk_current csökken,
     |                          a lapok mappelve MARADNAK
     |
     +-- felfelé             -> minden hiányzó lapra:
                                  ha MÁR mappelt  -> kihagyás
                                  különben        -> pmm_alloc_frame
                                                     vmm_map_page_pml4
                                                     nullázás
```

A "ha már mappelt, hagyd ki" ág nem kozmetika. A newlib `malloc_trim()`
negatív `sbrk`-t hív, amikor visszaadja a heap tetejét. Ilyenkor a brk lejjebb
megy, de a lapok mappelve maradnak. Ha a következő növekedéskor a kernel vakon
új fizikai frame-eket rakna a régiek helyére, akkor **élő malloc blokkok
csendben kinullázódnának**. Pontosan ez okozta a böngésző véletlenszerű
összeomlásait: a szabadlista sérült, és a következő `malloc` a `unlink()`
makróban szállt el. A `make run-test-heaptest` ennek a hibának az őre.

---

## 5. Ütemezés

Round robin, preemptív, 100 Hz PIT alapon. Kooperatív pontok is vannak
(`task_yield()`, blokkoló `waitpid`).

```
  IRQ0 (PIT)                       int 0xFE (explicit yield)
      |                                   |
      +-----------------+-----------------+
                        |
                 minden regiszter push
                        |
                 sched_maybe_switch(rsp)
                        |
            +-----------+-----------+
            | nincs váltás          | van váltás
            |                       |
            | rsp marad             | fxsave  -> régi taszk fpu_state
            |                       | rsp mentése a TCB-be
            |                       | CR3 = új taszk PML4
            |                       | fxrstor <- új taszk fpu_state
            |                       | rsp = új taszk mentett rsp
            +-----------+-----------+
                        |
                 minden regiszter pop
                        |
                      iretq
```

Az FPU/SSE állapotot `fxsave` / `fxrstor` menti. Enélkül a NetSurf lebegőpontos
számításai összekeveredtek volna a desktop rajzolásával.

Taszk életciklus:

```
  spawn("app.elf")
      |
      v
  task_create -> READY        16 KB kernel stack, saját PML4
      |
      v
  user_task_launcher          ELF betöltés, brk_start beállítás,
      |                       256 KB user stack mappelése,
      |                       hamis visszatérési cím a stack tetejére
      v
  RUNNING (Ring 3)
      |
      +-- sys_exit  vagy  kezelhetetlen kivétel Ring 3-ból
      |
      v
  DEAD  -> reaper szál felszabadítja a stacket és a lapokat
```

Ha egy felhasználói program hibázik (page fault, GP fault), a kernel **nem áll
meg**. Az `isr_common_c_handler` megnézi a `cs` alsó két bitjét, és ha Ring
3-ból jött a hiba, csak azt a taszkot lövi ki. A dump kiírja a CR2-t és
dekódolja a hibakódot is, enélkül egy page fault jelentése használhatatlan.

---

## 6. Rendszerhívások

`syscall` / `sysret` utasításpár, nem `int 0x80`. Az MSR-ek (STAR, LSTAR,
SFMASK) a boot elején állnak be.

```
  Ring 3                     Ring 0
  ------                     ------
  rax = szám
  rdi, rsi, rdx = arg1..3
  syscall  ------------>  syscall_entry (asm)
                            swapgs
                            user rsp mentése a TCB-be
                            kernel stack betöltése
                            regiszterek mentése
                            syscall_handler_c(nr, a1, a2, a3)
                              |
                              +-- uptr_ok(): minden user mutató
                              |   ellenőrzése a kanonikus user
                              |   tartományra
                              |
                            regiszterek visszaállítása
                            user rsp visszaállítása
                            swapgs
  <-------------------    sysret
  rax = eredmény
```

| Szám | Név | Leírás |
|---|---|---|
| 0 | write | stdout vagy fd, VFS-en át |
| 1 | exit | taszk befejezése |
| 2 | yield | CPU átadása |
| 3 | read | stdin vagy fd |
| 4 | get_fb | framebuffer mappelése 0xA0000000-ra, méretek visszaadása |
| 5 | spawn | ELF indítása új taszkként, pid visszaadása |
| 6 | open | útvonal megnyitása, fd |
| 7 | getdents | könyvtár olvasása |
| 8 | waitpid | várakozás gyerektaszkra |
| 9 | brk | user heap méretezése |
| 10 | ticks | PIT tick számláló |
| 11 | kbd_poll | nem blokkoló billentyű olvasás |
| 12 | mouse | x, y, gombok |
| 13 | block_count | tárolók száma |
| 14 | block_info | név, szektorszám, szektorméret |
| 15 | pci_count | PCI eszközök száma |
| 16 | pci_info | bus, dev, func, vendor, device, class |
| 17 | mkdir | könyvtár létrehozása |
| 18 | unlink | fájl törlése |
| 19 | close | fd lezárása |
| 20 | seek | fájlpozíció |
| 21 | net_connect | TCP kapcsolat hosztnévre vagy IP-re |
| 22 | net_send | küldés socketre |
| 23 | net_recv | fogadás socketről |
| 24 | time | RTC dátum és idő |

---

## 7. Fájlrendszer

```
  felhasználói program
        |  open / read / write / getdents
        v
  +---------------------------------------------+
  |  VFS: vfs_node_t, mount tábla (max 4)       |
  |  vfs_lookup("/mnt/dir/file.txt")            |
  +----------------+----------------+-----------+
                   |                |
             +-----v-----+    +-----v---------+
             |  TarFS    |    |  FAT32        |
             |  /        |    |  /mnt         |
             |  csak     |    |  írás is      |
             |  olvasás  |    |  LFN, alkönyv |
             +-----+-----+    +-----+---------+
                   |                |
             initrd a          +----v-------------------+
             memóriában        |  block device réteg    |
                               |  name, read, write     |
                               +----+----+----+---------+
                                    |    |    |
                                  ahci ata  nvme
                                    |    |    |
                               +----v----v----v---------+
                               |  PCI enumeráció        |
                               +------------------------+
```

A tarfs lapos formátum, de a NetSurf erőforrásai `netsurf/valami.css` néven
kerülnek bele. Ezt a `vfs_lookup` egy pontos egyezésre visszaeső ága kezeli, és
a tarfs külön könyvtár-node-okat is épít.

---

## 8. Hálózat

```
  user: sys_net_connect / send / recv
        |
  +-----v-----------------------------------------+
  |  TCP: állapotgép, körkörös RX puffer (64 KB)  |
  |       in-order kézbesítés, ablakhirdetés      |
  +-----+-----------------------------------------+
  |  UDP  |  ICMP  |   DHCP kliens  |  DNS kliens |
  +-------+--------+----------------+-------------+
  |  IPv4: fejléc, checksum, next hop választás   |
  +-----+-----------------------------------------+
  |  ARP: cache + függő csomagok sora             |
  +-----+-----------------------------------------+
  |  Ethernet keretezés                           |
  +-----+-----------------------------------------+
  |  e1000 driver: RX/TX descriptor gyűrűk, IRQ11 |
  +-----------------------------------------------+
```

Az ARP-nél volt egy hiba, ami minden első kapcsolatot 3 másodperces
időtúllépésbe vitt: az `arp_cache_update` csak akkor küldte ki a várakozó
csomagokat, ha a bejegyzés **már létezett**. Új bejegyzésnél a sorba tett TCP
SYN sosem ment ki, retransmit pedig nincs. Javítva.

Ismert korlátok: nincs retransmit időzítő, nincs torlódásvezérlés, egy socket
egy kapcsolat, IPv6 nincs.

---

## 9. Grafikus felület

A `desktop.elf` egy sima felhasználói program. A kernel csak a framebuffert
adja oda neki (`sys_get_fb`), minden mást maga rajzol egy back bufferbe.

```
  user/desktop/
    main.c      fő ciklus, input összegyűjtés, frame ütemezés
    wm.c        ablakok: fókusz, húzás, minimalizálás, z-sorrend
    taskbar.c   tálca, Start menü, óra
    render.c    primitívek: kitöltés, gradiens, alpha blend, lekerekítés
    icons.c     vektoros ikonok
    apps.c      alkalmazások regisztere és logikája
    app_browser.c  a beépített egyszerű böngésző
    theme.h     színpaletta
```

Egy képkocka:

```
  input: sys_kbd_poll + sys_mouse
      |
  wm_input  -> ablakok kezelése
  tb_input  -> tálca, Start menü
      |
  háttér (gradiens) -> ablakok hátulról előre -> tálca -> egérkurzor
      |
  memcpy a back bufferből a framebufferbe
```

Alkalmazások: Browser (egyszerű beépített), NetSurf, Files, Terminal,
Calculator, Clock, System Monitor, Notes, About.

A NetSurf indítása `spawn` + `waitpid`: amíg fut, a desktop a `waitpid`-ben
blokkol, így nem rajzol bele a böngésző képébe. Kilépés után a desktop
következő képkockája visszaállítja az asztalt.

A szövegrajzolás TrueType alapú (stb_truetype, DejaVu Sans Mono), LRU
glyph gyorsítótárral és alpha blendinggel.

---

## 10. Webböngésző

Ez a projekt legnagyobb alrendszere. A NetSurf 3.11 fut a RexOS-en, valódi
HTTPS oldalakkal.

### 10.1 Miért NetSurf

Chromium és WebKit kizárva: pthread, mmap, dinamikus linker kell nekik, és a
kódméretük is kezelhetetlen lenne. A NetSurf C-ben van, statikusan linkelhető,
és a saját HTML/CSS motorját hozza (libdom, libcss, libhubbub). Cserébe a
webnek nagyjából a 2010-es szintjét adja.

### 10.2 A teljes lánc

```
  tools/build-toolchain.sh
      binutils 2.43.1 + gcc 14.2.0 (x86_64-elf) + newlib 4.4
      kimenet: toolchain/
          |
  tools/build-ports.sh
      zlib, libpng, libjpeg-turbo, freetype, expat, libwebp, BearSSL
      + RexOS runtime: crt0.o, librexos.a, rexos.specs
      innentől "x86_64-elf-gcc --specs=rexos.specs" önálló RexOS binárist linkel
          |
  tools/build-netsurf.sh
      netsurf-all-3.11, TARGET=framebuffer, HOST=x86_64-rexos
      curl és OpenSSL nélkül, Duktape JS-sel
      + a RexOS-specifikus forrásokat bemásolja és a forrásfát patcheli
          |
  Makefile
      netsurf.elf + /netsurf erőforrások bele az initrd.tar-ba
```

### 10.3 Ami a RexOS oldalán készült

```
  user/newlib-glue/     newlib alá: syscalls, dirent+scandir, mini iconv,
                        BSD socket shim, mmap = malloc + read, regex csonk,
                        crt0 (RSP igazítás, __libc_init_array)

  user/netsurf/
    fetch_rexos.c       http és https fetcher a NetSurf fetcher API-hoz:
                        keep-alive kapcsolat cache, chunked dekóder,
                        gzip kicsomagolás zlib-bel, átirányítás
    nsfb_surface_rexos.c  libnsfb surface: közvetlen framebuffer rajzolás,
                        billentyűzet és egér lekérdezés
    fb_plot_path.c      vektoros path raszterizáló (SVG)
    html_relayout_rexos.c  dobozfa újraépítés DOM-változásra
```

Plusz három patch a vendor forrásfára, mind idempotens szkript:

| Szkript | Mit javít |
|---|---|
| `tools/patch-fb-path.py` | a framebuffer frontend path plotterét kicseréli a sajátunkra |
| `tools/patch-html-relayout.py` | beköti a dobozfa újraépítést a DOM eseményekbe |
| `tools/patch-libdom-class.py` | libdom class gyorsítótár hiba |

### 10.4 Egy oldal betöltése

```
  NetSurf fetcher poll
      |
  fetch_rexos_process()
      |
      +-- kapcsolat: cache-ből (keep-alive) vagy új
      |        |
      |        +-- TLS?  BearSSL kliens, CA bundle a user/ca_bundle.c-ből
      |        |         entrópia: rdtsc + PIT tick keverék
      |        |
      |        +-- TCP:  sys_net_connect(host, port)
      |
      +-- kérés kiküldése, fejlécek beolvasása HDR_MAX-ig
      |
      +-- státusz és fejlécek feldolgozása
      |      Content-Length / Transfer-Encoding / Content-Encoding
      |      3xx esetén FETCH_REDIRECT
      |
      +-- body streamelés
      |      chunked dekóder
      |      gzip -> zlib z_stream -> FETCH_DATA darabokban
      |
      +-- kapcsolat vissza a cache-be, ha a keretezés pontosan kifutott
```

### 10.5 Képformátumok

Mind a kilenc működik: PNG (alfával is), JPEG, GIF (animált is), BMP, ICO,
SVG, WebP.

A WebP-hez libwebp portra volt szükség. Az SVG viszont nem a dekóderen múlt:
a libsvgtiny végig be volt fordítva, csak a framebuffer frontend `path`
plottere volt üres csonk `"path unimplemented"` üzenettel, tehát a rajz
elkészült, aztán a semmibe rajzolódott. A `user/netsurf/fb_plot_path.c` pótolja:

```
  NetSurf path parancsok            affin transzformáció
  MOVE x y                              x' = a*x + c*y + e
  LINE x y            ------>           y' = b*x + d*y + f
  BEZIER c1 c2 vég                          |
  CLOSE                                     v
                              köbös Bézier lapítása szakaszokra
                              (a szegmensszám a kontrollpoligon
                               hosszából, 3 és 24 között)
                                            |
                                            v
                              alútvonalak listája
                                            |
                    +-----------------------+------------------+
                    |                                          |
              kitöltés                                    körvonal
              scanline, NONZERO WINDING                   nsfb_plot_polylines
              (ez az SVG alapértelmezése,                 alútvonalanként
               ettől jók a lyukas alakzatok)
```

### 10.6 A JavaScript és a dobozfa

A NetSurf a dobozfát (box tree) egyszer építi fel, a dokumentum beparse-olása
után. Ha ezután a JavaScript módosítja a DOM-ot, a DOM megváltozik, de a
képernyő nem. Emiatt tűnt úgy, hogy a JS nem működik, holott az időzítők és a
kódfuttatás rendben voltak.

A `html_relayout_rexos.c` pótolja a hiányzó lépést. A legfontosabb tervezési
döntés, hogy **a régi fa végig életben marad**:

```
  DOM esemény (NodeInserted / SubtreeModified / AttrModified)
      |
      | 120 ms debounce (egy JS blokk sok módosítása egy layoutba fusson)
      v
  relayout_run
      |
      +-- kapuk: DONE státusz, nincs futó konverzió, nincs reflow,
      |          nincs frameset vagy iframe, base.active == 0
      |
      +-- a régi állapot félretétele:
      |       old_bctx, old_layout, old_forms, old_objects
      |       forms = NULL, object_list = NULL, bctx = NULL
      |       layout MARAD a régi fán
      |
      +-- minden DOM node-ról a doboz- és a libcss gyorsítótár törlése
      |
      +-- dom_to_box(...)   szakaszosan fut, 10 elem / ütem
              |
              |   EZ ALATT a böngésző tovább rajzol és kezeli az inputot,
              |   a RÉGI fából. Ha itt NULL lenne a layout, a rajzolás
              |   elszállna (22 helyen használja a motor).
              |
              v
  rebuild_done
      |
      +-- siker: a csere egyetlen ponton történik
      |     régi objektumok, űrlapok, imagemap, kijelölés elengedése
      |     talloc_free(old_bctx)
      |     content__reformat
      |     ha képek töltődnek: 150 ms-onként "settle" layout
      |
      +-- hiba: restore_old(), minden marad a régiben
```

Ehhez két vendor javítás kellett:

1. A libcss node adat gyorsítótárat érvényteleníteni kell, különben a
   `set_libcss_node_data()` `old_node_data == NULL` assertjébe fut. Ehhez a
   `select.c`-be kellett publikus belépő, mert a `selection_handler` statikus.

2. A libdom az elem class neveit külön tömbben gyorsítótárazza, mert a CSS
   illesztő ezt kérdezi. Ez a tömb viszont **csak az attribútum
   létrehozásakor** épült fel: `className = "..."`, `classList.add()` és
   `setAttribute("class", ...)` egyike sem frissítette. Emiatt a JavaScriptből
   végzett stíluscsere nem hatott semmire.

### 10.7 Mit tud és mit nem

Működik: DOM műveletek, `getElementById`, `classList`, `addEventListener`,
JSON, Array és String metódusok, `setTimeout`, `setInterval`, `clearInterval`,
`innerHTML`, `navigator.userAgent`. CSS oldalon: flexbox, táblázatok,
`position: absolute`, `rgba()`.

Nem működik: inline `element.style` (a NetSurfben szándékos csonk),
`querySelector` és `querySelectorAll`, `XMLHttpRequest`, `fetch()`,
`localStorage`, `document.title`, `window.innerWidth`. ES6 szintaxis
(`let`, `const`, nyíl függvények, template string) sincs, mert a Duktape
ES5.1-es. CSS oldalon a libcss nem ismeri a `border-radius`, `box-shadow`,
`linear-gradient`, `transform` és CSS Grid tulajdonságokat.

---

## 11. Build rendszer

```
  kernel/*.c, *.asm  --+
                       |  gcc -ffreestanding -mno-red-zone -mno-sse
                       |  ld -T kernel/linker.ld
                       +--> build/rexos.elf
  user/*.c  -----------+
                       |  host gcc, statikus, -nostdlib
                       +--> build/*.elf (hello, shell, desktop, ...)
  toolchain/ ----------+
                       |  x86_64-rexos-gcc (cross + newlib + glue)
                       +--> build/netsurf.elf
                                   |
                   tar  ------> build/initrd.tar
                                   |
                   xorriso + limine ------> rexos.iso
```

Fontos részlet a kernel Makefile-jában: minden `.o` függ az **összes** kernel
headertől. Enélkül egy struct layout változás után a régi objektumok elavult
offszetekkel maradnak, és a hiba jóval később, teljesen máshol jön elő.

---

## 12. Tesztelés

### 12.1 Headless regressziós tesztek

Mindegyik egy külön initrd-t épít, ahol a tesztprogram `desktop.elf` néven
indul bootkor, majd a soros kimenetet ellenőrzi.

| Parancs | Mit véd |
|---|---|
| `make run-test-heaptest` | brk és lapmappelés: sbrk növekedés, 30000 körös malloc/free kavarás, zsugorodás és újranövés mintaellenőrzéssel |
| `make run-test-fputest` | FPU/SSE állapot mentése kontextusváltásnál |
| `make run-test-newlibtest` | newlib alapok: stdio, fopen, fseek, time |
| `make run-test-nsspawn` | a desktop valódi útvonala: `spawn("netsurf.elf")` + `waitpid`, majd egy teljes Wikipédia oldal betöltése. Sikeres, ha a gyerektaszk elindult és nem volt CPU kivétel |
| `make run-test-domstress` | a dobozfa újraépítés őre: 600 soros oldal képekkel, 300 ms-onkénti DOM módosítás, közben kb. 700 billentyűleütés QMP-n |

### 12.2 Böngésző mérőpad

A `testbench/` könyvtár tesztoldalakat tartalmaz, a `tools/bench.sh` pedig
felbootolja rájuk a rendszert és képernyőképet ment.

```
  tools/bench.sh images 45
      |
      +-- helyi HTTP szerver a testbench/-en (a vendég felől 10.0.2.2:8099)
      +-- initrd_bench.tar építése a megadott URL-lel
      +-- headless QEMU, QMP csatorna
      +-- 45 mp után screendump
      |
      v
  build/bench_images.png  és  build/bench_images.log
```

Elérhető oldalak: `images` (kilenc képformátum), `css` (doboz és elrendezés
tulajdonságok), `js` (kb. 25 nyelvi és DOM próba), `timer` (időzítők, a
sikert hálózati kérésekkel jelzi, hogy a rajzolás ne zavarjon bele),
`restyle` (JS-ből végzett stíluscsere négyféle úton), `churn` és `big`
(folyamatos DOM módosítás stresszhez).

---

## 13. Ismert korlátok

- Nincs SMP, egy magon fut minden.
- Nincs APIC, a régi PIC-et használjuk.
- TCP-ben nincs retransmit időzítő és torlódásvezérlés.
- Nincs USB Mass Storage, a pendrive csak bootra jó.
- Nincs hangkezelés.
- Nincs ACPI, a "Power off" menüpont a desktopból lép ki, nem kapcsolja ki a gépet.
- A HHDM 4 GB-ig mappel, ezért 2 GB fölötti RAM-mal a bootloader a modult a
  leképezett tartomány fölé teheti.
- KVM gyorsítás alatt a rendszer nem bootol rendesen, ez még nyitott kérdés.

---

## 14. Repó szerkezete

```
  kernel/          a kernel forrása
    arch/x86_64/     gdt, idt, isr, irq, syscall, pic
    mm/              pmm, vmm, heap
    sched/           ütemező, taszkok
    fs/              vfs, tarfs, fat32, elf loader
    net/             eth, arp, ipv4, icmp, udp, tcp, dhcp, dns
    drivers/         pci, ahci, ata, nvme, usb, net, framebuffer,
                     console, serial, keyboard, mouse, timer, rtc, block
    lib/             printf, string, panic
  user/            felhasználói programok
    desktop/         grafikus felület
    newlib-glue/     a newlib alá tett RexOS réteg
    netsurf/         a böngészőhöz írt RexOS-specifikus forrásfájlok
  tools/           toolchain, portok, NetSurf build, patchek, mérőpad
  testbench/       böngésző tesztoldalak
  third_party/     bearssl, stb, fonts
  toolchain/       a legenerált cross toolchain és a portolt könyvtárak
```

---

## 15. Roadmap

Kész:

- [x] Long mode boot, saját lapozótáblák, HHDM
- [x] PMM, VMM, kernel heap, user brk
- [x] Preemptív ütemező FPU állapotmentéssel
- [x] syscall / sysret interfész, user mutató ellenőrzés
- [x] VFS, tarfs, FAT32 írással, block réteg, PCI
- [x] AHCI, legacy ATA, NVMe
- [x] xHCI + USB HID billentyűzet és egér
- [x] e1000, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP
- [x] BearSSL TLS a userspace-ben
- [x] Többablakos desktop TrueType fontokkal
- [x] NetSurf port: HTML, CSS, kilenc képformátum, Duktape JS
- [x] SVG raszterizáló a framebuffer frontendhez
- [x] Dobozfa újraépítés DOM változásra

Következő lépések:

- [ ] `element.style`, `querySelector`, `XMLHttpRequest` bindingek
- [ ] TCP retransmit időzítő
- [ ] USB Mass Storage és telepítő
- [ ] APIC és SMP
- [ ] IRQ-alapú xHCI a polling helyett
