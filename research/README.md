# research/ — kildemateriale og protokollutledning

Denne mappa inneholder to slags filer: **kopiert kildemateriale fra andres
arbeid**, og **vår egen utledning** basert på det.

## Vår egen

| Fil | Innhold |
|-----|---------|
| `protocol-notes.md` | Utledet protokollbeskrivelse: buss-parametere, fysisk tilkobling/pinout, telegramformater, sjekksumalgoritme (verifisert numerisk i Python), og lista over usikkerheter som gjenstår. |

## Kopiert kildemateriale

Alt under her er **Copyright (c) 2018 Vongraven**, hentet fra
[Vongraven/Flexit-SL4R-master](https://github.com/Vongraven/Flexit-SL4R-master)
og gjengitt uendret under MIT-lisens. Lisensteksten ligger i
[`LICENSE-Vongraven`](LICENSE-Vongraven).

| Fil | Innhold |
|-----|---------|
| `Flexit_master.ino` | Original Arduino Mega-implementasjon, testet mot ekte SL4R/CS50. Hovedkilden til protokollkunnskapen. |
| `vongraven-README.md` | Hans README, med byte-dumper av linje 15 (status) og et kommandotelegram. Brukt til å verifisere sjekksumalgoritmen numerisk. |
| `images/vongraven-topology.png` | Koblingsskjema: MAX485 mot CS50-kortets 4P4C-port, i parallell med CI50. Eneste koblingsskjema som finnes for denne bussen — kopiert hit fordi originalrepoet har ligget urørt siden desember 2018 og kan forsvinne. |

## Forholdet til Vongravens repo

Dette repoet er **ikke en fork**. Det deler ingen kodelinjer med
`Flexit_master.ino` — implementasjonen her er en ESPHome external_component i
C++ med Python-codegen, bygget rundt en ikke-blokkerende tilstandsmaskin, mot
ESP32. Originalen er en frittstående Arduino-skisse for Mega med blokkerende
`while (!Serial1.available())`-løkker.

Det som er arvet er **protokollkunnskapen**: synkroniseringsregelen,
byte-offsetene i statustelegrammet, sjekksumalgoritmen og malen for
kommandotelegram. Den er reimplementert fra bunnen og verifisert numerisk mot
eksemplene i `vongraven-README.md` — ikke kopiert.
