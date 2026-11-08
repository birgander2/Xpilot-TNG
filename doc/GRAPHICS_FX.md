# Grafikeffekte für den Client – Ideensammlung & Konzept

Stand: 2026-11-07 · Status: Schritt 1 umgesetzt (`sdl3_fx.c`), Rest Konzept

## Ausgangslage

- Der Client rendert über den SDL3-2D-Renderer (`SDL_RenderGeometry`, Alpha-Blending), keine Shader.
- Additives Blending (`SDL_BLENDMODE_ADD`) und Texturen sind verfügbar → damit ist fast alles hier machbar.
- Relevante Stellen: `src/client/sdl3/sdl3_draw.c`, `sdl3_render_world.c`, `sdl3_render_objects.c`,
  `src/client/paintobjects.c` (`Paint_debris`, `Paint_wreckages`).

## Grundprinzip: Effekte aus dem Zustand ableiten

Der Client kennt nur, was der Server schickt (Debris, Schiffe, Schüsse, Sound-Events).
Alle Effekte sind **rein kosmetisch und clientseitig**, abgeleitet aus Ereignissen:

| Effekt-Auslöser | Erkennung |
|---|---|
| Explosion | Schiff verschwindet / Wrack erscheint / Debris-Burst / Todes-Sound |
| Wandkontakt | Sound-Event „player hit wall“ oder Geschwindigkeitssprung in Wandnähe |
| Kondensstreifen | Drehrate × Geschwindigkeit aus den Schiffsdaten |

Vorteile: kein Protokoll-Change, NG-kompatibel, funktioniert auch in Replays.

## Effekte

### Explosionen / Feuerbälle
- Beim Start prozedural erzeugtes Sprite-Sheet (Noise-basiert, ~16 Frames: heißer Kern → orange → Rauch).
  Keine Assets, keine Lizenzfragen.
- Mehrere Sprites pro Explosion mit Zufallsrotation/-skalierung; Feuer additiv, Rauch normal geblendet.
- Praktische Effekte: heller Blitz im ersten Frame, Schockwellen-Ring, nachglühende Glutpartikel,
  langsam driftender Rauch.

### Kondensstreifen
- Ringpuffer der Flügelspitzen-Positionen → Triangle-Strip mit ausblendendem Alpha.
- Nur bei hoher Querbeschleunigung (schneller Kurvenflug), kurz nachleuchtend.

### Funkenflug an Wänden
- Partikel mit Schwerkraft/Abprall entlang der Wandnormale.
- Farbverlauf orange → rot, als kurze Linien gezeichnet (Motion-Blur).

### Hügellandschaft in gefüllten Wänden (Pseudo-3D)
Die Wandbegrenzungen bleiben unverändert (Kollisionskante = klar sichtbar).
Nur das **Innere** gefüllter Wandflächen bekommt eine Polygon-Hügellandschaft aus zusätzlichen
Linien und Schattierung.

Umsetzung (`src/client/sdl3/sdl3_terrain.c`, Optionen `fxTerrain`, `fxTerrainMesh`):
1. **Raster**: globales, leicht gejittertes Dreiecksraster (~40 px), an die Map-Größe angepasst.
   Jedes Rasterdreieck wird mit den Dreiecken der Polygon-Triangulierung (dieselbe wie bei der
   Füllung) geschnitten: konvex ∩ konvex → robust, keine Ausreißer über die Wandkante.
2. **Höhe**: `h = H · (1 − e^(−d/R)) · Noise`, `d` = Abstand zur nächsten *sichtbaren* Wandkante.
   Interne Kanten (Edge-Style 0) zwischen zusammengesetzten Polygonen zählen nicht → nahtlos.
   Sichtbare Kanten liegen auf Höhe 0.
3. **Schattierung**: flach pro Rasterdreieck aus der Steigung, Licht von oben links;
   1.0 = ebener Boden, Wandfarbe wird abgedunkelt/aufgehellt.
4. **Linien**: Rasterkanten, auf das Polygon zugeschnitten, als hellere, halbtransparente Linien.
   Höhenlinien oder Gratlinien wären mögliche weitere Stile.
5. **Performance**: alle Wand-Polygone werden im ersten Frame einer Map vorberechnet
   (z. B. tourmination: 1459 Polygone in ~120 ms), in Chunks abgelegt, nur sichtbare Chunks
   werden gezeichnet. Die normale Füllung liegt darunter und deckt Subpixel-Fugen ab.
