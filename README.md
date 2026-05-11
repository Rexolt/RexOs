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
- **Műveletek**: `read`, `write`, `open`, `readdir`, `finddir`.
- **Útvonal-feloldás**: `vfs_lookup("/a/b/c")` rekurzívan navigál a `finddir` hívásokkal.
- **Mount tábla**: Max. 4 mount-pont a `fs_root` alatt. Indításkor a TarFS a `/`-ra, a FAT32 a `/mnt`-re kerül.

### 5.2. TarFS (Initrd)
A bootloader által betöltött TAR archívumot olvassa be a memóriából. Tartalmazza a felhasználói programokat (`.elf`) és konfigurációs fájlokat. Read-only.

### 5.3. Block Device Réteg
Egységes absztrakció a tároló-driverekhez (`kernel/drivers/block/block.h`). Egy `block_device_t` exportja: `name`, `sector_size`, `sector_count`, `read()`, `write()`. A felsőbb fs-k (FAT32) erről olvasnak, nem ismerik a konkrét hardvert.

### 5.4. PCI Bus
`kernel/drivers/pci` a legacy I/O port config space (0xCF8 / 0xCFC) alapján enumerálja az összes PCI eszközt. Minden eszközről vendor/device ID, class/subclass/prog_if, BAR0..5 elkérhető. Ez a fundamentális réteg minden modern hardver driver alá (AHCI, USB, hálózat, NVMe).

### 5.5. Tároló driverek
- **AHCI SATA** (`kernel/drivers/ahci`): modern PCI SATA controller-ek támogatása. PCI scan -> BAR5 (ABAR MMIO) -> HBA reset -> port COMRESET -> IDENTIFY -> READ DMA EXT (LBA48). Polling, egy port egyszerre.
- **Legacy ATA PIO** (`kernel/drivers/ata`): fallback a régi IDE 0x1F0 portokra, amikor nincs AHCI (pl. qemu `-M pc`).
- Mindkettő **block device-ként regisztrál** (`ahci0` / `ata0`), így a FAT32 bármelyikkel működik.

### 5.6. FAT32 (Lemez)
Valódi háttértároló-támogatás FAT32 fájlrendszerre:
- **FAT32 write-enabled** driver (`kernel/fs/fat32.c`), 8.3 nevek + LFN (Long File Name) támogatással, alkönyvtárak rekurzívan.
- **Írási műveletek**: fájlok létrehozása (`O_CREAT`), módosítása, törlése (`sys_unlink`) és mappák létrehozása (`sys_mkdir`).
- **MBR + sima FAT32** is támogatott (boot szektor detektálás).
- Bármilyen regisztrált block device-ról olvas/ír.
- Felmountolva a `/mnt`-re; minden FAT32 fájl elérhető a `vfs_lookup("/mnt/...")` hívással.
- A `make disk` célt használja a `disk.img` előállítására `mtools` segítségével (nem kell root).

---

## 🖱️ 6. Hardware Driverek

### 6.1. PIT (Timer)
A **Programmable Interval Timer** felelős az időalapért és az ütemezésért. 1.193182 MHz-es alapfrekvenciával rendelkezik, amelyet a kernel 100 Hz-re oszt le.

### 6.2. PS/2 Keyboard & Mouse (legacy)
A két eszköz ugyanazon a PS/2 kontrolleren osztozik, de külön megszakításokat használnak:
- **Keyboard (IRQ1)**: Scancode-okat küld a 0x60 porton. A driver tartalmaz egy scancode-to-ASCII táblázatot.
- **Mouse (IRQ12)**: 3-bájtos csomagokat küld. Az első bájt a gombok állapotát és a delta előjeleket tartalmazza, a második és harmadik bájt pedig az X és Y elmozdulást. A driver kezeli a csomag-szinkronizációt is.
- **Fallback**: a PS/2 driver mindig fut, de a `keyboard_has_data()` és `mouse_get_state()` API-k az USB HID adatokat is beolvassák, így a PS/2 port hiányakor is van input.

