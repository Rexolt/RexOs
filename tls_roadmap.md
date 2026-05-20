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
- [x] `tcp_send_segment` MSS option a SYN-ben (`1460`); `tcp_send_data`
      `peer_mss`-ig chunkol, PSH csak az utolsó szegmensen. A SYN-ACK
      `tcp_parse_options` MSS-t kiolvas és `s->peer_mss`-re ment, clamp 64..1460.
- [x] `tcp_receive` hirdetett `window_size = free(rx_buf)`, így nem invitál
      olyan adatot a peer, amit elveszítenénk. `TCP_RX_BUF_SIZE` 8 KB -> 32 KB,
      TLS rekord (16 KB) + handshake fragment elfér.
- [ ] Retransmission timer (RFC 6298 egyszerűsített): tx_buf + RTO
- [ ] `rx_buf` ring-buffer (a jelenlegi memmove-os "consume" `O(n)`-es,
      és teljes feltöltés esetén stallolja a kapcsolatot amíg a userspace
      ki nem olvas)

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
- [x] BearSSL függőségek RexOS oldalon: `memcpy`, `memcmp`, `memset`, `memmove`,
      `strlen` a `user/libc.c`-ben implementálva (a kerneloldal külön kapja).
- [x] **Userspace BearSSL build**: külön `libbearssl_user.a` (812 KB) `-fpie`,
      `-mno-red-zone`, freestanding flag-ekkel a PIE userspace ELF-be linkelhetően.
      Külön target: `make bearssl-user`.
- [~] **Entropy forrás**: BearSSL PRNG-hez seed kell. Jelenleg `tls.c`
      `tls_collect_entropy` rdtsc + `get_ticks()` + stack-cím-jitter keverékből
      ad 32 byte-ot - **KRIPTOGRÁFIAILAG GYENGE**, csak skeleton. TODO:
      `rdrand` (CPUID jelzi) + e1000 RX timing + PIT jitter SHA-256 kever (a
      `br_hmac_drbg`-be az engine `inject_entropy`-jával beadva).
- [x] CA store: `tools/gen_ca_bundle.sh` lefutott, **121 trust anchor**
      (Mozilla CCADB / curl.se cacert.pem). `user/ca_bundle.c` ~270 KB,
      `build/ca_bundle.o` 82 KB. A script `sed`-del a brssl kimenetét
      konvertálja (`static` strip, `#define TAs_NUM` -> `const size_t`).

Tesztek: `bearssl client_basic` analógiát futtató user-space teszt program,
ami `httpbin.org:443`-ra csatlakozik és kiírja a státuszsort.

---

## Fázis 3 - TLS kapcsolatkezelés a user-space HTTP kliensben

A BearSSL "low-level engine" API-ját kötjük rá a meglévő `net_*` syscallokra.

- [x] `user/tls.c` + `tls.h`: `tls_ctx_t` struktúra (BearSSL `br_ssl_client_context`
      + `br_x509_minimal_context` + `BR_SSL_BUFSIZE_BIDI` (~33 KB) I/O buffer
      a kontextuson belül). Skeleton, fordítás-tesztelve.
- [x] `tls_connect(host, port)`: TCP socket nyitása, `br_ssl_client_init_full`,
      `inject_entropy`, `set_buffer`, `br_ssl_client_reset(host, 0)` (SNI),
      handshake-loop (`tls_run_until(BR_SSL_SENDAPP)`).
- [x] `tls_send` / `tls_recv` / `tls_close` - engine SENDREC/RECVREC pumpolása
      a `net_send`/`net_recv` szinkron API-ra.
- [x] `http.c`: `http_conn_t` absztrakció (TCP vagy TLS), `hc_open`/`hc_send`/
      `hc_recv`/`hc_close`. `h_parse_url` `https://` esetén `is_tls=1`,
      default port 443. `desktop.elf` linkelve: 298 KB (volt ~150 KB).
- [x] `http.c`: HTTPS redirectet most már a TLS úton dolgozza fel, nem
      `HTTP_ERR_HTTPS`-szel utasít vissza. `h_make_absolute_url` is támogatja
      a `https://` séma propagálását relatív Location header esetén.
- [x] X.509 érvényesség-idő: `tls_set_validation_time` az RTC-ből
      (`get_time()`) számolja a `(days, seconds)` párt Howard Hinnant
      proleptic Gregorian képlettel; különben BR_ERR_X509_TIME_UNKNOWN.
- [ ] User-space heap növelése: BearSSL kliens kontextus ~35 KB (engine state
      ~6 KB + bidi I/O 33 KB), a peremen lévő `malloc`-ja a jelenlegi user-libc-ben
      `sbrk`-re épül - ellenőrizni hogy nem fragmentálódik kritikusan, valószínűleg
      egy free-list coalesce kell.
- [ ] Időzóna: a `get_time()` jelenleg helyi időt (RTC nyers) ad, BearSSL UTC-t
      vár. A validációs ablak elég laza ahhoz, hogy CET/CEST eltolás ne okozzon
      gondot, de utólag tisztítani kell.

Tesztek: terminálos `http https://example.com` parancs (új CLI app), majd a
RexBrowser-ben `https://example.com`, `https://en.wikipedia.org`.

---

## Fázis 4 - Böngésző UX a modern webhez

A protokoll már HTTPS, de a render még XX. századi.

- [ ] `http.c` támogasson `gzip`/`deflate` `Content-Encoding`-ot (BearSSL nem
      ad miniz-t; külön `miniz.c` vendoring; nélküle a modern szerverek
      tömörítve küldenek és nem értjük)
- [x] HTTP/1.1 `Accept-Encoding: identity` request fallback (gyorsfix amíg a
      gzip nincs kész) - `user/http.c` minden GET-en küldi
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
