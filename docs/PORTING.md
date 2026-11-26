# WARP HUB – stan prac, decyzje, wyniki

## Stan prac

| Krok | Stan |
|---|---|
| 1. Repo, katalog gier (`catalog/catalog.txt`), generator `tools/make_catalog.py` z testami | ✅ 19.11.2026 |
| 2. Klient do Shella `WarpHubCLI` (AmiSSL, pobieranie z `raw.githubusercontent.com`, SHA-256, SNTP) | ✅ 19.11.2026, Amiga 2 |
| 3. Test HTTPS z GitHubem: Amiga 2 ✅; Amiga 1, WinUAE, obciążenie CPU (WarpSSL) | ⏳ |
| 4. Zegar Amigi a certyfikaty: SNTP tylko do sprawdzenia certyfikatu (zrobione, do potwierdzenia przez użytkownika) | ✅ / decyzja |
| 5. GUI `WarpHub` 0.2 (ReAction + kafelki, PL/EN/DE, instalacja w oknie) | ✅ pierwsza wersja 19.11.2026, Amiga 2 |
| 6. Instalacja / aktualizacja w GUI, aktualizacja samego WARP HUB | ⏳ |
| 7. Repo `cizar-szymon/Amiga_WarpHub` na GitHubie, publiczne (katalog z `raw` działa na Amidze 2) | ✅ 19.11.2026 |
| 8. Wydanie WARP HUB (numer, ZIP, opisy PL/EN/DE, aktualizacja samego siebie) | ⏳ |

## Decyzja: pobieranie wprost z GitHuba (19.11.2026)

**WARP HUB pobiera gry wprost z GitHuba przez HTTPS** z AmiSSL 5. Na kartach Warp od
oprogramowania 3.074 AmiSSL przyspiesza **WarpSSL**: szyfrowanie AES-GCM i ChaCha20-Poly1305
oraz uzgadnianie TLS robi karta. Warp560 / 100 MHz: ok. 1 MB/s przy 55% CPU zamiast 450 KB/s
przy 100%.

Dlaczego tak:
- TLS gwarantuje, że pliki pochodzą z GitHuba, a SHA-256 z katalogu sprawdza każdy plik;
- nie potrzeba żadnego własnego serwera.

Wymagania gracza: AmiSSL 5 (`amisslmaster.library` ≥ 5), stos TCP/IP, poprawna data
(certyfikaty).

Jak to działa:
- `tools/make_catalog.py` (na PC) pyta GitHub API o najnowszy `releases/amiga/<Gra>-<v>.zip`
  na domyślnej gałęzi repo, pobiera go i zapisuje do `catalog/catalog.txt` każdy plik szuflady
  gry: rozmiar, SHA-256, flagę `N` / `K`;
- każdy plik jest porównany z drzewem gita tej gałęzi (SHA-1 bloba), więc to, co Amiga
  pobierze pojedynczo z
  `https://raw.githubusercontent.com/<repo>/<gałąź>/releases/amiga/<ścieżka>`, jest
  dokładnie tym, co w wydaniu;
- Amiga nie używa GitHub API (limit 60 zapytań na godzinę), tylko `raw`;
- flaga `K` oznacza plik gracza: ikonę programu z Tool Types, ikonę szuflady, `*.ini`,
  `*.prefs`, `*.cfg`. Te pliki są zapisywane tylko wtedy, gdy ich nie ma;
- `README` to `ReadMe.txt` z pustych szuflad na dane, czyli co gracz musi dołożyć.

Format katalogu: opis na początku `tools/make_catalog.py`.

Sprawdzanie wersji w grach (przycisk UPDATE) jest do ustalenia z użytkownikiem. Gry mogłyby
czytać ten sam `catalog.txt`.

## Wyniki

### Katalog (19.11.2026)
- Publiczne są 3 repo: Settlers 0.139 (`amiga-port`, 27 plików, 6,7 MB), OpenDUNE 0.54
  (`main`, 21 plików, 1,1 MB) i Diablo 0.48 (`main`, 251 plików, 18,2 MB).
- Miner 2049er, Syndicate Wars, Vastar i Xain'd Sleena: GitHub daje 404 (repo prywatne albo
  nieopublikowane). Generator je pomija; Amiga pobiera bez logowania.
- `tools/check_raw.py --all`: wszystkie 299 plików pobranych z `raw.githubusercontent.com`
  zgadza się z katalogiem (rozmiar + SHA-256), 82 s na PC.
- Testy: `python -m unittest discover -s tools/tests` – 9 testów OK.

### Do poprawienia w repo gier (znalezione przez generator)
- **CRLF w ZIP-ach:** w gicie pliki tekstowe mają LF, w ZIP-ie zrobionym na Windows CRLF.
  Generator bierze wersję z gita (LF) i pisze ostrzeżenie. Dotyczy:
  - Settlers `doc/micromod-licence.txt`;
  - OpenDUNE `doc/COPYING`;
  - Diablo 9 plików `mods/hf/txtdata/**/*.tsv`.

  Skrypty wydań powinny pakować pliki z gita (`git archive` / `git show`) albo z LF.
- **OpenDUNE `data/ReadMe.txt`** jest w ZIP-ie, ale nie w gicie (pewnie `.gitignore`), więc
  nie da się go pobrać przez `raw`. Pominięty.
- **Diablo** ma ścieżki ze spacjami (`assets/lua/mods/Floating Numbers - XP/init.lua`).
  Katalog je dopuszcza (pojedyncze spacje wewnątrz nazwy, ścieżka jest ostatnim polem linii).

### Amiga 2 (A4000 + Warp 7060), AmiSSL (19.11.2026)
- `amisslmaster.library 5.24`, OpenSSL 3.5.4 (`AmiSSL:` = `WorkSD:Net/AmiSSL`, certyfikaty
  w `AmiSSL:Certs`).
- `wget 1.10.1` w systemie nie obsługuje HTTPS.
- `OpenSSL s_client` do `raw.githubusercontent.com`: przy dacie Amigi 01-Jan-78 (brak baterii)
  certyfikat jest odrzucany: „certificate is not yet valid”. **Klient musi rozwiązać zegar.**
- `s_client` nie kończy się po zamknięciu połączenia i blokuje AmiAgenta (`RUN` czeka). Nie
  używać go przez `run`. Pomiar prędkości zrobi własny klient.
- Działa `C:WarpSSL` (widoczny w `Status`).

### WarpHubCLI 0.1 na Amidze 2 (19.11.2026, `RAM:WarpHub-test`, katalog lokalny `CATALOG=`)

Budowa: `sh tools/build_amiga.sh` (bebbo gcc 6.5 w Dockerze, SDK AmiSSL z `C:miga-devmissl`).
Binarka ma 33 KB, `__stkinit` jest (Makefile sprawdza), `find64.sh` pokazuje 0 instrukcji
64-bit mul/div. Testy przez AmiAgenta: `sh tools/amiga_test.sh <cfg> <nazwa> <argumenty>`
(uruchamia w tle z logiem, bo zawieszony `run` blokuje AmiAgenta).

| Test | Wynik |
|---|---|
| `TIME` | zegar Amigi 1978-01-01, NTP 2026-11-19 – poprawny |
| `LIST LANG=pl` | 3 gry z katalogu |
| `INSTALL opendune` (21 plików, 1,0 MB) | 6,3 s, 173 KB/s, TLSv1.3 TLS_AES_128_GCM_SHA256, wszystkie SHA-256 zgodne |
| `CHECK opendune` po instalacji | 0 plików do pobrania, 4 pliki gracza zostawione |
| uszkodzony program, zmieniony `opendune.ini`, skasowany `doc/COPYING` → `INSTALL` | pobrane tylko 2 pliki (490 KB, 1,5 s); stary program w `old/OpenDUNE-0.54`; `opendune.ini` gracza nietknięty |
| katalog z błędną SHA-256 programu | „wrong SHA-256 – not taken”, gra bez zmian, brak `.upd` |
| `INSTALL diablo` (251 plików, 17,3 MB) | 72,7 s, średnio 244 KB/s |
| jeden plik 5,2 MB (`devilutionx`) | 7,3 s, **721 KB/s** (z SHA-256 i zapisem do RAM:) |
| sprawdzenie 251 lokalnych plików (SHA-256 17 MB) | 7,6 s |

Wnioski:
- Duże pliki schodzą z prędkością ok. 720 KB/s. Przy wielu małych plikach dominuje czas
  zapytania, ok. 190 ms na plik (Diablo: 48 s z 73 s).
- Do poprawy: kilka zapytań naraz (pipelining HTTP/1.1) albo 2–3 połączenia.
- Jeden serwer NTP raz nie odpowiedział. Klient próbuje teraz kolejno `pool.ntp.org`,
  `time.cloudflare.com`, `time.google.com`, `europe.pool.ntp.org`.
- Nie zmierzone: obciążenie CPU w trakcie pobierania (czy WarpSSL faktycznie pracuje), Amiga 1,
  WinUAE.

## Okno WARP HUB (19.11.2026)

Decyzje użytkownika (19.11.2026):
- **GUI:** ReAction (OS 3.2) z galerią kafelków rysowaną przez program;
- **obrazki:** PNG w repo WarpHub (`catalog/images/<gra>.png`, 224×140, 256 kolorów,
  `tools/make_images.py`; źródła w `games.json`, pole `image`), czytane przez datatypes;
- **katalog instalacji:** program zawsze pyta (requester ASL), aktualizacja idzie do
  zapamiętanej szuflady;
- **ustawienia:** `WarpHub.prefs` obok programu (`LANGUAGE`, `CATALOG`, `INSTALLED <gra> <wersja> <szuflada>`).

Budowa okna (`amiga/warphub.c`):
- **u góry:** pasek stanu i postępu (fuelgauge), wybór języka, „Odśwież”;
- **galeria:** `space.gadget`, rysowana hookiem. Kafelek ma obrazek, nazwę i stan:
  do pobrania / zainstalowana / nowa wersja. Kliknięcie obsługuje `IDCMP_MOUSEBUTTONS`, bo
  `space.gadget` podaje pozycję myszy tylko w notyfikacjach. Przewijanie: scroller, kółko
  myszy, strzałki;
- **strona gry:** drugi `space.gadget` (opis, rozmiar, repo, „potrzebne pliki gry”) i przycisk
  Zainstaluj / Aktualizuj / Sprawdź pliki oraz Przerwij;
- **historia zmian:** listbrowser z pliku `<Gra>_release*.txt` gry, zawijana do szerokości.

Sieć działa w osobnym procesie (`hub_worker.c`), bo baza `bsdsocket` należy do jednego
procesu, a okno nie może czekać:
- odświeżanie: katalog, obrazki i historie zmian do `PROGDIR:cache/`, każdy plik sprawdzony
  SHA-256;
- instalacja: `hub_plan` + `hub_install`, postęp sygnałem do okna.

Przy starcie okno pokazuje katalog z pamięci podręcznej, a odświeżenie idzie w tle.

Pamięć modułów przez `AllocVec` (`hub_mem.h`), bo pula `malloc` libnix nie jest bezpieczna
przy dwóch procesach.

Teksty: polski bez ogonków (jak opisy gier w paczkach – czcionki Workbencha nie mają
polskich liter), niemiecki w Latin-1; `hub_to_latin()` zamienia UTF-8 z katalogu.