6. **Gefüllte** Polygone: Facetten ersetzen die Füllfarbe (abgedunkelt/aufgehellt).
   **Texturierte** Polygone: Textur bleibt, darüber Schatten per MOD-Blending und etwas
   additives Licht auf hellen Hängen; Rasterlinien weiß/halbtransparent.
   `classicGraphics` schaltet alles ab.

Erledigte Punkte:
- **Block-Maps**: nur Polygonkarten (wie gewünscht).
- **Map-Wrapping**: Raster, Rauschen und Abstandsberechnung sind periodisch zur Map-Größe;
  gewrappte Kopien und an der Map-Kante geteilte Wände sehen aus wie alle anderen.

Offen:
- Später kombinierbar mit dynamischem Licht (Explosionen hellen Hänge auf).

### Schiffe aufhübschen (Vorschläge)
Grundsatz: Die Shipshape bleibt unverändert, die gezeichnete Außenlinie ist weiterhin genau die
Kollisionskontur. Alles Neue liegt darunter, darauf oder daneben. Jeder Effekt einzeln schaltbar,
`classicGraphics` schaltet alles ab.

Rumpf (✅ umgesetzt in `src/client/sdl3/sdl3_fx_ship.c`, Optionen `fxShipHull`, `fxShipHalo`),
Variante „A: Weicher Glanz“ (gewählt nach Vergleich mit Tönung und Silhouette):
- Füllung: vorberechnete Kuppel-Lichttextur (diffus + Glanzpunkt), in Bildschirm-Orientierung
  über das Schiff gelegt → Glanz bleibt beim Drehen oben links. Funktioniert für jede Form/Größe.
- Kontur bleibt exakt die Shipshape, ihre Helligkeit folgt dem Licht (Schattenseite ~35 %).
- Ein erster Versuch mit Facetten war bei kleinen Formen zu unruhig, die volle Kontur zu dominant.
- Bei Lock (bisher schon voll gefüllt) bleibt die alte Darstellung; getarnt/phasend: unverändert.
- Halo nur bei Teamplay: eigenes Team (auch man selbst) blau, alle anderen in ihrer Schiffsfarbe.

1. **Gefüllter Rumpf**: Shipshape mit einer dunklen, halbtransparenten Variante der Schiffs-/
   Teamfarbe füllen → wirkt massiv statt Drahtgitter. Triangulierung für konkave Formen existiert
   (`SDL3_Triangulate_Polygon`).
2. **Pseudo-3D-Facetten**: passend zur Hügellandschaft den Rumpf vom Mittelpunkt aus in Facetten
   zerlegen und gegen das feste Licht schattieren. Da sich das Schiff dreht, das Licht aber nicht,
   wandern Glanz und Schatten beim Drehen über den Rumpf. Sehr plastisch, kostet fast nichts.
3. **Weicher Halo in Teamfarbe**: dezenter Schein entlang der Kontur, gezielt mit breiten weichen
   Quads (Glow-Sprite) statt Fullscreen-Bloom. Hilft auch bei der Team-Erkennung.

Antrieb:

4. ✅ **Triebwerksflamme** (`fxEngineFlame`): Glow-Sprites an `engine[dir]` – drei aufgefächerte,
   einzeln flackernde Strahlen (orange Mitte, rötere Seiten, ±19°), bläulich-weißer Kern, heißer
   Punkt an der Düse; unter dem Rumpf gezeichnet.
   Schub: eigenes Schiff = Schubtaste; andere = Server-Schubfunken direkt am Triebwerk (auch beim
   Schweben gegen Gravitation) oder Beschleunigung in Blickrichtung > 0,12 px/Tick²
   (Ausgleichsparabel über 9 Frames, Rauschen ~±0,05), 3 Frames Haltezeit.
   Ursprüngliche Idee: an der Triebwerksposition der Shipshape (`engine[dir]`) ein flackernder
   Flammenkegel mit Hitzeverlauf (weiß → blau → orange). Eigenes Schiff: Schub direkt bekannt;
   andere Schiffe: aus der Beschleunigung in Flugrichtung (vorhandenes FX-Tracking).
   (Ersetzt den Punkt „Bessere Schubflamme“ unter „Weitere Ideen“.)

Schild und Zustände:

