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
| 7. Repo WarpHub na GitHubie (katalog musi być publiczny), wydanie | ⏳ za zgodą |

## Decyzja: pobieranie wprost z GitHuba, bez serwera danych (19.11.2026)

Użytkownik zmienił pierwotną koncepcję z `CLAUDE.md` (serwer danych na Raspberry Pi 4, TCP
bez TLS). **WARP HUB pobiera gry wprost z GitHuba przez HTTPS** z AmiSSL 5. Na kartach Warp od
oprogramowania 3.074 AmiSSL przyspiesza **WarpSSL**: szyfrowanie AES-GCM i ChaCha20-Poly1305
oraz uzgadnianie TLS robi karta. Warp560 / 100 MHz: ok. 1 MB/s przy 55% CPU zamiast 450 KB/s
przy 100%.

Dlaczego tak:
- TLS gwarantuje, że pliki pochodzą z GitHuba, a SHA-256 z katalogu sprawdza każdy plik;
- nie trzeba otwartego portu w domu ani usługi na RP4;
- gry są dostępne także wtedy, gdy domowy internet nie działa.

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

Sprawdzanie wersji w grach (przycisk UPDATE, dziś Settlers 0.140+ pyta dawny serwer aktualizacji)
jest do ustalenia z użytkownikiem. Gry mogłyby czytać ten sam `catalog.txt`.

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