Pułapki bebbo gcc:
- makra `reaction_macros.h` (`End`) nie działają, bo `NewObject` jest tu makrem. Trzeba
  pisać `NewObject(..., TAG_END)`;
- bazy klas ReAction muszą być zainicjowane (`= NULL`). Jako symbole „common” dociągają
  zaślepki libnix, które próbują otworzyć „window.library”.

Testy na Amidze 2 (`RAM:WarpHub-test`, katalog lokalny `CATALOG=`, bo repo jeszcze nie jest
publiczne):

| Test | Wynik |
|---|---|
| start z Shella i z ikony (`WBStart`, `amiga/tools/wbstart.c`) | okno otwarte, katalog odświeżony, Esc zamyka okno i proces sieci |
| obrazki PNG (datatypes) na ekranie RTG 16-bit | poprawne |
| historia zmian z GitHuba (6 plików do `cache/notes`) | pobrana, wyświetlona |
| `LANG=de` | niemieckie napisy i opisy |
| `TESTINSTALL=settlers TESTTO=RAM:WarpHub-test/gui` (opcja testowa tylko z Shella, bez requesterów) | pasek postępu w trakcie, potem „Zainstalowana 0.139”, wpis w `WarpHub.prefs` |

Nie sprawdzone: klikanie (AmiAgent nie ma myszy) – wybór kafelka, requester ASL,
pytania Tak/Nie, Przerwij w trakcie, zmiana języka w oknie; Amiga 1; WinUAE; ekran 8-bit.
Ikona programu – do zrobienia i pokazania użytkownikowi.

## Symulacja wszystkich portów (19.11.2026)

`python tools/make_catalog.py --simulate` tworzy `catalog/catalog-test.txt` (poza gitem):
- gry publiczne przepisuje z `catalog.txt`, bez zapytań do GitHub API (limit 60 na godzinę
  wyczerpał się przy próbach);
- gry, których repo nie jest publiczne, bierze z lokalnych ZIP-ów
  (`games.json`, pole `local`), z linią `SOURCE local`;
- historie zmian tych gier zapisuje do `catalog/notes/` (poza gitem), bo Amiga nie pobierze
  ich z GitHuba.

10 gier:

| Gra | Wersja | Źródło | Rozmiar |
|---|---|---|---|
| Settlers | 0.139 | GitHub | 6,7 MB |
| OpenDUNE | 0.54 | GitHub | 1,1 MB |
| Diablo | 0.48 | GitHub | 18,2 MB |
| Miner 2049er | 0.1 | lokalnie | 0,25 MB |
| Syndicate Wars | 0.220 | lokalnie | 5,8 MB |
| Vastar | 0.1 | lokalnie | 0,19 MB |
| Xain'd Sleena | 0.9.17 | lokalnie | 0,36 MB |
| Wizard of Wor | 0.1 | lokalnie | 0,22 MB |
| AmiXcom | 0.106 | lokalnie (bez repo) | 42,6 MB |
| ZOD | 0.12 | lokalnie (repo AMIGAbench/ZodEngine, ZIP poza gitem) | 15,0 MB |

GTA nie ma jeszcze wydania w `releases/amiga`.

Obsługa w oknie:
- gry z `SOURCE local` mają na kafelku „Wkrótce”, na stronie informację, że repo nie jest
  publiczne, i wyłączony przycisk instalacji;
- korzeń szuflady bierze się z zawartości ZIP-a (ZOD: `Zod-0.12.zip` ma szufladę `ZOD`).

Inne zmiany:
- **okno:** wysokość ok. 85% ekranu (do 860 px), galeria 66%; na 1920×1080 mieszczą się
  2 rzędy, reszta po przewinięciu;
- **strzałki w lewo / w prawo** wybierają grę i przewijają galerię do niej; w górę / w dół
  i kółko myszy przewijają;
- **kodowanie:** historia zmian AmiXcom po polsku jest w ISO-8859-2, nie w UTF-8.
  `hub_to_latin()` zamienia bajty spoza UTF-8 z polskich liter ISO-8859-2.

Sprawdzone na Amidze 2: 10 kafelków z obrazkami, przewijanie, wybór AmiXcom strzałkami,
strona z informacją testową, historia zmian z lokalnej kopii.

## Grupy i przyciski filtrowania (19.11.2026)

Prośba użytkownika: u góry, zamiast paska z liczbą gier, przyciski filtrowania. Grupy gier
ustawiamy sami: `games.json`, pole `tags` (jedna lub kilka), w katalogu linia
`TAGS pc,new`.

| Przycisk (PL / EN / DE) | Klucz | Klawisz |
|---|---|---|
| Wszystkie / All / Alle | – | 1 |
| Nowości / New / Neu | `new` | 2 |
| Programy / Programs / Programme | `apps` | 3 |
| WarpExtreme | `warpextreme` | 4 |
| Arcade | `arcade` | 5 |
| PC port / PC-Port | `pc` | 6 |
| MAC port / MAC-Port | `mac` | 7 |
| Inne / Other / Andere | `other` | 8 |

„Wszystkie” dodane, żeby dało się wrócić do pełnej listy. Pasek stanu z postępem, wybór
języka i „Odśwież” są teraz na dole okna. Grupa bez gier pokazuje „Na razie brak gier w tej
grupie.”.

Grupy przypisane na próbę (do ustawienia przez użytkownika):

| Grupa | Gry |
|---|---|
| `pc` | Settlers, OpenDUNE, Diablo, Syndicate Wars, AmiXcom, ZOD |
| `arcade` | Vastar, Xain'd Sleena, Wizard of Wor |
| `other` | Miner 2049er |
| `new` | Diablo, AmiXcom, ZOD |
| `apps`, `warpextreme`, `mac` | puste |

Sprawdzone na Amidze 2 (klawisze 1–8 przez AmiAgenta): Wszystkie (10), Arcade (3),
Programy (komunikat o pustej grupie), podświetlenie wybranego przycisku.

## Ekran 640×480 (19.11.2026)

Uwaga użytkownika: okno musi się mieścić w 640×480 (Workbench Amigi 2 jest teraz w tej
rozdzielczości).

Zmiany:
- **mały tryb** przy ekranie poniżej 1024×700: kafelki 144×90, obrazek skalowany przez
  datatype (`PDTM_SCALE` przed `DTM_PROCLAYOUT`). Na dużych ekranach bez zmian (224×140);
- **okno** szerokie na najwyżej 3 kafelki, ale nie szersze niż ekran − 16; na małym ekranie
  prawie cała wysokość;
- **przyciski filtrów** bez równej szerokości (`LAYOUT_EvenSize` nie mieścił się w 640);
- **strona gry** ma wysokość na 7 linii czcionki ekranu (8 na dużym);
- **na kafelku** krótki napis „Nowa wersja 0.48”, pełny „(masz 0.47)” na stronie gry;
- **strzałki:** układ galerii przeliczany przed ruchem (`update_layout()`). Wcześniej
  nieaktualna liczba kolumn przewijała galerię w złe miejsce.

Na 640×480 mieści się jeden rząd kafelków (3 gry), resztę widać po przewinięciu.
Sprawdzone na Amidze 2: okno mieści się w ekranie, strona gry i historia zmian czytelne,
wybór strzałkami przewija galerię do wybranej gry.

Uwaga użytkownika: pod kafelkami zostawało dużo pustego miejsca. Teraz galeria ma wysokość
dokładnie na całe rzędy (na małym ekranie 1 rząd, na dużym tyle, ile mieści się w 55% okna,
najwyżej 3) i nie rośnie; resztę wysokości dostaje historia zmian. Sprawdzone na 640×480
(Amiga 2).

## Ikona programu (19.11.2026)

`python tools/make_hub_icon.py amiga/icon/WarpHub.info --preview <png>`. Styl ikony
szuflady WARP (`AmigaGAMES/WARP-drawer.info`):
- **góra:** czerwono-biały znak W na niebieskim panelu, wzięty piksel w piksel z ikony
  szuflady;
- **dół:** gładki niebieski korpus z cieniowaniem krawędzi szuflady, napis „HUB”
  (biały z cieniem) i czerwona strzałka pobierania.

Budowa:
- 46×46, przezroczyste tło;
- ColorIcon z dwoma obrazami: zwykły oraz zaznaczony, ciemniejszy z żółtą obwódką jak
  szuflada;
- klasyczny obraz w 4 kolorach Workbencha;
- stos 65536, bez Tool Types.

`tools/icon_to_png.py` zamienia ColorIcon na PNG (podgląd na PC).

Sprawdzone na Amidze 2: ikona w szufladzie obok szuflad WARP, start z ikony otwiera okno.

### Ikona – wersja 2 (decyzja użytkownika 19.11.2026)

Użytkownik chce ikonę w układzie logo „…hub”: na czarnym tle napis WARP z logo WARP, obok
pomarańczowy zaokrąglony prostokąt z czarnym „HUB”.

`python tools/make_hub_icon.py amiga/icon/WarpHub.info` (domyślnie `--style logo`,
`--height 34`):
- napis WARP wycięty z `amiga/icon/src/warp-logo-002.png` (logo ze strony amigawarp.eu,
  wiersze 964–1312, przezroczystość z jasności liter);
- „HUB” czcionką Arial Black na pomarańczowym `#FFA31A`;
- rysowane 8× większe i zmniejszone;
- przezroczystość tylko pełna albo żadna, najwyżej 64 kolory;
- wymiary 107×36 (z ramką na obwódkę zaznaczenia).

Pierwsza wersja (szuflada WARP z „HUB” i strzałką) zostaje jako `--style drawer`.

Sprawdzone na Amidze 2: ikona w szufladzie `RAM:WarpHub-test` na Workbenchu 640×480.
Okno szuflady trzeba było otworzyć na nowo (ARexx `WINDOW ... CLOSE/OPEN`), bo Workbench
trzymał starą ikonę.

### Ikona – wersja 3 (decyzja użytkownika 19.11.2026)

WARP u góry, HUB na dole, ikona kwadratowa w rozmiarze ikon pakietu WarpApps
(`SYS:WarpApps`: 46×46 – 48×48, np. WarpDashboard 48×48 na czarnym zaokrąglonym kwadracie).

`python tools/make_hub_icon.py amiga/icon/WarpHub.info` (domyślnie `--style square`):
- czarny zaokrąglony kwadrat 46×46 z szarą krawędzią;
- napis WARP z logo na całą szerokość wnętrza;
- pod nim pomarańczowy prostokąt z czarnym „HUB”;
- całość 48×48 z ramką na żółtą obwódkę zaznaczenia.

Poprzednie układy zostają jako opcje: `--style logo` (obok siebie) i `--style drawer`.

Sprawdzone na Amidze 2: na Workbenchu obok otwartej szuflady WarpApps ikona ma ten sam
rozmiar.

Poprawka: „HUB” mniejsze (czcionka 24% zamiast 30% wysokości), pomarańczowy prostokąt tylko wokół napisu, wyśrodkowany.

## Układ pod galerią (19.11.2026)

Uwaga użytkownika: historia zmian ma być tak szeroka jak opis gry, a przyciski po prawej
jeden pod drugim.

Teraz:
- **lewa kolumna:** strona gry, a pod nią historia zmian, ta sama szerokość;
- **prawa kolumna, od góry:** Zainstaluj / Aktualizuj / Sprawdź pliki, Przerwij, Odśwież,
  język;
