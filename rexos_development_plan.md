# RexOS - Átfogó Továbbfejlesztési Terv

Ez a dokumentum a RexOS operációs rendszer jövőbeli fejlesztéseinek részletes, lépésről lépésre haladó (roadmap) leírása. A cél egy modern, teljesen használható, írható fájlrendszerrel, SSD támogatással és grafikus telepítővel rendelkező operációs rendszer kialakítása.

---

## 1. Fázis: Modern Háttértár és SSD Támogatás (Storage Layer)

A gyors és megbízható lemezműveletek alapjai. Jelenleg a rendszer PIO módban vagy egyszerű AHCI-vel olvas. Ezt kell professzionális szintre emelni.

### 1.1. NVMe (Non-Volatile Memory Express) Driver
Az modern SSD-k alapértelmezett csatolófelülete.
- **PCIe Enumeráció kiegészítése:** Az NVMe kontrollerek (Class 01h, Subclass 08h) felismerése a PCI buszon.
- **Admin és I/O Queues:** Submission (SQ) és Completion (CQ) sorok kialakítása a memóriában (ring bufferek).
- **MSI / MSI-X (Message Signaled Interrupts):** Az elavult hardveres megszakítások (IRQ) helyett az NVMe a memóriába írja a megszakítási jelet. Ennek támogatása a kernel megszakítás-kezelőjében.
- **NVMe parancskészlet:** Read, Write, Flush, és Identify parancsok implementálása az I/O sorokon keresztül.

### 1.2. Fejlett AHCI / SATA Támogatás
A régebbi SSD-khez és merevlemezekhez.
- **DMA (Direct Memory Access):** A CPU tehermentesítése. A kontroller közvetlenül a fizikai memóriába másolja a lemezről az adatot (PRDT - Physical Region Descriptor Table használatával).
- **NCQ (Native Command Queuing):** Több lemezművelet aszinkron kiadása, amit az SSD a legoptimálisabb sorrendben hajt végre.
- **Írási műveletek:** Az `ata_write_sector` és `ahci_write` függvények implementálása a block driver szintjén.

---

## 2. Fázis: Írható Fájlrendszer és Könyvtárszerkezet (VFS & FS)

A Virtual File System (VFS) bővítése, hogy ne csak olvasható, hanem írható, mozgatható és törölhető fájlokat/mappákat kezeljen.

### 2.1. VFS (Virtual File System) Kiegészítése
A `vfs.c` kiegészítése írási képességekkel.
- **Új Syscall-ok:** `sys_mkdir`, `sys_rmdir`, `sys_unlink` (törlés), `sys_rename`, `sys_chmod`.
- **Fájlleíró (File Descriptor) módosítások:** Nyitási módok támogatása (`O_RDWR`, `O_CREAT`, `O_APPEND`).
- **File Cache / Page Cache:** A memóriába olvasott szektorok gyorsítótárazása. Visszaírás (Write-back) algoritmus a teljesítmény növelése érdekében.

### 2.2. FAT32 Írás Támogatás (FAT32 Write)
Mivel a FAT32 az iparági szabvány pendrive-okhoz és EFI partíciókhoz.
- **Cluster allokáció:** Szabad clusterek keresése a FAT (File Allocation Table) táblában.
- **Könyvtárbejegyzések (Directory Entries):** Új (akár hosszú, LFN) fájlnevek létrehozása a mappákban, meglévők törlése (0xE5 jelölővel).
- **Fájlnövelés:** Ha a fájl mérete nő, új clusterek láncolása a fájlhoz.

### 2.3. Native Ext2 vagy RexFS Kialakítása (Opcionális, javasolt)
A FAT32 nem támogatja a jogosultságokat (rwx). A rendszermeghajtóhoz egy robusztusabb fájlrendszer kell.
- Implementálni a klasszikus ext2 fájlrendszert Inode-okkal, vagy egy saját, modern, naplózó (journaling) fájlrendszert (RexFS), ami védelmet nyújt az áramszünet okozta adatvesztés ellen.

### 2.4. Szabványos Mappastruktúra (FHS - Filesystem Hierarchy Standard)
A UNIX-szerű felépítés bevezetése a fő partíción:
- `/bin`: Alapvető parancsok és beépített alkalmazások (`ls`, `cat`, `shell`, `editor`).
- `/boot`: A kernel (`rexos.elf`) és a Limine/GRUB fájljai.
- `/dev`: Eszközfájlok (Virtual block és char device-ok, pl. `/dev/nvme0n1`, `/dev/sda1`, `/dev/fb0`).
- `/etc`: Rendszer konfigurációs fájlok (pl. felhasználók, grafikus beállítások).
- `/home`: A felhasználók személyes adatai, dokumentumok, képek.
- `/tmp`: Ideiglenes memória-fájlrendszer (ramfs).
- `/usr`: Felhasználói programok, library-k (pl. `/usr/bin`, `/usr/lib`).

---

## 3. Fázis: Teljes Értékű Telepítő (RexOS Installer)