5. ✅ **Schild als Hexagon** (`fxShield`): blasses, gestricheltes Hexagon statt gestricheltem Kreis
   (Notschild sieht genauso aus). Deflektor: acht Bogenstücke mit weichem Glow, die sich langsam
   ums Schiff drehen, Farbe wandert weich zwischen Rot und Blau (statt hartem Farbwechsel);
   verglichen mit pulsierenden Wellen und kreisenden Funken. Blitzt bei Treffern und
   Wandkontakt auf (Glow entlang der Kanten); bei Panzerung erscheint es kurz.
   Treffer sendet der Server nicht sichtbar – Erkennung: Schuss war im letzten Frame am Schiff
   (< 30 px) und hat keinen Nachfolger in Flugweite, Schiff lebt noch; Wandkontakt aus der
   Stoßerkennung; eigenes Schiff ohne Schild exakt über den sinkenden Panzerungszähler.
   Ursprüngliche Idee: Energieblase, weicher Rand mit leichtem Schimmern, blitzt bei Treffern oder
   Wandkontakt kurz auf. (Erweitert „Schild-Schimmern“ unter „Weitere Ideen“.)
6. ✅ **Hyperjump** → Option `fxHyperjump` (Streckeffekt, Variante B von drei verglichenen:
   Implosion, Streckeffekt, Lichtwirbel). Absprung: das Schiff zieht sich in Flugrichtung zu
   einem Lichtstreifen lang (Umriss bis 3×, Streifen ~140 px) und ist weg, vorne ein kurzer
   Blitz (0,22 s). Ankunft: ein langer Streifen (~360 px) schießt von hinten heran und zieht
   sich in 0,25 s ins bis zu 6× gestreckte Schiff zusammen, Lichtblitz beim Ankommen, danach
   glüht der Umriss 1 s nach (folgt dem Schiff). Erkennung ohne Protokoll: ein Schiff ist von
   einem Frame zum nächsten weit woanders, ohne zu explodieren. Eigenes Schiff nur nach
   gedrückter Hyperjump-Taste (mit Item), fremde Schiffe nur, wenn kein Wurmloch (siehe 6a)
   an der alten Position ist. Den eigenen Absprung sieht man kaum, weil die Ansicht mitspringt.
   Offen: Erscheinen nach Respawn.
6a. ✅ **Wurmlöcher** → Option `fxWormholes`.
   - *Aussehen*: statt des Kartenpolygons (je nach Karte Textur, Kreis oder Quadrat) ein
     Tunnel aus glühenden rot-orangen Ringen, die laufend in die Tiefe wandern und zu einem
     langsam kreisenden Fluchtpunkt schrumpfen (verglichen: Spiralwirbel, Akkretionsring,
     Tunnel). Reine Ausgänge mit Polygon: Ringe kommen heraus; Ausgänge ohne Polygon bleiben
     unsichtbar. Blockkarten: statt der drei roten Ringe.
   - *Durchflug*: Eintritt – rot-oranger Lichtwirbel dreht sich nach innen, der Umriss des
     Schiffs wird drehend und schrumpfend hineingezogen, Blitz (0,5 s). Austritt – Blitz, der
     Wirbel wickelt sich langsam nach außen ab und strudelt gut 1 s nach, das Schiff glüht
     orange nach (0,9 s). Wurmlöcher führen immer zu einem anderen Wurmloch (zufällig unter
     out/normal, `wormholeStableTicks` lang dasselbe); „irgendwo hin“ ist nur der Hyperjump.
   - *Daten*: Polygon-Maps übertragen Wurmlöcher nur als Polygone. Der TNG-Server hängt sie
     deshalb an die TNG-Kartenobjekte an (`Map_objects_to_client`: SETUP_WORM_IN/OUT/NORMAL
     mit Position, nur bei `wormholeVisible`). Ältere TNG-Clients überspringen unbekannte
     Typen, NG-Clients bekommen den Anhang nicht. Das markierende Polygon findet der Client
     als kleinstes Polygon (< 300 px), dessen Mitte auf dem Wurmloch liegt.
   - Ohne die Daten (alter Server) bleibt alles wie bisher; Wurmlochsprünge fremder Schiffe
     gelten dann als Hyperjump.