- **na dole:** pasek stanu na całą szerokość.

Historia zmian: linie akapitu (w plikach łamane na ~70 znakach) są sklejane przed zawijaniem
do szerokości listy (`reflow()`). Osobno zostają punkty `- `, wersje (`0.139 (...)`),
podkreślenia `===` / `---` i puste linie. Wcześniej zostawały pojedyncze słowa w osobnych
liniach.

Sprawdzone na Amidze 2 (640×480).

## Wymagania i „Katalog gier” (19.11.2026)

Uwaga użytkownika: obok opisu gry ma być ramka z wymaganiami, przyciski pod nią, oraz
przycisk „Katalog gier” do wybrania szuflady zainstalowanych gier.

### Ramka „Wymagania”
- `games.json`, pole `req`: krótkie linie PL/EN/DE, przepisane z rozdziałów
  REQUIREMENTS / WHAT YOU NEED opisów gier. W katalogu linie `REQ <język> <tekst>`;
- linia z dwukropkiem (Dane:, ROM-y:) jest pomarańczowa;
- gra bez `req`, ale z pustymi szufladami na dane, pokazuje ogólne „Potrzebne pliki
  oryginalnej gry”;
- uwaga o danych zniknęła ze strony gry.

### Przycisk „Katalog gier”
- requester ASL wybiera szufladę, która trafia do `WarpHub.prefs` jako `GAMESDIR`;
- WARP HUB szuka w niej `<szuflada>/<Gra>/<program>` każdej gry z katalogu (program to plik
  szuflady gry, którego ikona ma flagę K);
- wersję bierze z `$VER` największego programu gry, który ma `$VER`: OpenDUNE, a nie
  MakeMusic (1.2); devilutionx, a nie Hellfire (bez `$VER`);
- znalezione gry zapisuje jako `INSTALLED`;
- requester instalacji zaczyna w tej szufladzie (nadal pyta zawsze).

### Prawa kolumna, od góry
Wymagania, Zainstaluj / Aktualizuj / Sprawdź pliki, Przerwij, Katalog gier, Odśwież, język.

### Testy na Amidze 2
- Opcja testowa z Shella `TESTGAMESDIR=<szuflada>` przeszukuje szufladę bez requestera.
- `RAM:WarpHub-test`: OpenDUNE 0.54 i Diablo 0.48 rozpoznane z `$VER`.
- Program OpenDUNE w tej szufladzie był uszkodzony po teście błędnej sumy. Naprawił go
  `WarpHubCLI INSTALL` z GitHuba: 1 plik, 472 KB, 1,6 s.
- `catalog.txt` (3 publiczne gry) i `catalog-test.txt` (10 gier) mają linie `REQ`.

### Sprawdzanie gier przy każdym starcie (19.11.2026, prośba użytkownika)
- Po wczytaniu katalogu (z pamięci, a przy pierwszym starcie po pobraniu) WARP HUB
  przeszukuje znów `GAMESDIR` i każdą szufladę z linii `INSTALLED`. W tym czasie tytuł okna
  pokazuje „Szukam zainstalowanych gier...”.
- Gra znaleziona: wersja z `$VER` jest zapisywana (gracz mógł zaktualizować grę ręcznie).
- Gry nie ma w swojej szufladzie: linia `INSTALLED` znika (gra usunięta albo przeniesiona).
- Szuflady w ogóle nie ma (dysk niezamontowany): nic nie jest zmieniane.
- `$VER` jest czytany kawałkami po 32 KB do pierwszego wystąpienia, a nie cały program naraz
  (Settlers ma 3,2 MB, Diablo kilka MB).
- Test na Amidze 2 (start przez `WBStart`, jak z ikony), w `WarpHub.prefs` celowo złe dane:
  - OpenDUNE 0.40 → 0.54;
  - Settlers 0.100 → 0.139;
  - Vastar (nie ma go w szufladzie): usunięty;
  - ZOD w nieistniejącym `RAM:NotThere`: zostawiony.
- Plik ustawień był zapisany ok. 2 s po poleceniu startu, razem z otwarciem okna. Mierzone
  na `RAM:`; na karcie SD albo dysku będzie wolniej, jeszcze nie mierzone.

### Osobna szuflada programów (19.11.2026, prośba użytkownika)
- Przycisk „Katalog programów” pod „Katalog gier” zapisuje `APPSDIR` w `WarpHub.prefs`.
- Pozycje z grupy Programy (znacznik `apps`) proponują instalację w tej szufladzie, a gry
  w `GAMESDIR`. Requester nadal pyta zawsze.
- Przy starcie i po wybraniu przeszukiwane są obie szuflady.
- Na Amidze 2 (640×480) okno z dodatkowym przyciskiem mieści się bez zmian układu.
- W katalogu nie ma jeszcze żadnej pozycji `apps`, więc instalacji programu nie sprawdzono.

### Dwa przyciski: Zainstaluj i Aktualizuj (19.11.2026, prośba użytkownika)
- Wcześniej był jeden przycisk ze zmieniającym się napisem (Zainstaluj / Aktualizuj /
  Sprawdź pliki). Teraz są dwa stałe:
  - „Zainstaluj” jest czynny, gdy gry nie ma;
  - „Aktualizuj” jest czynny, gdy gra jest i katalog ma nowszą wersję.
- Gdy gra jest aktualna, oba są wyłączone. „Sprawdź pliki” dla aktualnej gry zniknęło
  z okna; `WarpHubCLI CHECK` / `INSTALL` nadal to umie.
- Test na Amidze 2: w testowym katalogu Diablo 0.49 przy zainstalowanej 0.48 →
  „Aktualizuj” czynny, „Zainstaluj” wyłączony; Settlers i OpenDUNE aktualne → oba
  wyłączone. Katalog testowy przywrócony.

### Instrukcja uruchomienia (19.11.2026, prośba użytkownika)
- Pierwszy przycisk w prawej kolumnie, „Instrukcja uruchomienia” (EN „How to start”, DE
  „Startanleitung”), także klawisz Help. Otwiera osobne okno z przewijanym tekstem:
  - jest `<Gra>/doc/Install.txt` → ten plik (Settlers, OpenDUNE);
  - nie ma → pliki `ReadMe.txt` z pustych szuflad, każdy z nagłówkiem „Do szuflady
    `<Gra>/data`:” (Diablo, gry symulowane);
  - gra bez takich plików → „Nic nie trzeba dokładać…”.
- Okno: przycisk Zamknij, gadżet zamknięcia albo Esc; strzałki i kółko myszy przewijają.
  Główne okno w tym czasie czeka (zajęty wskaźnik), a kliknięte w nim rzeczy są pomijane.
- Po instalacji, gdy gra potrzebuje danych, okno otwiera się samo. Zastępuje requester
  z listą plików `ReadMe.txt`.
- Pliki są pobierane razem z historią zmian do `PROGDIR:cache/guide/<klucz>_<n>.txt`, ze
  sprawdzeniem SHA-256 (`hub_guide_files()` w `hub_catalog.c`, ta sama reguła w
  `make_catalog.py`). Gry symulowane: `catalog/notes/<klucz>_guide<n>.txt`.
- Linie „Dane: …” / „ROM-y: …” zniknęły z ramki Wymagania (`games.json`, oba katalogi).
  Mówi o tym instrukcja.
- Test na Amidze 2: 9 plików instrukcji pobranych przy starcie; okna Diablo (dwa ReadMe)
  i OpenDUNE (`Install.txt`) otwarte klawiszem Help, Esc zamyka. Pierwsza wersja łamała
  linie za wąsko (miara czcionką okna zamiast czcionką ekranu), poprawione.
- Do rozważenia: `Install.txt` jest w trzech językach naraz (EN, PL, DE) i zaczyna się od
  rozpakowania ZIP-a, czego przy WARP HUB nie trzeba. Można przewijać od razu do części
  w języku programu albo dodać w portach krótszy tekst tylko dla WARP HUB.

### Historia wydań tej samej wysokości co przyciski (19.11.2026, prośba użytkownika)
- Z prawej kolumny zniknął pusty odstęp pod wyborem języka. Okno otwiera się najniższe, na
  jakie pozwala układ (`WA_InnerHeight 50`; ReAction powiększa je do minimum).
- Historia wydań zaczyna się na wysokości „Instrukcji uruchomienia” i kończy na wysokości
  wyboru języka. Wysokość galerii nadal liczy `win_h`.
- Amiga 2 (640×480): okno o kilka linii niższe niż ekran, góra i dół obu kolumn równo.
- Przy okazji: w `WarpHub.prefs` użytkownik wybrał `GAMESDIR WorkSD:Games` i
  `APPSDIR WorkSD:`. Sprawdzanie przy starcie rozpoznało tam wszystkie 10 gier, z wersjami
  z `$VER`, m.in. Xain'd Sleena 0.9.17, AmiXcom 0.106, ZOD 0.12. Gra zainstalowana w dwóch
  miejscach ma w ustawieniach tylko jedno: to, które sprawdzono jako ostatnie.

### Język zapamiętany przy zamknięciu (19.11.2026, prośba użytkownika)
- Język był już zapisywany przy każdej zmianie w liście wyboru. Teraz `WarpHub.prefs` jest
  zapisywany jeszcze raz przy zamknięciu programu (Esc, gadżet zamknięcia, Ctrl-C), razem
  z bieżącym językiem.
- Test na Amidze 2: kopia w `RAM:WarpHub-lang`, ustawienia bez linii `LANGUAGE`, start
  `LANG=de`, zamknięcie przez Ctrl-C → w pliku `LANGUAGE de`. Zmiany w liście wyboru języka
  nie da się sprawdzić bez myszy.

## Bez paska stanu (19.11.2026)

Uwaga użytkownika: ramka z liczbą gier jest niepotrzebna, więc ją usunąłem. Stan pokazuje
tytuł okna:
- w czasie pracy postęp, np. „WARP HUB - Pobieranie Settlers/Settlers (47%)”;
- po zakończeniu wynik, np. „WARP HUB - The Settlers 0.139 zainstalowana w ...”;
- brak połączenia: komunikat o błędzie;
- w spoczynku samo „WARP HUB”.

Pułapka: po usunięciu paska zostało jego `CHILD_WeightedHeight, 0` i trafiło do dolnej części
okna. Wszystkie wiersze miały wtedy wagę 0, ReAction rozkładał wolne miejsce na przerwy nad
i pod galerią. `LAYOUT_DeferLayout` też usunięty, więc okno układa się od razu.

Sprawdzone na Amidze 2 (640×480): układ bez przerw, postęp w tytule przy instalacji
Settlers z okna.

## Repo publiczne (19.11.2026)

- Użytkownik upublicznił `cizar-szymon/Amiga_WarpHub`. Przed tym: `CLAUDE.md` (adresy sieci
  domowej) tylko na dysku, historia przepisana bez niego, opisy bez dawnego serwera.
- PC bez logowania: `catalog/catalog.txt` (37 289 B, końce linii LF) i obrazki z `raw` – 200.
- Amiga 2, `WarpHubCLI LIST` z domyślnym adresem: 3 gry (Settlers 0.139, OpenDUNE 0.54,
  Diablo 0.48), katalog w 980 ms, TLS 1.3 AES-128-GCM, czas z NTP (zegar Amigi 1978).

