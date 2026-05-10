# RexOS - The x86_64 Sovereign Kernel Project

A **RexOS** egy ambiciózus, az alapoktól felépített (from scratch) operációs rendszer projekt, amely a modern x86_64 (Intel 64 / AMD64) architektúra lehetőségeit használja ki. A rendszer célja egy olyan monolitikus, de tiszta rétegekre osztott kernel létrehozása, amely képes biztonságos felhasználói környezetet (User Mode / Ring 3) biztosítani preemptív multitasking és grafikus felület mellett.

---

## ✨ 0. A Projekt Filozófiája

A **RexOS** nem egy hagyományos szoftverfejlesztési folyamat eredménye. Két fő pillérre épül, amelyek meghatározzák a fejlődését:

### 0.1. Részben „Vibe Coding” Projekt
Ez a projekt a modern, AI-asszisztált fejlesztés egyik ékes példája. A **Vibe Coding** itt azt jelenti, hogy a fejlesztés fókuszában nem a száraz, előre rögzített specifikációk állnak, hanem az **iteratív felfedezés** és a **kreatív flow**. 
- A fejlesztés során egy fejlett AI ágenssel (mint amilyen Antigravity) való szoros együttműködésben születnek meg a modulok. 
- A „vibe” adja az irányt: ha egy funkció (például a grafikus desktop vagy a preemptív ütemező) izgalmas kihívásnak tűnik, a rendszer abba az irányba fejlődik tovább, feszegetve a technológia és az AI-ember együttműködés határait.

### 0.2. Tanulóprojekt (Learning Journey)
Bár a kód mélyén komplex x86_64 assembly és C kódok rejlenek, a RexOS elsősorban egy **tanulási platform**. 
- Célja a hardware és a szoftver közötti legmélyebb kapcsolat megértése: hogyan kezeljük a memóriát regiszterek szintjén, hogyan irányítsuk a CPU-t megszakításokkal, és hogyan építsünk fel egy izolált felhasználói környezetet a semmiből.
- A projekt során elkövetett hibák (mint a korábbi syscall stack-corruption vagy a PS/2 szinkronizációs problémák) a tanulási folyamat szerves részei, amelyek dokumentálása és javítása adja a projekt igazi értékét.

---

## 🏛️ 1. Rendszerarchitektúra és Alapok

A RexOS a **Long Mode** architektúrára épül, amely 64 bites címtartományt és kiterjesztett regiszterkészletet biztosít.

### 1.1. Boot-fázis és Inicializálás
A rendszer a **Limine** bootloaderen keresztül indul. A Limine gondoskodik a CPU 64-bit módba kapcsolásáról, a kezdeti lapozó táblák felállításáról és a hardware információk (memóriatérkép, framebuffer cím) átadásáról.
- **HHDM (Higher Half Direct Map)**: A teljes fizikai RAM leképezésre kerül a `0xffff800000000000` virtuális címtől kezdődően. Ez alapvető a kernel számára a fizikai lapok közvetlen eléréséhez.
- **Kernel Base**: A kernel a `0xffffffff80000000` címtartományban foglal helyet, ami lehetővé teszi az alsó címtartomány (0x0 - 0x00007FFFFFFFFFFF) fenntartását a felhasználói programok számára.

