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