## Pliki gier, automat i edytor w przeglądarce (19.11.2026, prośba użytkownika)

- `catalog/games.json` zastąpiony plikami `catalog/games/<klucz>.txt`, po jednym na grę
  (format w `tools/game_files.py`; `ORDER` = miejsce w galerii, `LOCAL` i `IMAGE-SOURCE`
  tylko dla PC). `make_catalog.py` i `make_images.py` czytają katalog. Po przejściu
  `catalog.txt` i `catalog-test.txt` identyczne jak wcześniej, poza nagłówkiem (bez daty,
  żeby automat nie robił commitu codziennie). Testy: 13 (4 nowe dla plików gier).
- Amiga nadal czyta jeden `catalog.txt`. Odczyt katalogu z wielu plików odrzucony, bo
  `raw.githubusercontent.com` nie pokazuje listy katalogu (trzeba by API z limitem 60/h na IP),
  każdy plik to osobne pobranie, a listy plików z sumami i tak robi program.
- `.github/workflows/catalog.yml`: po zmianie w `catalog/games/`, `catalog/images/` albo
  narzędziach, codziennie o 4:17 UTC i ręcznie. Kroki: testy, `make_images.py --normalize`
  (każdy PNG do 224×140 / 256 kolorów, poprawne zostają), `make_catalog.py`
  z `GITHUB_TOKEN`, commit bota tylko przy zmianie. Akcje przypięte do SHA.
- `.github/workflows/pages.yml`: `editor/` na GitHub Pages.
- Edytor `editor/index.html` + `editor.js` (PL, bez bibliotek):
  - lista gier z API, formularz: klucz, miejsce, nazwa / opis / wymagania PL/EN/DE, grupy,
    repo i gałąź („Sprawdź repo”: publiczne? jest `releases/amiga/<Gra>-<v>.zip`?), obrazek
    z pliku (wypełnij / zmieść / piksele) na płótnie 224×140;
  - zapis: jeden commit przez Git Data API (plik gry + obrazek), usuwanie tak samo;
  - token fine-grained tylko do tego repo (Contents: Read and write), w `sessionStorage`
    albo, po zaznaczeniu, w `localStorage`; CSP pozwala łączyć się tylko z `api.github.com`.
- Test edytora (Edge bez okna, bez sieci): odczyt i zapis w JavaScripcie dają te same
  10 plików co Python. Formularz zmienia 4 pliki nieszkodliwie: pomija `NAME pl` równe
  angielskiej, grupy w stałej kolejności. Złe dane dają komunikaty. Zapis na GitHub
  sprawdzony dopiero po wdrożeniu (niżej).


### WARP HUB z katalogiem z GitHuba (19.11.2026)
- Amiga 2 (ekran 1280×1024), nowa kopia w `RAM:WarpHub-lang`, ustawienia bez `CATALOG`, start
  przez `WBStart`. W 25 s pobrane z GitHuba: `catalog.txt` (37 540 B, z taktowaniem
  w wymaganiach), 3 obrazki, 6 historii zmian i 4 pliki instrukcji. W galerii 3 publiczne gry.
- Sprawdzanie przy starcie rozpoznało w `WorkSD:Games` Settlers 0.139, OpenDUNE 0.54
  i Diablo 0.48 („Zainstalowana”, oba przyciski wyłączone).
- Na dużym ekranie galeria ma 3 rzędy, choć gry mieszczą się w jednym: pusty obszar pod
  kafelkami (do poprawy: wysokość galerii według liczby gier).

### Przyciski filtra: nowa kolejność, bez Nowości (19.11.2026, decyzja użytkownika)
- Wszystkie, Programy, AMIGA port (`amiga`), PC port, MAC port, WarpExtreme, Arcade, Inne;
  w programie (PL/EN/DE, klawisze 1–8) i w edytorze. Znacznik `new` usunięty z Diablo,
  AmiXcom i ZOD. Amiga 2 (1280×1024): przyciski w tej kolejności. 640×480 nie sprawdzone.

### Przyciski filtra według kart Warp (19.11.2026, decyzja użytkownika)
- Wszystkie, Programy, Warp 560/1260 (`warp1260`), Warp 1240 (`warp1240`), Warp 1260X
  (`warp1260x`), Warp 7060 (`warp7060`), PowerPC (`ppc`), Inne (`other`). Typy z platform
  (AMIGA/PC/MAC port), Arcade i WarpExtreme usunięte.
- Wstępnie według wymagań: wszystkie gry 560/1260 i 7060; 1240: Settlers, OpenDUNE, AmiXcom,
  ZOD, Miner 2049er, Wizard of Wor. 1260X i PowerPC puste (decyzja użytkownika w edytorze).

### Nowe gry z katalogu rozpoznawane po „Odśwież” (19.11.2026, zgłoszenie użytkownika)
- Błąd: szuflady gier były przeszukiwane raz, przy starcie, według katalogu z pamięci. Gra,
  która doszła do katalogu dopiero po odświeżeniu (Vastar), była „do pobrania”, choć leżała
  w `WorkSD:Games/Vastar` (`Version`: Vastar 0.1).
- Teraz po każdym odświeżeniu przeszukiwane są szuflady dla gier bez linii `INSTALLED`
  (`scan_games_dir(..., only_new)`; nic nie jest wtedy zapominane). Przy pierwszym starcie
  bez katalogu w pamięci pełne przeszukanie po pobraniu.
- Test Amiga 2, kopia `RAM:WarpHub-f`: w pamięci stary katalog (3 gry), start → pobrany
  nowy (5 gier) → w ustawieniach doszły `vastar 0.1` i `xainsleena 0.9.17`.
- Uwaga: `raw.githubusercontent.com` trzyma pliki do 5 minut (`Cache-Control: max-age=300`,
  parametr w adresie tego nie omija) – zmiana z edytora jest na Amidze najpóźniej po 5 min.
- Automat katalogu co godzinę (wcześniej raz dziennie): upublicznienie repo gry albo nowe
  wydanie nie zmienia plików WarpHub i nie uruchamiało go (Syndicate Wars brakowało do
  ręcznego uruchomienia). Nowa wersja WARP HUB w `SYS:WarpApps`.
- Syndicate Wars: w gicie 52 pliki wydania miały LF zamiast CRLF z ZIP-a, w tym 21
  `alltext-*.wad`, do których `alltext-*.idx` trzyma pozycje dla CRLF. Teksty gry
  zainstalowanej z gita (WARP HUB) byłyby poprzesuwane. Poprawka w repo gry (`14fa4215`,
  gałąź `amiga`): `.gitattributes` `releases/** -text`, pliki zapisane jak w ZIP-ie.
  Katalog: 108 z 108 plików Syndicate Wars zgodnych z `SyndWars-0.220.zip`.

### Zatwierdzona wersja dla gier z cudzych repo (19.11.2026, decyzja użytkownika)
- Pole `APPROVED <wersja> <commit>` w pliku gry: `make_catalog.py` bierze wtedy listę,
  ZIP i drzewo gita z tego commita i zapisuje w katalogu `BRANCH <commit>`. Amiga pobiera
  z `raw.githubusercontent.com/<repo>/<commit>/...`, więc nowsze wydanie autora nie trafia
  do graczy, dopóki ktoś go nie zatwierdzi. Gdy w commicie jest inna wersja niż zatwierdzona,
  gra jest pomijana z ostrzeżeniem. Bez pola: zawsze najnowsze (własne gry).
- Edytor: „Zatwierdź obecne wydanie” (najnowszy ZIP i commit gałęzi w tej chwili),
  „Zawsze najnowsze”; „Sprawdź repo” pokazuje „autor wydał X, zatwierdzona Y”.
- Testy: 17 (2 nowe dla `APPROVED` na fałszywym GitHubie, 1 dla złego commita). Edytor
  w Edge z prawdziwym API: Vastar zatwierdzony jako 0.1 / `e1082c5`. Katalog z tym polem:
  `BRANCH e1082c5…`. Amiga 2: `WarpHubCLI INSTALL vastar` z tego katalogu do
  `RAM:WarpHub-pin` – 11 plików, 189 KB w 3,5 s, `Version`: Vastar 0.1.
- Przy okazji: `WarpHubCLI` z katalogiem z pliku (`CATALOG=RAM:…`) nie pyta NTP, więc przy
  zegarze z 1978 instalacja kończy się błędem certyfikatu; z `USENTP` działa.

### ZOD z gałęzi release (19.11.2026, decyzja użytkownika: wariant A)
- Wydanie ZOD jest poza gitem na gałęziach kodu (dane silnika, jak u Cego) i w GitHub
  Releases (`v0.12`). Teraz także na osobnej gałęzi `release` repo
  `cizar-szymon/Amiga_Game_Zod`, bajt w bajt: skrypt w projekcie ZOD
  `tools/release/push_release_branch.sh` (`* -text`, bez zmian w drzewie roboczym).
  ZIP z dysku = ZIP z Releases; 105 z 105 plików szuflady zgodnych z ZIP-em.
- `catalog/games/zod.txt`: `REPO cizar-szymon/Amiga_Game_Zod` (w edytorze wpisało się
  z kropką na końcu), `BRANCH release`.
- Amiga 2: `WarpHubCLI INSTALL zod` do `RAM:WarpHub-pin` – 14,3 MB w 47,7 s (307 KB/s),
  `Version`: Zod 0.12; ReadMe pustych szuflad cuts/, music/, packs/.