Cél, hogy a Live CD-ről bootolva a felhasználó tartósan feltehesse a rendszert a gépére (vagy virtuális gépére).

### 3.1. Particionáló és Formázó Motor
- **GPT (GUID Partition Table) támogatás:** A GPT partíciós tábla olvasása és írása. (A régi MBR elhagyása).
- **Formázás (mkfs):** Beépített modulok egy üres partíció FAT32 vagy ext2/RexFS formátumra való inicializálására (boot szektor generálás, üres FAT és gyökérkönyvtár felírása).

### 3.2. A Telepítési Folyamat Lépései
1. **Lemezválasztás:** Az elérhető SSD/HDD eszközök kilistázása kapacitással (ahogy most is látszik az "Installer" appban).
2. **Particionálás:** A telepítő létrehoz egy EFI rendszerpartíciót (pl. 200MB, FAT32) a bootloadernek, és egy nagy rendszerpartíciót (RexFS) az OS-nek.
3. **Fájlmásolás:** A telepítő átmásolja a kernel képfájlt és a szükséges `/bin`, `/lib` tartalmakat a merevlemezre.
4. **Bootloader Telepítés:** A `Limine` (vagy GRUB) felírása az EFI partícióra, és a `limine.conf` automatikus generálása úgy, hogy a lemez UUID-je alapján találja meg a kernelt.

### 3.3. Grafikus Telepítő Alkalmazás (GUI Installer)
A `user/desktop.c`-ben lévő jelenlegi Installer kibővítése "Next -> Next -> Finish" típusú varázslóvá (Wizard).
- Folyamatjelző sáv (Progress bar) a fájlmásolás alatt.
- Telepítés utáni automatikus újraindítás (ACPI reset).

---

## 4. Fázis: Felhasználói Környezet és Új Appok

Amint a lemez írható és a rendszer strukturált, a felhasználói élmény következik.

### 4.1. C Standard Library (libc) Portolása
A jelenlegi minimalista `libc.c` lecserélése vagy bővítése egy teljes értékű könyvtárra (pl. `Newlib` portolása). Ez lehetővé teszi, hogy létező Linux/Unix programokat (pl. Nano, GCC, Python) minimális módosítással lehessen futtatni a RexOS-en.

### 4.2. Új, Hasznos Alkalmazások
1. **Képnézegető (Image Viewer):**
   - Először BMP, majd JPG/PNG (stb_image könyvtár) dekódolás.
   - Zoom és Pásztázás (Pan) funkciókkal.
2. **Zenelejátszó (Audio Player):**
   - Hangkártya driver (Intel HDA / AC97) beépítése a kernelbe.
   - Egyszerű WAV lejátszó a grafikus felületen, Play/Pause/Volume gombokkal.
3. **Jegyzettömb / Fejlesztői Kódszerkesztő (RexPad):**
   - A jelenlegi "Editor" továbbfejlesztése. 
   - Szintaxiskiemelés (Syntax Highlighting) C és Asm fájlokhoz.
   - Mentés (`Ctrl+S`), Keresés, Lecserélés funkciók.
4. **Fájlkezelő 2.0 (Files):**
   - Drag-and-drop támogatás.
   - Fájlok átnevezése (Rename), másolása (Copy), törlése (Delete) grafikus felületen.
   - Szabad hely megjelenítése a lemezen.

### 4.3. Ablakkezelő (Window Manager) Szétválasztása
A jelenlegi `desktop.c` egyetlen monolitikus program. A modern architektúra érdekében:
- Egy **Compositor (Window Server)** program, ami kezeli a framebuffert, és kliens-szerver (IPC, pl. UNIX socketek) kommunikációval fogadja más `.elf` fájloktól a kirajzolandó ablakok képpontjait.
- Ezzel a "Kalkulátor" és a "Snake" külön önálló `calc.elf` és `snake.elf` programokká válnak, és ha az egyik lefagy, nem rántja magával az egész grafikus felületet!

---

## Javasolt Megvalósítási Sorrend (Roadmap)

1. **VFS és ATA/AHCI Write driver implementáció** -> Hogy egyáltalán tudjunk lemezre írni.
2. **FAT32 Write képesség** -> Fájlok mentésének lehetősége a jelenlegi kódszerkesztőben.
3. **Mappastruktúra és Dinamikus betöltő (Loader)** kialakítása -> Valós `/bin` könyvtár elérése a RAM disk helyett.
4. **A CLI fájlkezelő eszközök (mkdir, rm, cp)** megírása és tesztelése.
5. **Grafikus Telepítő logikájának megírása** -> Így telepíthetővé válik az OS SSD-re.
6. **NVMe Driver** -> Modern hardverek támogatása.
7. **Window Manager szétválasztása** -> Az alkalmazások önálló `.elf` fájlokként fognak futni.
8. **Portolt alkalmazások és új UI programok (Képnézegető, Audio)**.

*Ez egy ipari minőségű OS fejlesztési íve. A projekt hatalmas, de hihetetlenül izgalmas kihívásokat rejt!*