### 6.3. USB xHCI + HID
Modern gepeken a billentyűzet + egér az USB-n keresztul jelenik meg. A teljes stack:
- **PCI discovery** (`class=0x0C subclass=0x03 prog_if=0x30`): xHCI controller detektálása.
- **MMIO (BAR0 → HHDM)**: Capability / Operational / Runtime / Doorbell regiszterek.
- **HC reset + run**: stop → HCRST → wait CNR=0 → RUN=1.
- **Command Ring + Event Ring + ERST**: 256 TRB-s ringek, 4 KB oldalakon, cycle-bit és Link TRB a ring végén.
- **Scratchpad buffer-ek**: ha a HCSPARAMS2 kéri, aláfoglaljuk.
- **Port enumeráció**: minden csatlakoztatott portra COMRESET → `Enable Slot` command → Input + Device Context építés → `Address Device`.
- **Control transfer (EP0)**: `GET_DESCRIPTOR(Device)`, `GET_DESCRIPTOR(Config)`, `SET_CONFIGURATION` három-stage TRB sorozattal (Setup + Data + Status).
- **HID osztály detektálása**: Interface descriptor `class=3` (HID), Interrupt IN endpoint descriptor kikeresése.
- **Configure Endpoint**: HID EP-hez új TR ring, Input Control Context (A0 + EP flag), `Configure Endpoint` command.
- **Report polling**: a fizikai egér-/billentyű mozdulat minden egyes HID report-ot egy Transfer Event ként Event Ring-re tesz; a `xhci_poll()` minden belőpéskor feldolgozza és újra küldi a Normal TRB-t.
- **HID Boot Protocol**: keyboard = 8 bájtos report (mod + 6 keycode), mouse = 3 bájtos report (buttons, dx, dy). ASCII konverzió a 104-128 usage ID-s tartományból.
- **Integráció**: a `keyboard_has_data()`/`keyboard_getc()` és a `mouse_get_state()` transzparensen elfogadja az USB forrást — a userland app-ok semmit sem érzékelnek a váltásból.
- **Polling mód**: nincs IRQ még, a `xhci_poll()` minden input-syscallból fut le (1-2 μs). IRQ-s változat később.
- **Limitáció**: egyetlen xHCI controller, max 8 USB device, USB hub nincs. Néhány BIOS a PCI enumerálásnál a hub miatt megsokszorozza a xHCI-t — az elsőt használjuk.

### 6.4. Framebuffer
A Limine által biztosított lineáris memóriaterület. A kernel és a GUI közvetlenül pixeladatokat ír ide (32-bit bpp, ARGB formátum).

---

## 🏢 7. Felhasználói Alrendszer (RexOS SDK)

### 7.1. Libc
A felhasználói programok nem közvetlenül hívják a syscall-okat, hanem a `libc.a` könyvtáron keresztül:
- `malloc(size)` / `free(ptr)`: Dinamikus memóriakezelés.
- `print(str)`: Szöveges kimenet.
- `spawn(path)`: Új folyamat indítása.
- `waitpid(pid)`: Várakozás folyamat befejezésére.

### 7.2. Desktop Environment (v2)
A RexOS grafikus asztali környezete teljes többablakos shell:
- **Interaktív ablakkezelés**: címsor-húzás, bezárás (x), fókusz, kaszkád pozícionálás (max. 16 ablak).
- **Modern Grafika**: Alpha blending (átlátszóság), lekerekített sarkok, vetett árnyékok és vertikális színátmenetek.
- **Asztali ikonok**: Bal oldalt kattintható alkalmazás-indítók, **smooth hover animációkkal**.
- **Tálca**: Glassmorphism (áttetsző) effekt, Start gomb, megnyitott ablakok listája, digitális óra, Shutdown gomb.
- **Háttérkép**: Automatikus `.bmp` háttérkép betöltése a lemezről vagy procedurális gradiens generálása.
- **Performance**: Dirty Rectangle alapú renderelés (csak a változott képernyőterületek frissítése).
- **Start menü**: Felugró applikáció-választó panel, beépített Shutdown funkcióval.
- **Beépített alkalmazások**:
  - **Files** — fájlkezelő initrd és FAT32 (`/mnt`) támogatással.
  - **Code Editor** — Teljes értékű szövegszerkesztő: Command/Insert módok (Vim-stílus), és **lemezre mentési lehetőség (`m` billentyű)**.
  - **Calculator** — 4 alapművelet, billentyűzettel és egérrel is.
  - **Terminal** — beépített parancssor: `ls`, `cd`, `cat`, `pwd`, `run`, `uptime`, `clear`.
  - **SysInfo** — élő rendszer-információk (uptime, block + PCI device szám).
  - **Hardware** — részletes PCI + block device lista vendor/device ID-kkel.
  - **Installer** — RexOS telepítő (target disk választó, confirm flow; a tényleges írás várja a FAT32 write és USB-MSC támogatást).
  - **Clock** — nagyméretű digitális óra (kb. pixel-art számokkal).
  - **About RexOS** — projekt leírás.
- **Back buffer**: A teljes renderelés egy off-screen pufferben történik a villogásmentes megjelenítésért.
- **Bitmap Font + Kurzor**: 5x7 pixeles font; 12x16 pixeles egér-kurzor a back buffer fölött rajzolva.
- **ESC**: kilépés az asztalból.

---

## 🛠️ 8. Fejlesztés és Build Rendszer

A projekt egy komplex, de könnyen kezelhető **Makefile**-ra épül.

### 8.1. Fordítási Folyamat
1. **Kernel Fordítás**: Minden kernel `.c` és `.asm` fájl lefordul tárgykóddá, majd egyetlen `rexos.elf` fájllá linkelődik a kernel címtartományához igazítva.
2. **Userland Fordítás**: A libc és a felhasználói programok (`shell.c`, `desktop.c`, stb.) lefordulnak, de különálló `.elf` fájlokká.
3. **Initrd Generálás**: A lefordult felhasználói programokat a `tar` segédprogram egyetlen `initrd.tar` fájlba csomagolja.
4. **ISO Készítés**: Az `xorriso` és a `limine-deploy` segítségével elkészül a bootolható `rexos.iso`.