### Zegar Amigi z przyszłości; nadpisanie z edytora (19.11.2026 wieczorem)
- Zegar Amigi 2 wskazywał nagle rok 2094 (`Date`: 08-Dec-94). WARP HUB pytał serwer czasu
  tylko przy zegarze wcześniejszym niż data zbudowania, więc wziął 2094 i OpenSSL odrzucił
  certyfikat („format error in certificate's notBefore field”). Oba okna pokazywały katalog
  z pamięci. Teraz zawsze najpierw serwer czasu; zegar Amigi tylko, gdy żaden nie
  odpowiada i data jest między datą zbudowania a +10 lat (`HUB_CLOCK_OK`). Program do
  Shella tak samo. Amiga 2: `WarpHubCLI LIST` przy zegarze 2094 – katalog pobrany.
- ZOD znikł z katalogu: zapis w edytorze z formularza otwartego przed moją zmianą usunął
  `BRANCH release`. Przywrócone. Edytor przed zapisem porównuje teraz SHA pliku na GitHubie
  z SHA z chwili otwarcia i przy różnicy nic nie zapisuje.

### Wydania gier w gicie bajt w bajt jak ZIP (19.11.2026)
- W repo Settlers (`amiga-port`), OpenDUNE, Diablo, Vastar i Xain'd Sleena dopisane
  `releases/** -text` w `.gitattributes`, pliki wydania zapisane jak w ZIP-ie: Diablo 60
  plików (`.tsv`, `.txt`, `.lua`, `.ini` z CRLF), OpenDUNE `COPYING` + brakujący w gicie
  `data/ReadMe.txt` (ignorowany przez `data/` w `.gitignore`, dodany `-f`), Settlers
  `micromod-licence.txt`, Vastar wersja PC (4 pliki, zgodne z `Vastar-0.1-pc.zip`).
  Settlers przez osobny katalog roboczy `amiga-port` (gałąź użytkownika `lan-observer`
  nietknięta).
- Katalog po przebudowie: wszystkie 7 gier, każdy plik z sumą równą plikowi w ZIP-ie
  (27 + 22 + 251 + 108 + 11 + 11 + 105). Generator nie musi już brać „wersji z gita”
  zamiast ZIP-a.

## WARP HUB 0.3: wydanie i aktualizacja samego siebie (19.11.2026)
- Decyzja użytkownika: własna szuflada `SYS:WarpApps/WarpHub/` (program, ikona,
  `WarpHub.prefs`, `cache/`, `doc/`), w `SYS:WarpApps` ikona szuflady WARP.
- `tools/make_release.py`: `releases/amiga/WarpHub-<v>.zip`, `WarpHub/` (program, ikona,
  `doc/` z opisami PL/EN/DE i historią wydań, ikony tekstów z OpenDUNE), `WarpHub.info`
  (`WARP-drawer.info`); sprawdza `$VER`. Źródła tekstów: `docs/release/amiga/`.
- Katalog: `catalog/games/warphub.txt` (klucz `warphub`, grupa Programy, `ORDER 1`), obrazek
  z ikony. Automat katalogu rusza też po zmianie w `releases/`. `.gitattributes`:
  `releases/** -text`.
- Program: `note_self()` – uruchomiony z szuflady `<dir>/WarpHub` zapisuje się jako
  zainstalowany `warphub` w `<dir>`; po aktualizacji samego siebie okno „zamknij i uruchom
  ponownie”. Stary program idzie do `old/` jak u gier.

## WARP HUB 0.4: program prosto w SYS:WarpApps (19.11.2026, decyzja użytkownika)
- Użytkownik chce sam program z ikoną w `SYS:WarpApps`, jak WarpDiag, bez szuflady.
- Katalog: linia `FLAT` (plik gry `FLAT`): pliki z `<Root>/` idą prosto do wybranej
  szuflady, reszta (ikona szuflady, `doc/`) ma znacznik `X` – pobierana tylko do okna
  (historia, instrukcja), nie instalowana. `SIZE` bez plików `X`.
- Amiga: `hub_local_path()`, plan i instalacja pomijają `X`; stary program przy FLAT jako
  `<program>.old`; pamięć podręczna `PROGDIR:WarpHub-cache`; `note_self()` zapisuje
  zainstalowany `warphub` w szufladzie programu. Nieznany znacznik pliku = `X` (0.3
  odrzucało cały katalog przy nieznanym znaczniku).
- Opisy i historia wydań 0.4 (PL/EN/DE), edytor zachowuje `FLAT`.

## WARP HUB 0.5: przycisk Uruchom (19.11.2026, pomysł użytkownika)
- Ostatni wiersz prawej kolumny: „Uruchom” i wybór języka obok siebie (`LAYOUT_EvenSize`).
  Czynny dla zainstalowanej gry (nie dla samego WARP HUB). Uruchamia największy program
  szuflady gry, którego ikona jest plikiem gracza, przez `OpenWorkbenchObjectA`
  (workbench.library 44) – jak dwuklik: Tool Types i stos z ikony. Klawisz Return też.
- Amiga 1: kopia testowa w `RAM:WarpHub-run`, Vastar wybrany strzałkami, Return →
  okno Vastar z ekranem tytułowym, tytuł WARP HUB „Starting Vastar...”. Esc zamknął grę.
- **Aktualizacja samego siebie sprawdzona (Amiga 1, użytkownik klikał):** WARP HUB 0.4 →
  „Update” → `SYS:WarpApps/WarpHub` 0.5 (`Version`), `WarpHub.old` = 0.4, ikona i
  `WarpHub.prefs` bez zmian, w ustawieniach `INSTALLED warphub 0.5 SystemSD:WarpApps`.

## Do zrobienia: prywatne źródło pełnych paczek (pomysł użytkownika 19.11.2026)
- Cel: na Amigach użytkownika WARP HUB instaluje także **pełne paczki** z danymi gier
  (`private/<Gra>-<v>-full.zip` z projektów gier), których nie wolno publikować.
- Kod jawny i ogólny: drugie, prywatne źródło katalogu włączane tylko liniami w
  `WarpHub.prefs` na Amigach użytkownika (np. `PRIVATE <adres https>/catalog.txt`,
  `PRIVATE-KEY <losowy klucz>`); bez nich program działa jak dziś. W kodzie i repo żadnego
  adresu ani klucza; żadnego ukrywania w kodzie („tajny klawisz” nic nie chroni).
- **Serwer: osobny Raspberry Pi 4** (nie Pi400 – serwer gier), na publicznym IP
  użytkownika z **przekierowanym portem** na routerze (ustawia użytkownik). HTTPS
  (certyfikat, np. Let's Encrypt, albo własne CA wbudowane do sprawdzania), pliki tylko z
  poprawnym kluczem, katalog w tym samym formacie co publiczny (rozpakowane pełne paczki,
  SHA-256). Generator katalogu z `private/` na PC, wysyłka na RP4.
- Ostrożnie: dane gier tylko dla Amig użytkownika; serwer niewidoczny bez klucza, limity
  prób, log. Wdrożenie na RP4 i router tylko za zgodą użytkownika (adres RP4 – zapytać).
- **Hasło w okienku (decyzja użytkownika 19.11.2026), sprawdzane przez serwer:**
  - WARP HUB pyta o hasło w okienku (raz po uruchomieniu, gdy jest `PRIVATE` w ustawieniach)
    i wysyła je przez HTTPS do serwera na RP4; program nie wie, jakie hasło jest dobre –
    w kodzie ani hasła, ani jego skrótu, ani adresu (adres tylko w `WarpHub.prefs`).
  - Serwer porównuje (skrót hasła u siebie), wydaje katalog i pliki; po kilku złych próbach
    blokada adresu IP na pewien czas, wpis w logu.
  - Hasło trzymane tylko w pamięci do zamknięcia programu; opcja „zapamiętaj na tej Amidze”
    (zapis w `WarpHub.prefs`) dla wygody na maszynach użytkownika.
  - Odrzucone: hasło sprawdzane przez sam program (jawny kod – do obejścia w kilka minut).

### Serwer na RP4 (23.11.2026)
- RP4 (Debian 13, Python 3.13), po kablu; WiFi wyłączone (`nmcli radio wifi off`). Adres
  tylko w pamięci agenta / `CLAUDE.md`, nie w repo.
- **`tools/push_private.sh user@host [--no-www | --full]`** (adres z linii poleceń albo `PRIVATE_HOST`):
  wysyła `private_server.py`, `cert.pem`, `key.pem`, `server.cfg` (z `WarpHub-private` na PC)
  i `www/` do `~/warphub-private` (nowe `www` obok starego, podmiana jednym `mv`), robi
  `run.sh` (restart serwera po 10 s, gdy padnie) i wpis `@reboot` w crontabie użytkownika –
  bez roota (port 6831 > 1024, `sudo` nie jest potrzebne). Na koniec restart i sprawdzenie.
- Certyfikat i hasło przeniesione z PC: odcisk `2FCB 9FDF 6356 5006…` taki sam, Amigi,
  które logowały się do serwera na PC, zmieniają tylko adres.
- Sprawdzone z PC: `www/` na RP4 = PC (209 plików, SHA-256 każdego), bez hasła i ze złym
  hasłem 401, zły login w `server.log`.
- Po nowym wydaniu gry: `python tools/make_private.py`, potem `sh tools/push_private.sh …`.
- **Od 25.11.2026 tylko zmienione pliki** (`tools/private_sync.py`, życzenie użytkownika: cała
  `www/` to 2,5 GB przy jednej zmienionej grze). Lista `www/` z serwera (rozmiar, czas – `tar`
  zachowuje czasy, `make_private.py` nie rusza niezmienionych plików), na serwerze `www.new` =
  `cp -al www` (dowiązania twarde), nowe i zmienione pliki strumieniem `tar` z `--unlink-first`
  (plik w starym `www/` zostaje jak był – trwające pobieranie czyta go do końca), usunięte pliki
  usuwane, podmiana jednym `mv` jak dotąd. `--full`: cała `www/` jak wcześniej; `private_sync.py
  … --dry`: tylko co by poszło. Sprawdzone 25.11.2026: po pełnej wysyłce 0 z 2597 plików do
  wysłania; plik testowy dodany → 1 plik, usunięty → zniknął z serwera; `push_private.sh` razem
  z restartem 7,7 s (wcześniej kilka minut).
- WARP HUB z kłódką = **0.6** (numer podniesiony 23.11.2026, wydany jako **0.7** 24.11.2026 w
  `releases/`); wgrany na Amigę 1 do `SYS:WarpApps/WarpHub`, poprzedni jako `WarpHub-0.5`.
- Test 24.11.2026 (Amiga 1, WARP HUB 0.6): logowanie kłódką przez **adres publiczny**
  przeszło (przekierowanie portu działa; w logu jako adres routera – złe hasła z sieci
  domowej przez adres publiczny liczą się wspólnie), „Private source: 4 full packages”.
- Pełny Vastar z RP4 (przez adres publiczny) zainstalowany na Amidze 1 w `Work:Games`:
  30 plików (program, doc, 19 ROM-ów) pobranych z Amigi bajt w bajt jak na serwerze,
  `Version` = Vastar 0.1, program `rwed`.
- **0.7:** „Odśwież” sprawdza od nowa wszystkie szuflady gier (także bez sieci) – gra
  usunięta przy otwartym WARP HUB nie jest już pokazywana jako zainstalowana (w 0.6 tylko
  po ponownym starcie programu; zgłoszenie użytkownika: usunięty Vastar dalej zielony).
- **Hasła dodatkowe (kody), 24.11.2026, decyzja użytkownika: tylko dla jego Amig.** Obok
  hasła głównego kody z nazwą: na wybrane gry (`--game`, można kilka) i / albo na czas
  (`--days` / `--hours`). Na PC:
  ```
  python tools/private_server.py code amiga2-vastar --game vastar
  python tools/private_server.py code dzien --days 1
  python tools/private_server.py codes            # lista
  python tools/private_server.py revoke NAZWA     # albo revoke --expired
  sh tools/push_private.sh <user@host> --codes    # tylko codes.json, bez restartu
  ```
  - Kod wypisywany tylko raz; w `codes.json` sam skrót (PBKDF2, wspólna sól kodów – jedno
    PBKDF2 na logowanie). Kod bez gry i bez czasu nie istnieje (to jest hasło główne).
  - Kod na gry: katalog tylko z tymi grami (WARP HUB pokazuje „1 full package”, reszta
    z publicznego katalogu), inne pliki i obrazki 403. Kod po terminie: 401 „code expired”,
    nie liczy się do blokady (to nie zgadywanie). Serwer czyta `codes.json` po zmianie.
  - Log: `login <ip> code <nazwa>`, `files <gra> <ip> code <nazwa>` (raz na grę na
    logowanie), `expired code <nazwa> <ip>`.
  - Testy: `tools/tests/test_private_server.py` (prawdziwy serwer na PC, 8 testów). Na RP4:
    kod na Vastara – katalog tylko `vastar`, plik Vastara 200, OpenDUNE 403; kod 1 h – 4 gry;
    po `revoke` + `--codes` 401 bez restartu.
- Do zrobienia: test z Amigi (logowanie kłódką, instalacja pełnej gry), przekierowanie
  portu na routerze (użytkownik) i test z zewnątrz, stały adres RP4 w DHCP routera.

## 0.8: Odinstaluj (24.11.2026)
- Przycisk **Zainstaluj** jest dla zainstalowanej gry (także w starej wersji) przyciskiem **Odinstaluj**, po osobnym pytaniu
  „Odinstaluj|Anuluj”. WARP HUB sam siebie nie odinstalowuje. `hub_uninstall()` w `hub_install.c` usuwa **tylko pliki z katalogu
  gry** (każda ścieżka sprawdzona `hub_path_ok`), potem szuflady, które opustoszały; zapisy, `bugNNN.*`, `old/` i obce pliki
  zostają (komunikat mówi, że szuflada gry została). Bez zalogowania na kłódce usuwa tylko pliki publiczne – dane z prywatnego
  źródła zostają w `data/`. `WarpHubCLI UNINSTALL <gra> TO=<szuflada>` do testów.
- Test (Amiga 2, `RAM:UTEST`): instalacja TD, dopisany `user/bug001.txt` i obcy plik, UNINSTALL: 16 plików skasowanych
  (tyle co w katalogu), `TiberianDawn.info`, `doc/`, `data/` zniknęły, `user/bug001.txt` i obcy plik zostały. Okno
  (nazwa przycisku, pytanie, kliknięcie) sprawdza użytkownik – AmiAgent nie ma myszy.
- Znane: przy „Aktualizuj” gry z dużymi danymi (490 MB) WARP HUB kilka minut liczy sumy SHA-256 i pokazuje tylko
  „Sprawdzanie plików…” (wygląda jak zawieszenie); postęp sprawdzania do dodania.

## 0.9: every process says what it does (24.11.2026)
- Rule of the user (24.11.2026, for all programs and games, also in `PORTING_PLAN.md`): every longer process is described on the bar of the window, so one can see that the program works.
  Found when "Aktualizuj" of a game with 490 MB of data showed only "Sprawdzanie plikow ..." for 7 minutes.
- `hub_plan_progress()` (hub_install.c): the check of the files calls back about every 256 KB of the file that is read and at the start of every file (file, number of count, bytes of all); `hub_plan()` is the same without a callback
  (WarpHubCLI). The Stop button / Ctrl-C (`hub_break`) ends it (plan.files = -1, the job says "Stopped.").
- The process of the network (`hub_worker.c`) gives it to the window in `chk_index`, `chk_count`, `file`, `done`, `total` (+ `checking`); the refresh does the same for the pictures and texts of the games (`loading_games`). `worker_news()`
  in `warphub.c` writes "Checking files: N of M, FILE (xx%)" / "Loading pictures and texts: N of M (game)" and moves the progress bar. A stop by the user is "Stopped." without a requester (`T_STOPPED`).
- Not needed: the removal of a game (a few files, instant), the scan of the drawers (it has its own text).
- Tested by the user on Amiga 2 (`RAM:WarpHub09`): looks good.

## 0.10: clean up old versions and bug reports (25.11.2026)
- Request of the user: "Odswiez" also checks the chosen, installed game for old versions and for bug reports and offers to delete them, in a window of its own (not in the main window).
- `hub_clean_list()` / `hub_clean_delete()` (hub_install.c): the items are the entries of `<Game>/old/` and the bug reports of `<Game>/user/` (drawers `bugNNN` and loose files `bugNNN.*` of the older releases of the
  C&C ports; the name must be "bug" + at least three digits, so `bugemail.txt` and the saved games are never touched). Programs in a shared drawer (`flat`, WARP HUB itself) have none.
  Deleting lists the drawer again after every entry (deleting while listing confuses ExNext). `WarpHubCLI CLEAN <game> TO=<drawer>` lists, `CLEANALL` also deletes (tested on Amiga 2 in RAM:).
- Window (`show_cleanup()` in `warphub.c`, a window of its own like the guide): the list (heading with the number of files and the size, then every item with its size), the buttons "Delete old versions",
  "Delete bug reports", "Close"; every deletion asks first (the bug reports cannot be restored) and the list and the buttons are updated. It opens only after "Odswiez" pressed by the user (`clean_after_refresh`), not at the start.
- `ask()` uses the ReAction `requester.class` (47.9 on Amiga 2) so that every question opens in the middle of the WARP HUB window; `EasyRequest` opened at the top left corner of the screen. Fallback to `EasyRequest` when the class is missing;
  a `%` in the body text is doubled (the body is a format string there).
- Tested by the user on Amiga 2 (`RAM:WarpHub09`): works.

## 0.11: faster start - remembered versions and sums, ETag (25.11.2026, request of the user)
What the start did (0.10): cached catalog -> every game program read up to `$VER` (7.8 MB on Amiga 2:
Diablo 3.4 MB, Red Alert 2.5 MB without `$VER`) -> refresh (whole public catalog 77 KB, whole private
catalog 300 KB, every picture / history / guide of the cache read and SHA-256 checked) -> every
program read for `$VER` once more. The catalog grows 7-8 KB per game (a line per file).
- `$VER` remembered with the size and the date of the program (`WarpHub-cache/versions.txt`,
  `HubStamps` in `hub_util.c`): read again only after a change.
- Cache files: size + date + SHA-256 in `WarpHub-cache/sums.txt`; checked by SHA-256 only when
  the size or the date changed (or the catalog has another SHA-256).
- The drawers are looked at again after a refresh only when the catalog changed (FNV-1a of the
  text, `cur_sum` / `scanned_sum`) or the user pressed Refresh (also without the network).
- ETag: `If-None-Match` for the public catalog (GitHub answers 304) and the private one
  (`private_server.py` sends an ETag = SHA-256 of the catalog as sent, filtered for a code);
  ETags in `WarpHub-cache/etags.txt`, the private catalog in `WarpHub-cache/private.txt` (read only
  after a login answered 304). `hub_https`: no body for 304 / 204 / 1xx (a 304 without
  Content-Length waited for the server to close the connection).
- `TIMES` (Shell / Tool Type): the times to `RAM:WarpHub-times.txt`.
- Measured on Amiga 2 (A4000, Warp 7060), `RAM:WarpHub-test`, 10 + 4 private games:

  | Step | 1st start (as 0.10) | next starts |
  |---|---|---|
  | drawers at the start | 860 ms, 11 programs read | 20 ms, 0 read |
  | public catalog | 4680 ms (76270 B) | 1980-2280 ms (304) |
  | private catalog (old server, no ETag) | 6480 ms (300556 B) | 2820-6560 ms (300556 B) |
  | pictures and texts of the cache | 600 ms | 340-360 ms |
  | drawers after the refresh | 960 ms | 40 ms |
  | ready after the start | 17.4 s | 8.3-11.6 s |

  Left: the TLS connection to GitHub (~2 s with 304), NTP 0.5-1.7 s, the pictures (datatypes,
  ~1 s at the start and ~1 s after every refresh: `take_catalog` loads all again). The new
  `private_server.py` is on the RP4 (25.11.2026, `push_private.sh <user@host> --no-www`); from the
  PC: catalog 200 with an ETag (300556 B), the same request with If-None-Match 304 (0 B).
  Amiga 2 with the new server (3 starts): 1st private 4960 ms (300556 B, ETag remembered), then
  1380 / 740 ms (304); whole refresh 8.0 -> 3.8 / 3.1 s, **ready 11.4 -> 6.6 / 5.8 s after the
  start** (0.10: ~17 s). Left: public catalog 2.1 s (TLS to GitHub, 304), pictures 2 x 1 s, NTP.
- Pictures: `take_catalog` keeps the tile (datatype object) of a game with the same key and
  picture SHA-256 (`Tile.sha`), only new / changed pictures are loaded. After a refresh
  980 -> 80-100 ms; **ready 5.5 s after the start** (Amiga 2, 2 starts). The first load at the
  start (~0.9 s, 10 pictures) stays.
- Released as **0.11** (25.11.2026), `SYS:WarpApps/WarpHub` on Amiga 2 = the release build
  (checked byte for byte). The window with the pictures not checked by a screenshot (`shot` of
  AmiAgent did not finish on Amiga 2 that day); the window title showed the private source.
  Checked by the user on Amiga 2 (25.11.2026): the pictures look right. Tests: 24 PC tests (new: ETag / 304 of the
  private server, also for a code). Installed on Amiga 2 as `SYS:WarpApps/WarpHub` 0.11 (0.10 kept
  as `WarpHub-0.10`).
- Later, with many games: a small index catalog (name, version, picture, SHA-256 of the file list
  of each game) and the file list of a game only for Install / Update.

## 0.12: what a failed TLS handshake says (25.11.2026)
A player with WARP HUB 0.5 got only "TLS handshake failed" on Refresh (no OpenSSL reason) -
nothing to go on. Now `handshake_err()` in `hub_https.c` gives the host, the reason (OpenSSL
error string, or for SSL_ERROR_SYSCALL: connection closed / reset / timeout / network error),
`SSL_get_error`, `Errno()` and `OpenSSL_version()`. `WarpHubCLI` takes `https://host:port/`.
Tested on Amiga 1 (A1200, Warp 1260, WarpSSL, AmiSSL with OpenSSL 3.5.4), WarpHubCLI against
the RP4:
- port 22 (SSH): `error:0A00010B:SSL routines::wrong version number [SSL 1, errno 0, OpenSSL 3.5.4 30 Sep 2025]`
- a server that closes at once: `error:0A000126:SSL routines::unexpected eof while reading [SSL 1, errno 0, ...]`
- a server that accepts and stays silent, closing after 200 s: `connection reset by the server
  or the network [SSL 5, errno 54, ...]` after **203 s** - the 30 s socket timeout
  (`SO_RCVTIMEO`) does **not** work during the handshake on this Amiga: a silent server holds
  the refresh until it closes (Stop / Ctrl-C still breaks it). Open: handshake with a
  non-blocking socket and WaitSelect with a time limit.
- normal catalog: 10 games, 1200 ms, TLSv1.3.
The reason of the player's failure is not known yet (asked: AmiSSL / WarpSSL / stack versions,
the whole message of WarpHubCLI LIST).
- Released as **0.12**; installed on Amiga 1 as `SYS:WarpApps/WarpHub` (= release build, byte
  for byte), 0.10 kept as `WarpHub-0.10`.

## 0.13: time from HTTP when NTP does not answer (25.11.2026)
A player with WARP HUB 0.11: "The clock of the Amiga is wrong and no time server answers" - no
NTP (UDP 123 blocked somewhere) and a clock older than the program. Now `hub_net_time()`
(`hub_time.c`, used by the window and WarpHubCLI): the NTP servers, then the `Date` header of
`HEAD / HTTP/1.1` to raw.githubusercontent.com, www.google.com, www.cloudflare.com (port 80,
5 s per piece, `WaitSelect`); only for the certificate check, the clock is not changed.
Tested on Amiga 1: `WarpHubCLI TIME` - NTP and the three web servers give the same second
(13:56:53 UTC, PC 13:56:55); `LIST NTP=no-such-host.invalid` loads the catalog (time from
HTTP); the window 0.13 in RAM: (TIMES) ready after 6.5 s, private source logged in. Not tested:
an Amiga with a wrong clock and UDP 123 blocked (the player's case). No 64-bit mul/div.
- **Time limits (0.13, after a player waited "kilkanaście minut" on "loading pictures and
  texts"):** the RP4 log showed his connections open but nothing coming back (server segments
  retransmitted, never acknowledged) - and WARP HUB waited without end (SO_RCVTIMEO does not
  work under AmiSSL/WarpSSL). Now the socket is non-blocking (`FIONBIO`) and every wait -
  connect, TLS handshake, request, every read - is `WaitSelect` with 20 s and Ctrl-C
  (`wait_sock` / `ssl_wait` in `hub_https.c`). A request that got no answer (nothing to the sink
  yet) is tried once more on a new connection; an install retries a file once
  (`hub_conn_net_error`); a refresh counts the file as missing and goes on. Tested on Amiga 1
  (WarpHubCLI against test servers on the RP4, test certificate as CAFILE):
  silent before TLS -> 24 s "no answer within 20 s (timeout)"; TLS without an answer -> 43 s
  (with the retry) "no answer within 20 s"; body stopping after 1000 B -> 23 s "no data within
  20 s"; Stop (Ctrl-C) -> 1 s "stopped (Ctrl-C)" (Roadshow gives EINTR, not the signal);
  GitHub normal 1.2 s. The window: refresh with the private source 2.9 s, install of Vastar to
  RAM: (11 files) OK.

## WarpNetTest 0.1: network test for players (25.11.2026)
`amiga/nettest.c` -> `build/amiga/WarpNetTest` (36 KB; from the icon a CON window, Return ends;
every line also to `RAM:WarpNetTest.txt`). DNS, TCP connect (443, 80), NTP and the HTTP time,
GitHub: TLS handshake + WarpHub.info, 5 small requests on one connection, catalog, the
Settlers 0.140 ZIP with KB/s; the private source of `WarpHub.prefs` (PROGDIR: or SYS:WarpApps):
DNS, TCP, handshake + catalog (with a remembered password) or a 401. No address in the code.
Amiga 1 (A1200, Warp 1260, home network): GitHub handshake + small file 0.9-1.4 s, small
request 36-40 ms, catalog 286-744 KB/s, ZIP 1.1-1.3 MB/s; private catalog 590 KB/s, again on
the connection 1.1-1.3 MB/s; all in 3-4 s. Package for a player: `build/WarpNetTest.zip`
(not in git; icon = the WARP HUB icon).
- **WarpNetTest 0.2:** rows with a mark `[ OK ]` / `[SLOW]` / `[FAIL]` (PL `[WOLNO]` / `[BLAD]`),
  bold headers in the window (CSI 1m; the log file plain), numbered parts (network, time,
  GitHub, private server) and **RESULT** in plain words: DNS / port 443 / NTP blocked / wrong
  clock / TLS failing / speed of GitHub (fast >= 100 KB/s, slow >= 30) with an estimate of the
  first start of WARP HUB (4 files a game, ~38 KB a game, +220 ms a file as measured in WARP
  HUB) and of a 10 MB game, the private server. Polish or English after LANGUAGE of
  WarpHub.prefs. Amiga 1: "Wszystko w porzadku", first start ~10 s (measured 11.4 s); a wrong
  private port gives `[BLAD]` with the TLS error in the result.

## 0.14: resumed installs, 3 tries, 30 s (25.11.2026)
The player's line (WarpNetTest: DNS 7 s, TCP connect 4.3 s, TLS without data) got through with
0.13, but the full OpenDUNE (83 MB, private) stopped with a timeout - and a stopped install
deleted every `.upd`, so the next try started from zero. Now:
- `fetch_one` takes an `.upd` that is already whole and right (size + SHA-256, `local_file`);
  a stopped install keeps the `.upd` files (no `remove_partial` any more; the file being
  downloaded is deleted by `fetch_one`); Uninstall also deletes `<file>.upd`.
- A file is tried up to 3 times when `hub_conn_net_error`, 2 s apart (`Delay(100)`).
- `TIMEOUT_S` 30 (user's decision).
Tested on Amiga 1: Settlers 0.140 (GitHub) to RAM:, Stop after 9 files -> 9 `.upd` kept; the
second install finished in 14 s, 27 files, `Version` 0.140, no `.upd` left. (That the 9 were
not downloaded again is not visible from outside - code path only.)
- WarpNetTest 0.3: RESULT lists every problem (not only DNS), "very slow connecting" (> 1 s),
  "connection opens but no data comes", the hints (cable, duplex, Wi-Fi bridge, `ping -s 100 /
  1400`, MTU 1400), a wrong clock without network time. Tested with a silent server on the
  RP4: `[BLAD] Polaczenie sie otwiera, ale dane nie dochodza` + hints after 33 s.
- Also tried again (up to 3): a connection that fails before the request (no TCP connection
  or no TLS handshake in time) - `request()` sets `net_error` when `conn_open` fails, except a
  refused / not remembered certificate. Seen: the player's Diablo install never reached the
  RP4 (no `files diablo` in the log) - the handshake timed out. Not tested live (needs a
  server that serves the catalog and then goes silent).
- Released together as **0.13 + 0.14** (commit 2549fe1); Amiga 1 `SYS:WarpApps/WarpHub` = 0.14
  (byte for byte), 0.10 kept as `WarpHub-0.10`.

## WARP HUB in the private source (25.11.2026, user's request)
`catalog/games/warphub.txt`: `LOCAL ../AmigaSoft/WarpHub`; `make_release.py` also writes
`private/WarpHub-<v>-full.zip` (= the release ZIP; `private/` in `.gitignore`, older to
`private/old`); `make_private.py` applies FLAT (`make_catalog.apply_flat`, shared with the
public catalog) - without it the private entry would install a drawer `WarpHub/` instead of the
file in SYS:WarpApps. The private entry of 0.14: the same FILE lines (size, SHA-256, flags) as the
public one, FLAT, SOURCE private. On the RP4 (`push_private.sh`, 29 files); the main password
and the 7-day code see `warphub`. Amiga 1, a copy in RAM: logged in: TESTINSTALL=warphub ->
"WARP HUB 0.14 zainstalowana", `WarpHub` + `WarpHub.info` as files (no drawer), from the RP4
(`files warphub` in the log), byte for byte the release.
Note: a release must be built from the committed program - `make_release.py` packs
`build/amiga/WarpHub` as it is (after later changes in `hub_https.c` it would have made
another "0.14"; restored from git).

## WarpNetMonitor 0.1: speed charts over minutes (25.11.2026)
`amiga/netmon.c` -> `build/amiga/WarpNetMonitor` (44 KB; Intuition window 624x432, fits
640x480). Targets in turns of 10 s: GitHub (Settlers 0.140 ZIP, again and again), Cloudflare
(`speed.cloudflare.com/__down?bytes=25000000`), Hetzner (`fsn1-speed.hetzner.com/100MB.bin`),
the private source of WarpHub.prefs (`/catalog.txt`; without a password the 401 response time
only). Speed chart: a bar a second in the colour of the server (red = a second without data);
response chart: time to the first byte of every request, red X = error; table: avg / min / max
KB/s (not the first second of a slice: TLS), stalls (> 1 s without data), errors, response; at
the end a verdict per server (STABLE / UNSTABLE / SLOW / NO CONNECTION) and "all servers stall
= the line of the Amiga" / "only some". `DURATION=<s>` (Tool Type / Shell, default 180);
`RAM:WarpNetMonitor.txt` every second. `hub_https`: `hub_wait_hook` / `hub_wait_sigs` - a wait
wakes every second (and on the window signal) so the window draws and reads its gadgets during a
stall; NULL in WARP HUB (plain waits). Fixes found on Amiga 1: `hub_url_path` encoded the `?` of
the Cloudflare query (404) - the paths are used as they are; the first second of a slice and the
bit after the end were counted as min (GitHub "UNSTABLE"). Amiga 1, 180 s: all four STABLE,
GitHub 1177, Cloudflare 1241, Hetzner 1180, RP4 1064 KB/s, "Lacze stabilne".

### Seen on Amiga 2 (25.11.2026): a resumed Diablo install looked stuck
The user restarted the full Diablo (804 MB, private) after an earlier try had left whole `.upd`
files (diabdat.mpq 517 MB, hellfire / hf*.mpq). 0.14 checks each with SHA-256 before taking it
(SD card + SHA: minutes for 517 MB) and gave no progress meanwhile: the status stood at
"Pobieranie DiabloWarp/data/ReadMe.txt (1%)" for many minutes, then jumped (hfmonk 78%,
hfmusic 82%). WarpNetMonitor at the same time: all servers > 1 MB/s - not the network.
Fix (next release): the check of an earlier `.upd` reports progress (`resume_cb` through
`local_file`'s CheckCtx), so the bar moves while it reads. The label still says "Pobieranie".

## 0.15: window "Downloaded files" (25.11.2026, idea of the user)
A title bar button left of the padlock (same image class, `files_img`: a list with an arrow,
green while an install runs) and the key F1 open `show_files()`: a ReAction window as the guide
(listbrowser + Close), on top "<game>: pobrane X z N plikow (a MB z b MB)", every file of the
install "path (size) - czeka / sprawdzanie N% / N% / gotowe / blad". `HubFile.state` / `got`
(new fields, written by the network process in `hub_install.c`: WAIT at the start, CHECK while an
earlier `.upd` is checked - with progress, GET while downloading, DONE / FAIL); the window's loop
also waits on the worker signal: `worker_news()` as in the main loop, the list at most every
300 ms, scrolled to the current file. Tested on Amiga 2 (RAM:WF, public Diablo 18 MB): F1 during
the download -> "Diablo (DevilutionX): pobrane 17 z 251 plikow (1.0 MB z 17.4 MB)", rows done /
96% / waiting, Esc closed it, the install finished (devilutionx 0.51). The button itself was
not clicked (AmiAgent has no mouse) - it is drawn left of the padlock (screenshot).
- Window title (user's wish): "WARP HUB 0.15 - <what it does>", alone "WARP HUB 0.15" (the
  version right after the name; `set_status`, `WA_Title`).

## WARP NetMonitor 0.2: dashboard in the style of "Net Monitor" (25.11.2026, user's wish)
The same measurement (GitHub, Cloudflare, Hetzner, RP4 in turns of 10 s), new look: a dark
window 640x440 (own pens by ObtainBestPen, topaz 8): red header "WARP NetMonitor" with CPU / KS,
a menu on the left with the logo bars (1 Panel, 2 Serwery, 3 Wynik), five cards (download now,
average, response, jitter, stalls / errors), an area chart (dark fill + bright top line in the
colour of the server, red = a second without data) with the ranges 1 min / 3 min / all,
buttons Pause / Reset / Start (mouse and keys P R S, 1-3 the views, Esc end), the legend table
(peak / average / minimum / stalls / errors / response) and a status line. Jitter: |response -
previous response of the same server and kind| (kind = new connection with TLS or kept), else
it mixed 0.8 s handshakes with 40 ms requests. After the test the view Result; Start again.
Amiga 1 (60 s) and Amiga 2 (180 s): all four STABLE; Amiga 2 GitHub ~1.5 MB/s, Cloudflare
~1.85 MB/s, Hetzner ~1.1 MB/s, RP4 ~1.35 MB/s, jitter 11-14 ms; one run on Amiga 2 had two
GitHub seconds without data and gaps on the RP4 (stalls 2 / 1). Installed in
Work:Games/WarpNetMonitor on Amiga 2.

### WARP NetMonitor moved to the Warp repo (25.11.2026, user's decision)
NetMonitor leaves WarpHub: it is rebuilt in the style of WarpDashboard (its DashGfx /
DashWidgets drawing, helvetica, WARP logo) as `csWarpAmigaSoftware/WarpNetMonitor` (GPLv3, next
to WarpDashboard). Removed here: `amiga/netmon.c`, its Makefile target, its icon. The network
code it uses (`hub_https`, `hub_time`, `hub_util`, `hub_wait_hook`) stays in WarpHub; WarpNetTest
stays here.

## WarpNetTest 0.4: stack, ping, certificates, ports, TLS (25.11.2026)
New parts (the old ones kept; sections 1-7, RESULT):
- **Stack and router** (section 1): the release string of bsdsocket (`SBTC_RELEASESTRPTR`).
  warpSOCKET (the WiFi module of the Warp, stack mode): the state of the module through
  `cswarp.library` `netCtl(CSWNET_INFO)` (LVO -618, only for 30.65+, version-1 info block
  laid out by hand: link, address, gateway) - warpsocket.library has no `GetRouteInfo` /
  `QueryInterfaceTagList` (ENOSYS). Roadshow: `GetRouteInfo(AF_INET, RTF_GATEWAY)` (the
  route to 0.0.0.0), the interfaces with device / address / MTU (`ObtainInterfaceList`,
  `QueryInterfaceTags`; a `warpnet` device = the WiFi of the Warp in bridge mode). Option
  `ROUTER=<address>` (Tool Type / Shell) when the gateway is not known.
- **Ping** (section 2): own ICMP echo through `socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)`
  (written here, not taken from `wping`, which is GPLv3): 20 requests every 100 ms, then at
  most 1 s for the last replies; 56 B and 1400 B (warpSOCKET takes at most 1468) to the
  router and to 1.1.1.1. The reply may start with the IP header (skipped by its length);
  id / sequence / data checked, ICMP unreachable / time exceeded counted. Times from the
  E-Clock (`timer.device` UNIT_ECLOCK, tenths of a ms, no 64-bit). Rows: loss %,
  min/avg/max, jitter (average change between consecutive replies). RESULT: Amiga-router
  loses >= 10% -> WiFi / cable / router (try another router, a phone hotspot, channel,
  cable); big lost and small not -> MTU (mtu=1400); router fine and the internet loses ->
  behind the router / the provider; router silent but 1.1.1.1 answers -> "the router does
  not answer pings" (not judged); no raw socket -> said in the result, not an error.
- **Certificates** (section 5): `badssl.com` must connect; `expired.`, `self-signed.`,
  `wrong.host.badssl.com` must be refused ("Certificate of ... refused" of hub_https);
  a bad one taken -> `[BLAD] AmiSSL nie sprawdza certyfikatow` + hint (date, AmiSSL,
  WarpSSL); another error -> "not checked". `www.howsmyssl.com/a/check`: TLS version and
  rating (a row; "Bad" -> SLOW).
- **Ports** (section 6): TCP connect to portquiz.net (listens on every port) 80, 443, 6831
  (and the port of PRIVATE when another), 5 s each: "port 6831 blocked in your network -
  the private source cannot work from here"; private server unreachable but its port open
  -> "the server does not answer".
- RESULT lines wrap at 70 characters (the CON window has 78 columns).
`hub_https` / WARP HUB unchanged. No 64-bit mul/div (`find64.sh`: 0). 62 KB.
Tested on **Amiga 1** (A1200, Warp 1260, **warpSOCKET 30.75**, WiFi of the Warp, stack
mode; WARP NetMonitor was running at the same time): from the Shell 18 s, router
4.3/10.5/20.0 ms 0%, 1400 B 0%, 1.1.1.1 0%, the three bad certificates refused, TLS 1.3
"Probably Okay", ports 80/443/6831 open, "Wszystko w porzadku"; `ROUTER=<an unused LAN address>`
(nobody there) -> router 100% lost, internet fine -> "Router nie odpowiada na ping ..."
(INFO); from the icon (WBStart, LANGUAGE en in PROGDIR:WarpHub.prefs) 16 s, Return closed
the window; Ctrl-C during the ping -> "Stopped (Ctrl-C)" in 3 s.
Not tested: Roadshow (GetRouteInfo, interface list, raw socket under Roadshow) - Amiga 2
was not reachable from the PC that evening; a line that really loses packets (the
verdicts for losses / MTU are code paths only); a stack without raw sockets.
Package `build/WarpNetTest.zip` (program 755, icon, ReadMe.txt, not in git).

## Private server: POST /upload for WARP NetMonitor (25.11.2026)
`private_server.py do_POST`: `/upload` only, with a valid password or code (any); the body is
read in 64 KB pieces and dropped, the answer is the number of bytes; at most `UPLOAD_MAX` 64 MB a
request (411 without Content-Length, 413 larger), at most `UPLOAD_PARALLEL` 3 uploads at the same
time (more: 503 "busy, try later" at once - the RP4 serves games first, user's wish); log "upload
<ip> [code]" once per login. Load: the Amiga sends ~1 MB/s, the RP4 decrypts and drops it.
Tests: 27 (upload with the main password and a code, 401 without, 404 on another path, 503 at the
limit). On the RP4 since 22:28 (`push_private.sh --no-www`); from the PC 4 MB in 0.56 s.

## WARP NetMonitor in the catalog (26.11.2026)
WARP NetMonitor (csWarpAmigaSoftware/WarpNetMonitor, GPLv3) is released in its own public repo
`cizar-szymon/Amiga_WarpNetMonitor` (releases/amiga + its source, user's decision). Catalog:
`catalog/games/warpnetmonitor.txt` (TAGS apps + Warp 040/060 groups, FLAT into SYS:WarpApps next
to WARP HUB, ORDER 2, LOCAL ../AmigaSoft/Amiga_WarpNetMonitor), picture
`catalog/images/warpnetmonitor.png` (the English panel). `make_catalog.py --simulate`: version
0.9, 17 files (program N, icon K, doc X). Fix: `--simulate` copies the blocks of public games
from catalog.txt as text, `apply_flat` only for built entries (it failed on them since the
refactor).

## 0.16: group buttons, favorites, the Warp card (26.11.2026, user's decisions)
**Buttons** (keys 1-7, the window is not larger; on 1280x1024 the row is narrower than the old
one, the labels are shorter in every language): Ulubione / Favorite / Favoriten, Zainstalowane /
Installed / Installiert, Dostepne / Available / Verfügbar (only not installed), Programy (`apps`),
Tryb ARM / ARM mode / ARM-Modus (`warp_arm`), PowerPC (`ppc`), Inne (`other`), the eighth empty
and disabled (for later). "Wszystkie" is gone. `filter_key[]` in `warphub.c`; the view is built
again at every `refresh_all()` (an install moves a game from Available to Installed), the
selection stays even when the game leaves the group (the page and Run stay; arrows go to the
first tile).
- **Start group:** the last one, `FILTER <name>` in `WarpHub.prefs` (written at the change and
  at the end). The first start without it: Installed when the player has a game / program of
  the catalog besides WARP HUB itself (WARP HUB is always "installed"), otherwise Available.
- **Favorites:** `WA_RMBTrap` (the window has no menus), `MENUDOWN` on a tile toggles it, key
  F (0x23) for the selected tile; `FAVORITES key,key` in `WarpHub.prefs`
  (`hub_prefs_toggle_favorite`, up to 1 KB). The star (11x11, yellow 0xffd820 with a dark edge)
  is drawn bottom right, beside the two text lines under the picture (coordinator: not on the
  picture); name and state end 18 px before it. Title: "Ulubione: dodano X." / "usunieto X.".
  Empty Favorites: "Kliknij kafelek prawym przyciskiem, aby dodac go do ulubionych."
- **Card:** `hub_card.c` - FindConfigDev 0x1400/101, then `getDiagnostic()` of
  `cswarp.library` (LVO -42, own minimal structure as `CsWarpMini.h`: `boardName` at 0,
  `warpBoardType` at 344, checked at compile time), only read. ARM: type 9 (7060) or 13
  (1260X); another known type: no ARM; type 0 (old library): by the name ("7060", "1260X"),
  otherwise unknown. **Dimmed** (every second pixel of the picture in the background colour,
  name grey, on the page in orange "Nie dziala na tej Amidze (<card>): potrzebny PowerPC /
  karta Warp z ARM"): a game without `warp` (or the old card groups) with `ppc` on any Warp
  card, with `warp_arm` on a Warp card without ARM. **Without a Warp card / library nothing
  is dimmed** (the Amiga may have PowerPC or be an emulator; PowerPC is not looked for).
  Shell option `TESTCARD=<name>|NONE` for tests; TIMES logs the card.
- **Catalog tags:** game files and the editor have only `warp`, `warp_arm`, `ppc`, `other`,
  `apps` (may overlap). All `warp1260/1240/1260x/7060` became `warp` (Syndicate
  Wars without 1240 and AmiXcom without 1260X lost that difference - the user's decision);
  no game has `warp_arm` yet. `game_files.parse` reads the old groups as `warp`;
  `make_catalog.py` writes for `warp` also the old four (`catalog_tags`), so the card buttons of
  0.15 and older still work (their parser only compares names; unknown tags do nothing).
  Tests: 30 OK.

Tested on **Amiga 1** (A1200, Warp 1260 Extreme, 1280x1024), `RAM:WarpHub-test` (copy of the
user's settings and cache; the private login lines removed for the dimming test):
- cswarp.library: `warp1260X`, type 13, ARM 1 (TIMES);
- first start without FILTER -> Installed (9 tiles); F on Settlers and C&C TD: stars bottom
  right, title "Ulubione: dodano ..."; key 1: the two favorites; key 3: OpenDUNE, Red Alert, ZOD;
  after Esc `FAVORITES settlers,tiberiandawn` and `FILTER available` in the prefs;
- a test catalog (OpenDUNE `TAGS ppc`, ZOD `TAGS warp_arm`): with `TESTCARD="Warp 1260"` both
  dimmed, the reason on the page; with the real card (ARM) only OpenDUNE; ARM mode tab: ZOD,
  PowerPC tab: OpenDUNE;
- Favorites emptied (F, right, F): the hint text.
Not tested: the right mouse button itself (AmiAgent has no mouse), 640x480 (Amiga 2, other
network), German / English labels on the screen.

**Amiga 2** (A4000, Warp 7060, Workbench 1920x1080, `RAM:WarpHub-test`, copy of the user's prefs
and cache, LANGUAGE en, test catalog with OpenDUNE `ppc` / ZOD `warp_arm`): card `warp7060`,
type 9, ARM 1. English buttons Favorite ... Other, the eighth disabled. The user marked WARP HUB
and WARP NetMonitor with the **right mouse button** (first real test of it): stars bottom right,
`FAVORITES warphub,warpnetmonitor`. Nothing dimmed, as it should be: ZOD runs with the ARM of the
7060, and OpenDUNE comes from the private source (logged in), whose entry still has the old card
tags. The refresh took 68 s (pictures and texts 63 s, NTP 8 s) - not looked into. 640x480 not
checked (the user's Workbench would have to change its mode).
