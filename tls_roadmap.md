# RexOS - Út a modern (HTTPS) webhez

Cél: a `RexBrowser` natívan érje el a `https://` oldalakat. A TLS-t **BearSSL**
portolásával valósítjuk meg (kicsi, malloc-mentes, ANSI C, embedded TLS lib).

A munka négy fázisra bomlik. Minden fázis után buildelhetőnek és tesztelhetőnek
kell lennie a rendszernek.

---

## Fázis 1 - TCP/HTTP stack megszilárdítása (előkészítés)

A TLS állapotgép byte-pontos, in-order stream-et követel. Jelenleg a TCP
implementációnk több helyen sérülékeny. Ennek a fázisnak nincs új API-ja,
csak a meglévő hibákat javítjuk.

- [x] `tcp_receive` FIN+data együttes ACK helyes kiszámítása
- [x] `tcp_receive` in-order szekvenciaszám ellenőrzés, out-of-order eldobás
      és duplicate-ACK küldés
- [x] `SYS_NET_RECV` IRQ-biztos `rx_buf` lopás (cli/sti kritikus szakasz)
- [x] HTTP redirect: ha a célt `https://`-re küldik, barátságos üzenet és a
      `final_url` mező maradjon az utolsó sikeresen lekért HTTP URL-en, ne az
      elérhetetlen HTTPS-en
- [ ] `tcp_send_segment` dinamikus szegmens-méret (>2 KB), MSS option a
      SYN-ben (`1460`) hogy a szerver ne 536-ra essen vissza
- [ ] `tcp_receive` `window_size` kezelés, hogy ne overflow-ozzon a rx_buf
      nagy adatfolyamnál (TLS handshake ~5 KB, body sokszor MB)
- [ ] Retransmission timer (RFC 6298 egyszerűsített): tx_buf + RTO
- [ ] `rx_buf` ring-buffer (a jelenlegi memmove-os "consume" `O(n)`-es)

Tesztek a fázis végén:
- `curl example.com` jellegű terhelés a böngészőből: 1 MB HTML letöltése
  csomagvesztés-szimulációval (`-netdev user,...,loss=10`).
- `tcpdump` (host oldal) a handshake-en: nincs RST, nincs SACK miss.

---

## Fázis 2 - BearSSL forrás integrálása

BearSSL letöltése (`https://www.bearssl.org/git/BearSSL`) és a kernelhez
illesztése.

- [x] `third_party/bearssl/` mappa, vendoring (BearSSL 0.6, 277 .c, ~2 MB,
      SHA256 `6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`)
- [x] `Makefile`: BearSSL fordítása `-ffreestanding -mno-sse -mno-mmx
      -mno-red-zone -mcmodel=kernel` kapcsolókkal, `make bearssl` →
      `build/bearssl/libbearssl.a` (~809 KB). BR_USE_UNIX_TIME=0,
      BR_USE_URANDOM=0, BR_RDRAND=0, BR_SSE2=0, BR_AES_X86NI=0 (a környezet
      egyiket sem ajánlja), BR_64=1, BR_INT128=1, BR_LE_UNALIGNED=1.
      Mind a 277 forrás warning nélkül átmegy.
- [ ] BearSSL függőségek RexOS oldalon: `memcpy`, `memcmp`, `memset`, `strlen`
      a `kernel/lib/string.c`-ben már megvan, de a libbearssl.a önmagában
      ezeket nem hozza - majd a user-space `libc.c`-be is be kell tenni
      (vagy újrahasznosítani a kernelét egy közös headerre)
- [ ] **Entropy forrás**: BearSSL PRNG-hez seed kell. Forrás:
      `rdrand` (ha CPUID jelzi) + `pit_ticks()` + mouse jitter + e1000 RX
      timing keverékéből SHA-256 (BearSSL-en belül adott `br_hmac_drbg`)
- [ ] CA store: Mozilla CCADB → BearSSL `brssl ta` paraméteres trust-anchor
      tömb generálás (egy `ca_bundle.c` ~150 KB beépítve)

Tesztek: `bearssl client_basic` analógiát futtató user-space teszt program,
ami `httpbin.org:443`-ra csatlakozik és kiírja a státuszsort.

---

## Fázis 3 - TLS kapcsolatkezelés a user-space HTTP kliensben

A BearSSL "low-level engine" API-ját kötjük rá a meglévő `net_*` syscallokra.

- [ ] `user/tls.c` + `tls.h`: `tls_ctx_t` struktúra (BearSSL `br_ssl_client_context`
      + `br_x509_minimal_context` + I/O bufferek)
- [ ] `tls_connect(host, port)`: TCP socket nyitása, BearSSL engine indítása,
      handshake-loop (`br_ssl_engine_current_state` ↔ `net_send`/`net_recv`)
- [ ] `tls_send` / `tls_recv` / `tls_close`
- [ ] `http.c`: ha az URL `https://`, akkor `tls_*` réteg, különben sima `net_*`
- [ ] `http.c`: HTTPS redirectet nem `HTTP_ERR_HTTPS`-szel utasít vissza, hanem
      ezen az új útvonalon dolgozza fel
- [ ] User-space heap növelése: BearSSL kliens kontextus ~25 KB, a peremen
      lévő `malloc`-ja a jelenlegi user-libc-ben `sbrk`-re épül - ellenőrizni
      hogy nem fragmentálódik kritikusan

Tesztek: terminálos `http https://example.com` parancs (új CLI app), majd a
RexBrowser-ben `https://example.com`, `https://en.wikipedia.org`.

---

## Fázis 4 - Böngésző UX a modern webhez

A protokoll már HTTPS, de a render még XX. századi.

- [ ] `http.c` támogasson `gzip`/`deflate` `Content-Encoding`-ot (BearSSL nem
      ad miniz-t; külön `miniz.c` vendoring; nélküle a modern szerverek
      tömörítve küldenek és nem értjük)
- [ ] HTTP/1.1 `Accept-Encoding: identity` request fallback (gyorsfix amíg a
      gzip nincs kész)
- [ ] HTML renderer: legalább `<title>`, `<meta viewport>` semmibevétel,
      `<style>` átugrás, alapvető CSS color a `style=` attribútumból
- [ ] Cookie jar (egyszerű in-memory `Set-Cookie` → `Cookie` echo)
- [ ] SNI a TLS ClientHello-ban (BearSSL alapból támogatja, `br_ssl_client_reset(ctx, host, 0)`)

---

## Kockázatok / nyitott kérdések

- **Bignum performancia**: BearSSL "i31" implementációja x86_64-en kb. 100 ms
  egy P-256 ECDHE-re QEMU-ban; elfogadható.
- **Idő/RTC**: tanusítvány `notBefore`/`notAfter` ellenőrzéshez kell pontos
  UTC. `kernel/drivers/rtc` adta `rtc_time_t` jó, de a timezone/DST nem
  kezelt; egyezzünk meg hogy nyers UTC-t adunk át BearSSL-nek.
- **MTU/TSO**: ha a szerver 1460-as MSS-szel küld vissza, a `tcp_receive`
  egy IP fragmentből kapja a teljes szegmenst. Ha a router fragmentál,
  jelenleg IPv4 reassembly-nk nincs - eldob. Ez a TLS handshake-et ronthatja
  ha valaki közbe iktatott fragmentál. Megfigyelni.