### 8.2. Futtatás QEMU-ban
A javasolt parancsok:
```bash
make run         # Modern q35 chipset + AHCI SATA (alapértelmezett)
make run-legacy  # Régi -M pc chipset + IDE PIO (fallback)
make run-uefi    # UEFI mód OVMF firmware-rel (AHCI)
make run-usb     # q35 + AHCI + xHCI + USB kbd/mouse (valos géphez közel)
```

A RexOS boot közben automatikusan választ: először PCI-n megpróbálja az AHCI controllert (q35, valódi gépek), és ha nem talál, visszaesik a legacy IDE PIO-ra (régi gépek / `-M pc`).

Manuálisan modern módban:
```bash
qemu-system-x86_64 -M q35 -m 256M \
    -cdrom rexos.iso \
    -drive id=disk0,file=disk.img,format=raw,if=none \
    -device ide-hd,bus=ide.0,drive=disk0 \
    -boot d -serial stdio
```

### 8.3. Saját fájl hozzáadása a FAT32 lemezhez
A `disk.img` fájlt bármikor szerkesztheted `mtools`-szal:
```bash
mcopy -i disk.img sajat.txt ::/sajat.txt
mdir  -i disk.img ::
```
A RexOS legközelebbi indításkor látni fogja az új fájlt a `/mnt`-en.

### 8.4. Live USB fizikai gépre
A `rexos.iso` hibrid bootolható (BIOS + UEFI). Pendrive készítés:
```bash
sudo dd if=rexos.iso of=/dev/sdX bs=4M conv=fsync status=progress
sync
```
(A `/dev/sdX` a pendrive eszközneve — **előtte ellenőrizd `lsblk`-val**.)

A BIOS-ban kapcsold ki a **Secure Boot**-ot, és ha nem indul modern módban, állítsd **Legacy/CSM** bootra. Ha a gép csak xHCI-t biztosít USB-re (ami ma szinte minden gép), a RexOS az újonnan hozzáadott USB stack miatt továbbra is kap billentyűzetet/egeret.

**Mi működik ma valós gépen:**
- ✅ Framebuffer (UEFI GOP / BIOS VBE)
- ✅ Kernel boot, scheduler, memóriakezelés
- ✅ AHCI SATA (olvasás/írás) — `/mnt` FAT32
- ✅ NVMe SSD támogatás (PCIe / MMIO)
- ✅ USB billentyűzet + egér (xHCI + HID boot)
- ✅ PS/2 fallback ha a BIOS emulál
- ✅ Modern GUI: Átlátszóság, árnyékok, animációk
- ✅ ACPI Shutdown / Power management
- ✅ VFS írás: fájlok mentése, törlése, mappák létrehozása

**Mi NEM működik (még):**
- ❌ USB Mass Storage (a pendrive csak bootra lesz jó, a RexOS-on belül nem jelenik meg block device-ként)
- ❌ Hálózat (E1000/virtio-net folyamatban)
- ❌ Hang (Intel HDA driver hiánya)

---

## 🗺️ 9. Roadmap és Jövőkép

A RexOS célja, hogy egy teljes értékű, tanulságos és használható live + telepíthető operációs rendszerré váljon.

**Kész:**
- [x] **PCI enumeráció**: modern hardver driver-fundament.
- [x] **Block device réteg**: absztrakt `block_device_t` interfész.
- [x] **AHCI SATA**: modern PCI SATA tárolók olvasása.
- [x] **Legacy ATA PIO**: fallback régi gépekre.
- [x] **FAT32 read-only**: lemezalapú fájlrendszer.
- [x] **Több ablakos desktop**: interaktív ablakkezelés, 9 beépített app, Hardware + Installer UI.

**Folyamatban (live rendszer + telepítő útvonal):**
- [x] **USB xHCI controller**: modern gépeken USB bekapcsolása (2025-11-es úttal).
- [x] **USB HID**: USB billentyűzet + egér driver, transzparens integráció.
- [x] **NVMe**: modern SSD-k támogatása (PCIe MMIO).
- [x] **FAT32 írás**: cluster-foglalás, FAT-frissítés, directory entry alloc.
- [x] **ACPI shutdown**: szoftveres leállítás és shutdown képernyő.
- [ ] **USB Mass Storage**: boot pendrive-ról, második block device.
- [ ] **Installer logika**: partició + FAT32 formáz + kernel/initrd másolás + Limine setup.
- [ ] **USB hub támogatás**: több device egy porton.

**Távoli célok:**
- [ ] **NVMe**: modern SSD-k.
- [ ] **Network Stack**: E1000/virtio-net + TCP/IP.
- [ ] **APIC + SMP**: több magos CPU.
- [ ] **Shared Memory IPC**.
- [ ] **Font Smoothing / TrueType**.

---
*RexOS - The Awakening of a New Kernel.*
*Developed with passion for low-level systems.*