7. ✅ **Wenig Treibstoff**: die Triebwerksflamme stottert (nur eigenes Schiff – fremden Treibstoff
   sendet der Server nicht). Ab `fuelWarning` zufällige Aussetzer über 1–4 Frames, häufiger und
   länger je leerer der Tank, ungleichmäßige Länge; bei vielen Aussetzern Fehlzündungen an der
   Düse (kleiner Blitz, 2–3 Funken, helle Rauchwölkchen – eigener Partikeltyp, der dunkle
   Explosionsrauch war auf Schwarz unsichtbar); bei leerem Tank (Server fliegt mit
   Restleistung) kürzere Flamme.

Bewusst nicht: Rollneigung in Kurven oder perspektivisches Verzerren der Form – das würde die
Kontur verändern und bei Kollisionen in die Irre führen.

Vorschlag für ein erstes Paket: gefüllter Rumpf mit Facetten (1+2) und Triebwerksflamme (4).

### Weitere Ideen
- **Bloom/Glow für Vektorlinien**: Linien mehrfach breit, additiv, mit wenig Alpha zeichnen.
  Ein erster Versuch als Fullscreen-Bloom (Welt in Render-Target, Helligkeitsschwelle per
  MOD-Potenzkurve c⁶, Downsample + Kawase-Blur, additiv zurück) war technisch sauber, aber der
  Effekt auf 1-px-Vektorlinien blieb minimal – wieder entfernt (siehe Git-Historie, Commit 8d01fc9).
  Lohnt sich eher mit echten Shadern oder dickeren Linien.
- ✅ **Minen Explosionen** Minen explodieren mit speziellem Feuerball, Clusterminen mit einem anderen Effekt, Nuklearminen auch mit speziellem Effekt, alle je nach Explosionsstärke kleiner oder größer
  → umgesetzt (`fxMineExplosions`, `fxNukeFlash`, `fxScreenShake`):
  - Erkennung: Mine verschwindet im Sichtbereich des Servers (`active_view`, nicht der evtl.
    größere gezeichnete `ext_view`) und an der Stelle tauchen Trümmer/Schüsse auf – Minen
    fehlen sonst auch ohne Explosion (Rand der Serversicht, `maxVisibleObjects` nach Nukes).
    Abgleich über Position. Im selben Frame:
    Trümmer im Umkreis, hochgerechnet mit `sparkProb` (normal ~110–140, nuklear > 400),
    Schüsse im Umkreis (Cluster ~27, nuklearer Cluster > 100; Clustermine ab 5 Schüssen, damit
    auch 4 Teile mit je ~7 erkannt werden). Stärke = Anteil an einer vollen Mine (aufgeteilte
    Minen: 1/2 → 0,5, 1/3 → 0,33, 1/4 → 0,25; zuerst ^0,7 mit 0,62/0,46/0,38, zu groß), über
    einer vollen Mine ^0,7; skaliert Größe, Menge, Reichweite; kleine Explosionen sind
    zusätzlich kürzer.
  - Normal: kompakter, heller Feuerball, Druckwelle, Funkenkranz, kurz.
  - Cluster: wie normal, aber hell bläulich (Feuer weiß → hellblau → tiefblau, blaue Glut und
    Funken). Ein erster Versuch mit flachem „Praxis“-Ring gefiel nicht.
  - Nuklear: greller Blitz über die ganze Sicht (abschaltbar), große Druckwelle, aufsteigender
    Feuerball, Atompilz aus angeleuchtetem Rauch (Stiel + Kappe). Nuklearer Cluster: Pilz plus
    große blaue Explosion.
  - Bildschirmwackeln bei jeder Nuke auf der ganzen Map, für alle gleich stark: ausgelöst über
    den Nuke-Sound, den der Server allen schickt (Serveroption `sound`), sonst über die
    Erkennung im Bild; pro Nuke einmal. Nur die Kamera wackelt, die Seitenleiste nicht.
  - Raketen/Torpedos: später (gleiche Erkennung).
- **Phosphor-Nachleuchten (Vektormonitor-Modus)**: in Textur rendern, vorigen Frame nur abdunkeln
  statt löschen → Asteroids-/Vectrex-Look. Als Option.
- **Dynamisches Licht**: Explosionen, Schubdüse und Laser hellen nahe Wände kurz auf
  (radialer Gradient auf der Wandfüllung).