### 1.2. GDT és IDT (Global & Interrupt Tables)
A kernel saját szegmentálási és megszakítási táblákat definiál:
- **GDT**: Tartalmazza a Kernel Code (0x08), Kernel Data (0x10), User Code (0x1B) és User Data (0x23) szegmenseket. A szegmensek közötti váltás elengedhetetlen a Ring 3-ba való átlépéshez.
- **IDT**: 256 bejegyzésből áll. Kezeli a CPU kivételeket (Exceptions), mint a **Page Fault (#PF)**, **General Protection Fault (#GP)**, valamint a hardware megszakításokat (IRQ-k).

---

## 🧠 2. Memória-menedzsment

A RexOS memóriakezelése három egymásra épülő rétegből áll, biztosítva a rugalmasságot és a biztonságot.

### 2.1. PMM (Physical Memory Manager)
Egy **Bitmap-alapú** allokátor, amely a RAM minden 4KB-os blokkját (frame) egy bittel reprezentálja. 
- **Működése**: A bootloader által adott memóriatérkép alapján a kernel megjelöli a használható területeket. 
- **Allokáció**: Amikor a kernelnek vagy egy folyamatnak fizikai lapra van szüksége (pl. lapozásnál), a PMM kikeresi az első szabad bitet és lefoglalja azt.

### 2.2. VMM (Virtual Memory Manager)
A 64 bites módban kötelező **4-szintű lapozást (PML4)** használja.
- **Struktúra**: PML4 -> PDPT -> PD -> PT. Minden szint 9 bitet foglal el a virtuális címből.
- **Izoláció**: Minden felhasználói taszk saját PML4 táblával rendelkezik. A kontextusváltás során a `CR3` regiszter frissítésével váltunk a folyamatok címtartományai között.
- **Flag-ek**: Támogatja a `NX` (No-Execute), `User`, és `Writable` biteket a memória védelme érdekében.

### 2.3. Kernel & User Heaps
- **Kmalloc**: A kernel belső dinamikus memóriakezelője, amely kis méretű objektumok allokálására alkalmas a kernel címtartományában.
- **sys_brk**: A felhasználói programok a `sys_brk` rendszerhívással növelhetik a saját adat-szekciójuk feletti területet. A kernel ilyenkor új fizikai lapokat mappel a kért virtuális címekre.

---

## ⚙️ 3. Ütemező és Multitasking

A multitasking képesség teszi lehetővé, hogy a RexOS egyszerre több programot futtasson (pl. shell és desktop).

### 3.1. Preemptív Ütemezés
A rendszer **időosztásos** alapú. A PIT (Timer) 100 Hz-es megszakításai (IRQ0) szakítják félbe az éppen futó taszkot.
- **Task Control Block (TCB)**: Egy struktúra, amely tárolja a folyamat PID-jét, nevét, állapotát (RUNNING, DEAD), regisztereit, stack mutatóját és lapozó tábláját.
- **Context Switch**: Assembly nyelven írt rutin, amely elmenti a CPU összes regiszterét a stackre, kicseréli az aktuális taszkot, majd betölti az új taszk regisztereit.

### 3.2. Taszk Életciklus
1. **Spawn**: A `sys_spawn` hívás beolvassa az ELF fájlt a fájlrendszerből.
2. **Load**: Az ELF loader felépíti a virtuális memóriát és feltölti a kód/adat szekciókat.
3. **Execute**: A scheduler sorra veszi a taszkot, és `SYSRET` utasítással elindítja Ring 3-ban.
4. **Exit**: A taszk a `sys_exit` hívással jelzi a végét.
5. **Reap**: A kernel `reaper` szála felszabadítja a lezárt taszk erőforrásait.

---

## 🛡️ 4. Rendszerhívások (Syscalls)

A felhasználói alkalmazások a `SYSCALL` utasítással kérnek szolgáltatásokat a kerneltől.

### 4.1. Regiszter Konvenciók
A paraméterek átadása az alábbi regiszterekben történik:
- `RAX`: Rendszerhívás száma (pl. 1=EXIT, 5=WRITE).
- `RDI, RSI, RDX, R10, R8, R9`: Argumentumok 1-től 6-ig.
- `RCX` és `R11`: A CPU felülírja őket a visszatérési címmel és a flag-ekkel.

### 4.2. Stack Biztonság
A RexOS kritikus fontosságú biztonsági funkciója, hogy rendszerhíváskor a kernel **saját kernel-stackre** vált. Ez megakadályozza, hogy egy rosszindulatú felhasználói program korrumpálja a kernel stackjét a hívás közben. Az assembly stub elmenti a felhasználói `RSP`-t a TCB-be, és betölti a kernel-stacket.

---

## 📁 5. Virtuális Fájlrendszer (VFS)

A fájlkezelés Unix-szerű absztrakción alapul.

### 5.1. VFS Csomópontok (vnodes)
Minden fájl vagy mappa egy `vfs_node_t` struktúra.
- **Műveletek**: `read`, `write`, `open`, `getdents`.
- **Mounting**: A rendszer indításkor felcsatolja a TarFS-t (initrd) a gyökérkönyvtárba (`/`).

### 5.2. TarFS (Initrd)
Mivel még nincs lemezdriverünk, egy TAR archívumot használunk fájlrendszerként. A kernel végigjárja a TAR header-eket, és virtuális fájlstruktúrát épít belőlük a memóriában. Ez tartalmazza a felhasználói programokat (`.elf`) és konfigurációs fájlokat.

---

## 🖱️ 6. Hardware Driverek

### 6.1. PIT (Timer)
A **Programmable Interval Timer** felelős az időalapért és az ütemezésért. 1.193182 MHz-es alapfrekvenciával rendelkezik, amelyet a kernel 100 Hz-re oszt le.

### 6.2. PS/2 Keyboard & Mouse
A két eszköz ugyanazon a PS/2 kontrolleren osztozik, de külön megszakításokat használnak:
- **Keyboard (IRQ1)**: Scancode-okat küld a 0x60 porton. A driver tartalmaz egy scancode-to-ASCII táblázatot.
- **Mouse (IRQ12)**: 3-bájtos csomagokat küld. Az első bájt a gombok állapotát és a delta előjeleket tartalmazza, a második és harmadik bájt pedig az X és Y elmozdulást. A driver kezeli a csomag-szinkronizációt is.

### 6.3. Framebuffer
A Limine által biztosított lineáris memóriaterület. A kernel és a GUI közvetlenül pixeladatokat ír ide (32-bit bpp, ARGB formátum).

---

## 🏢 7. Felhasználói Alrendszer (RexOS SDK)

### 7.1. Libc
A felhasználói programok nem közvetlenül hívják a syscall-okat, hanem a `libc.a` könyvtáron keresztül:
- `malloc(size)` / `free(ptr)`: Dinamikus memóriakezelés.
- `print(str)`: Szöveges kimenet.
- `spawn(path)`: Új folyamat indítása.
- `waitpid(pid)`: Várakozás folyamat befejezésére.

### 7.2. Desktop Environment
A RexOS büszkesége a grafikus asztali környezet.
- **Ablakok**: Támogatja a címsorral, bezáró gombbal rendelkező ablakok kirajzolását.
- **Bitmap Font**: Egy szoftveres font-renderelő, amely 5x7 pixeles karaktereket rajzol.
- **Kurzor**: Egy 12x16 pixeles szoftveres egérkurzor, amely a háttér mentésével és visszatöltésével mozog villogásmentesen.
- **Élő Óra**: A tálcán folyamatosan frissülő időmérő a `sys_ticks` segítségével.

---

## 🛠️ 8. Fejlesztés és Build Rendszer

A projekt egy komplex, de könnyen kezelhető **Makefile**-ra épül.

### 8.1. Fordítási Folyamat
1. **Kernel Fordítás**: Minden kernel `.c` és `.asm` fájl lefordul tárgykóddá, majd egyetlen `rexos.elf` fájllá linkelődik a kernel címtartományához igazítva.
2. **Userland Fordítás**: A libc és a felhasználói programok (`shell.c`, `desktop.c`, stb.) lefordulnak, de különálló `.elf` fájlokká.
3. **Initrd Generálás**: A lefordult felhasználói programokat a `tar` segédprogram egyetlen `initrd.tar` fájlba csomagolja.
4. **ISO Készítés**: Az `xorriso` és a `limine-deploy` segítségével elkészül a bootolható `rexos.iso`.

### 8.2. Futtatás QEMU-ban
A javasolt parancs a teszteléshez:
```bash
qemu-system-x86_64 -M q35 -m 256M -cdrom rexos.iso -boot d -serial stdio
```

---

## 🗺️ 9. Roadmap és Jövőkép

A RexOS célja, hogy egy teljes értékű, tanulságos és használható operációs rendszerré váljon.
- [ ] **SATA/AHCI**: Valódi háttértároló támogatás.
- [ ] **FAT32**: Lemezalapú fájlrendszer.
- [ ] **Network Stack**: Alapvető hálózati kommunikáció.
- [ ] **Shared Memory**: Gyorsabb IPC (Inter-Process Communication).
- [ ] **Font Smoothing**: Fejlettebb betűmegjelenítés (anti-aliasing).

---
*RexOS - The Awakening of a New Kernel.*
*Developed with passion for low-level systems.*
