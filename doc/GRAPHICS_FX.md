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
6. **Erscheinen / Hyperjump**: Lichtwirbel oder Implosion beim Auftauchen und Springen.
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
    Schüsse im Umkreis (Cluster ~27, nuklearer Cluster > 100). Stärke = Anteil an einer vollen
    Mine ^0,7 (aufgeteilte Minen: 1/2 → 0,62, 1/3 → 0,46, 1/4 → 0,38) skaliert Größe, Menge,
    Reichweite; kleine Explosionen sind zusätzlich kürzer.
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
  Glühen, fast rauchlos. Raketen haben keine Kennung: pro Frame wird die Fahne vom nächsten
  Heck gleicher Länge im Vorframe fortgesetzt, Rauch gleichmäßig über die Strecke verteilt.
  Verglichen mit Rauchwölkchen einheitlich und glattem Rauchband.
- **Mündungsfeuer** an Kanonen, **Laser-Glow**.
- **Lichtgeschwindigkeit**: Langsam deutlich werdender Überschallkegel, später auch stärker werdende Leuchtspur
- **Parallax-Sternenfeld** im Hintergrund (mehrere Ebenen).
- **Wurmloch-Strudel**, **Hyperjump-Streckeffekt**, **Ball-Tether-Glühen**.
- **Positionslichter** (aus „Schiffe aufhübschen“ verschoben): rot/grün an den Flügelspitzen
  (`markingLights`) mit Glow-Sprite statt Quadrat + Kreuz. Verglichene Varianten: Glow-Punkt
  (sanftes Auf-/Abblenden), Glow + Kreuzstrahl (Linsenreflex, behält das Kreuz-Motiv),
  Dauerlicht + weißer Strobe-Blitz (realistisch, bei vielen Schiffen unruhig).

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