- **Brandspuren (Decals)**: dunkle Flecken am Boden, die langsam verblassen.
- ✅ **Bessere Schubflamme**: flackernder Kegel mit Hitzefarbverlauf statt nur Debris-Punkten.
  → umgesetzt als Triebwerksflamme (`fxEngineFlame`), siehe „Schiffe aufhübschen“.
- ✅ **Schild-Schimmern**: Hexagonmuster, das beim Treffer aufblitzt.
  → umgesetzt als Hexagon-Schild (`fxShield`), siehe „Schiffe aufhübschen“.
- ✅ **Raketen-Rauchfahnen** (`fxMissileTrails`): Typ an der gezeichneten Länge erkennbar
  (Serveroption `distinguishMissiles`, Standard an): Smart Missile (12) gelber Antrieb + dicker
  weißer Qualm, Heat-Seeker (15) rot-orange Flamme + dunkler, schmaler Rauch, Torpedo (18) blaues
  Glühen + dünne hellblau-graue Spur. Raketen haben keine Kennung: pro Frame wird die Fahne vom nächsten
  Heck gleicher Länge im Vorframe fortgesetzt, Rauch gleichmäßig über die Strecke verteilt.
  Verglichen mit Rauchwölkchen einheitlich und glattem Rauchband.
- **Mündungsfeuer** an Kanonen, 
- ✅ **Laser-Glow** → Option `fxLaserGlow`: Laserpulse als weiche Leuchtkapsel aus drei
  Schichten (farbiger Saum ~8 px, überwiegend farbiger Kern ~2 px, dünne weiße Mitte),
  additiv. Team/eigene Pulse in kräftigerem Blau (`#4073FF`) als die Palette, Gegner in sattem Rot (`#FF140A`, Kern weniger weiß).
- ✅ **Phasing** Schiff stark durchsichtig
  → das normale Glanzschiff zu 30 % deckend (Teil von `fxShipHull`).
- ✅ **Cloak** Hübschere Linie, ähnlich gut gezeichnet wie Schild und Deflektor
  → weich leuchtende Striche (5/4 px), die langsam entlang der exakten Shipshape wandern,
    mit breitem Lichthof, darunter der Rumpf zu 25 %. Classic: weiter der alte gestrichelte Umriss.
- **Lichtgeschwindigkeit**: Langsam deutlich werdender Überschallkegel, später auch stärker werdende Leuchtspur
  → Prototyp gebaut und wieder zurückgenommen, nicht im Code (`fxLightspeed`, Schwelle
  `fxLightspeedStart`, Standard 300 px/s, im Spiel einstellbar):
  Bugwelle vor dem Schiff (Variante „B“), weiche S-Kurve von 1× bis 2× Schwelle: erst kurz,
  schmal und dunkelblau, dann heller, breiter, länger; mit steigendem Tempo spitzer (Machkegel).
  Kometenschweif mit hellem Kern von 1,67× bis 3,33× Schwelle. Beides folgt der Flugrichtung
  (geglättete Geschwindigkeit), nicht der Nase. Keine harten Schwellen.
  Messung Bots: killing 95 % < 361 px/s (max 474), tourmination max 320 px/s.
- ✅ **Sternenfeld** → Option `fxStars`: ersetzt die blauen Navigationspunkte
  (`backgroundPointDist`) durch ein dezentes statisches Sternenfeld. Die Sterne sitzen fest an
  der Karte und scrollen mit den Wänden, ohne Parallaxe. Ein Stern pro Rasterzelle (64 px) mit
  36 % Wahrscheinlichkeit, Lage/Helligkeit/Tönung (3/8 weiß, 2/8 blau, je 1/8 orange, rötlich, violett; erste Fassung mit kaum sichtbaren Farben) aus einem
  Hash der Zelle – nichts wird gespeichert, und das Muster passt exakt über den Kartenrand.
  Größe (1,8–5,2 px Halbmesser, meist klein, wenige große) und Helligkeit sind unabhängig
  voneinander. Meist matt, einzelne heller (erste Fassung mit 30 % und schwächeren Sternen war zu dezent). Gezeichnet direkt nach dem Löschen des Bildes, unter Karte und
  Hügeln. Erster Versuch war ein Parallax-Feld mit drei Ebenen; gefiel nicht und wurde
  verworfen.
