# RexOS - Hálózati Stack és E1000 Driver Terv

A hálózatkezelés (Networking) bevezetése a RexOS-be egy hatalmas ugrás, ami megnyitja az utat a böngészők, hálózati fájlrendszerek (NFS) és az internet felé. 

A hálózati architektúrát az OSI (Open Systems Interconnection) modell alapján építjük fel, alulról felfelé haladva.

---

## 1. Fázis: Hardver Réteg (Intel E1000 Gigabit Ethernet Driver)

A QEMU és a VirtualBox alapértelmezett, kiválóan dokumentált hálózati kártyája az Intel E1000 (pl. PCI Vendor `0x8086`, Device `0x100E` vagy `0x10D3`).

### 1.1. PCI Inicializálás és MMIO
- **PCI Keresés:** Az E1000 kártya felderítése a PCI buszon.
- **BAR0 Olvasás:** A kártya MMIO (Memory Mapped I/O) regisztereinek leképzése a kernel memóriájába.
- **Bus Mastering:** Bekapcsolása a PCI parancs regiszterben, hogy a kártya használhassa a DMA-t (Direct Memory Access).
- **EEPROM Olvasás:** A kártya 6 bájtos **MAC címének** (Media Access Control) kiolvasása a kártya EEPROM-jából.

### 1.2. Transmit (TX) és Receive (RX) Ring Bufferek
- A kártya a fizikai memóriából olvas és oda is ír (DMA). Ehhez körkörös (Ring) puffereket kell allokálni a memóriában.
- **RX Ring:** Ide írja a kártya a bejövő hálózati csomagokat.
- **TX Ring:** Ide tesszük a kiküldendő csomagokat, és szólunk a kártyának (egy regiszter írásával), hogy küldje ki őket a kábelre.
- **Megszakítások (IRQ):** A kártya megszakítást küld (általában IRQ 10 vagy 11), amikor új csomag érkezik. Ezt kell a kernel megszakítás-kezelőjébe bekötni.

---

## 2. Fázis: Adatkapcsolati Réteg (Ethernet II)

Amint tudunk nyers byte-okat küldeni és fogadni a kábelen, meg kell értenünk az Ethernet kereteket (Frames).

### 2.1. Ethernet Header (14 bájt)
Minden csomag az alábbi fejléccel kezdődik:
- **Cél MAC cím** (6 bájt)
- **Forrás MAC cím** (6 bájt)
- **EtherType** (2 bájt): Megmondja, mi van a csomagban (pl. `0x0800` = IPv4, `0x0806` = ARP).

### 2.2. A Hálózati Interfész (Net Dev) Absztrakció
Hasonlóan a `block_device_t`-hez, létrehozunk egy `net_device_t` struktúrát, amin keresztül a felsőbb rétegek küldhetnek csomagokat anélkül, hogy ismernék az E1000-et (így később jöhet virtio-net vagy Realtek driver is).

---

## 3. Fázis: Hálózati Réteg (ARP és IPv4)

A lokális hálózaton (LAN) és azon túl történő kommunikáció alapjai.

### 3.1. ARP (Address Resolution Protocol)
Ahhoz, hogy egy IP címre csomagot küldjünk, tudnunk kell a hozzá tartozó MAC címet.
- **ARP Request:** "Kié a `192.168.1.1` IP cím? (Válaszolj a MAC címeddel a Broadcast MAC címre küldve)."
- **ARP Reply:** A válaszok feldolgozása.
- **ARP Cache:** Egy táblázat a kernelben, ami gyorsan megmondja, melyik IP-hez melyik MAC cím tartozik, hogy ne kelljen mindig kérdezni.

### 3.2. IPv4 (Internet Protocol version 4)
- **IP Header (20 bájt):** Forrás IP, Cél IP, TTL (Time to Live), Protokoll (pl. ICMP, UDP, TCP).
- **Checksum:** Az IP fejléc ellenőrzőösszegének kiszámítása.
- Csomagok fogadása, a fejléc ellenőrzése, és továbbítása a felsőbb rétegeknek.

---

## 4. Fázis: Szállítási Réteg és Eszközök (ICMP és UDP)

Ezek a protokollok már valós felhasználói élményt nyújtanak.

### 4.1. ICMP (Internet Control Message Protocol) - "Ping"
- Az első és legfontosabb teszt!
- Implementálni az **Echo Request** feldolgozását és az **Echo Reply** visszaküldését.
- Ezzel a RexOS már pingelhető lesz a gazdagépről (Host OS) vagy a hálózat más gépeiről!

### 4.2. UDP (User Datagram Protocol)
- Egyszerű, kapcsolat nélküli port-alapú kommunikáció.
- **UDP Header (8 bájt):** Forrás Port, Cél Port, Hossz, Checksum.
- Hasznos gyors adatcserére, DNS (Domain Name System) lekérdezésekre és DHCP-re (IP cím automatikus igénylése a routertől).

---

## 5. Fázis: A "Nagy Vad" - TCP (Transmission Control Protocol)

A TCP a legnehezebb hálózati protokoll. Kapcsolat-orientált, garantálja a csomagok sorrendjét és megérkezését.
- A **Three-way Handshake** (SYN, SYN-ACK, ACK) implementálása.
- Sorszámozás (Sequence numbers) és nyugtázás (Acknowledgments).
- Újraküldés (Retransmission) csomagvesztés esetén.
- Ablakméret (Sliding window) kezelése a sebesség szabályozására.
- *Ez a fázis teszi majd lehetővé a HTTP (Web) kapcsolatokat!*

---

### Mivel kezdjünk?
A hálózati fejlesztés aranyszabálya az alulról felfelé építkezés.
Az első lépés az **E1000 driver alapjainak (PCI felismerés, MMIO, MAC cím kiolvasás) megírása**, és a hálózati keretrendszer (`kernel/net/` mappa) kialakítása. Amikor a driver már tudja, hogy megszakítást kapott egy nyers adatcsomagról (például egy bejövő Ping kérésről), jöhet az Ethernet és az IPv4 feldolgozás!