- ✅ **Minen** → Option `fxMines`: neue Texturen `mine_team_fx.ppm` / `mine_other_fx.ppm` (21×15,
  wie die Originale) mit 3D-Schattierung – dieselbe Textur und Silhouette, aber wie eine Kuppel
  mit Licht von oben links gerechnet (dunklerer Rand, Glanzlicht, schwaches Reflexlicht unten).
  Gilt nur mit Textur-Objekten; mit `classicGraphics` bzw. `fxMines` aus kommen die
  Originaltexturen. Stärken verglichen: leicht / mittel / kräftig, genommen wurde „kräftig“.
  Der erste Versuch (neu gezeichnete Metallkapsel mit Rand-Leuchten, drehende Wurfminen) wurde
  verworfen: zu weit weg vom Original.
- ✅ **Ball** → Option `fxBall`: glänzende Kugel (Variante A von drei verglichenen: glänzend,
  leuchtend, gemustert): feiner Verlauf mit Licht von oben links, schwacher Widerschein unten,
  scharfes plus breites Glanzlicht, dunklerer Rand. Der Rand ist weich anti-aliased (4×4
  Samples, keine zusätzliche Weichzeichnung wie in der Vorschau; Rampen von 1,7 und 1 px
  waren zu weich) – das geht nur mit Alphakanal, deshalb wird die Kugel beim ersten
  Gebrauch pro Ballfarbe zur Laufzeit als Textur gerechnet (23×23, max. 16 Farben) statt aus
  `ball_gray16.ppm` gelesen (PPM kennt nur durchsichtig/deckend). Farbe aus dem Kartenstil, der
  Standardball (Stil 0xff) grün. Zurückgestellt: Lichtschein (B) und rollendes Muster (C).
- ✅ **Ball-Tether-Glühen** → Option `fxTether`: das Ballseil als mattes blaues Leuchtseil mit
  hellem Kern, in ~6-px-Stücken, die minimal zittern (leichtes Flimmern); der Ball hat einen
  schwachen Lichthof. Über die Ruhelänge gedehnt (`ballConnectorLength`, Standard 120 – der
  Server schickt sie nicht) verfärbt es sich nach Rot, voll bei der maximalen Dehnung
  (`maxBallConnectorRatio` 0,3). Verglichen: Leuchtseil, Spannung (dünner/heller/zitternd),
  Energiefluss (Lichtperlen). Traktor-/Pressorstrahlen als dasselbe Leuchtseil, aber immer blau
  (keine Ruhelänge, also keine Spannung).
- **Positionslichter** (aus „Schiffe aufhübschen“ verschoben): rot/grün an den Flügelspitzen
  (`markingLights`) mit Glow-Sprite statt Quadrat + Kreuz. Verglichene Varianten: Glow-Punkt
  (sanftes Auf-/Abblenden), Glow + Kreuzstrahl (Linsenreflex, behält das Kreuz-Motiv),
  Dauerlicht + weißer Strobe-Blitz (realistisch, bei vielen Schiffen unruhig).
- **Highscores** Leicht durchsichtig im FX Modus

## Architektur

1. **`src/client/sdl3/sdl3_fx.c`**: zentrales Partikel-/Effektsystem
   - feste Pools, kein malloc pro Frame
   - zeitbasiert (unabhängig von FPS und Server-Ticks)
   - Blend-Modus pro Ebene (additiv / normal)
2. **Prozedurale Texturen** beim Start: Soft-Glow-Punkt, Feuerball-Sheet, Funke.
3. **Event-Erkennung**: dünne Schicht zwischen `paintobjects.c` und FX-System.
4. **Optionen**: jeder Effekt einzeln schaltbar + Preset „Klassisch / Modern“.
   Der klassische Look muss erhalten bleiben (auch für kompetitive Spieler).
5. **Später**: echte Shader (Bloom, Schockwellen-Verzerrung) über `SDL_GPURenderState`
   (ab SDL 3.4), ohne den Renderer umzuschreiben – Verfügbarkeit in unserer SDL-Version prüfen.

## Reihenfolge (Vorschlag)

1. ✅ FX-System + Funken + Feuerbälle — eigener Reiter „FX“ im Menü: `classicGraphics` (alles aus), `fxExplosions`, `fxSparks`, `fxAmount`
2. ✅ Kondensstreifen (`fxContrails`) — Glow verschoben zu „Weitere Ideen“
3. ✅ Hügellandschaft in Wänden (`fxTerrain`, `fxTerrainMesh`), nur Polygonkarten
4. Restliche Ideen nach Lust und Laune
