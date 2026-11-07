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

Konzept:
1. **Stützpunkte**: Gejittertes Punktraster innerhalb des Polygons + die Randvertices.
2. **Triangulierung**: Delaunay (oder einfacher: Raster am Polygon geclippt).
3. **Höhe**: `h = f(Abstand zum Rand) + Noise` → Rand liegt auf Höhe 0, zur Mitte hin
   Hügel/Bergrücken. Seed aus Map-Hash → sieht bei jedem Start gleich aus.
4. **Schattierung**: Lambert pro Dreieck (Normale · feste Lichtrichtung, z. B. von oben links),
   Füllfarbe abgedunkelt/aufgehellt.
5. **Linien**, als Stil wählbar:
   - Dreieckskanten dezent (Wireframe-/„Tron“-Hügel)
   - Höhenlinien / Isolinien (topographischer Karten-Look)
   - Nur Grat-Linien (Kanten mit starkem Normalenwechsel)
6. **Performance**: Einmal beim Map-Laden vorberechnen, in Chunks ablegen, nur sichtbare Chunks per
   `SDL_RenderGeometry` in einem Batch zeichnen.

Offene Punkte:
- **Block-Maps**: zusammenhängende Wandblöcke als Region behandeln, Distanz-Transformation auf dem
  Blockraster statt Polygon-Abstand. **AR** Nur für Polygonkarten
- **Map-Wrapping** an den Rändern berücksichtigen.  **AR** Gewrappte Polygone sollten nicht anders aussehen als normale
- Später kombinierbar mit dynamischem Licht (Explosionen hellen Hänge auf).

### Weitere Ideen
- **Bloom/Glow für Vektorlinien**: Linien mehrfach breit, additiv, mit wenig Alpha zeichnen.
- **Phosphor-Nachleuchten (Vektormonitor-Modus)**: in Textur rendern, vorigen Frame nur abdunkeln
  statt löschen → Asteroids-/Vectrex-Look. Als Option.
- **Dynamisches Licht**: Explosionen, Schubdüse und Laser hellen nahe Wände kurz auf
  (radialer Gradient auf der Wandfüllung).
- **Brandspuren (Decals)**: dunkle Flecken am Boden, die langsam verblassen.
- **Bildschirmwackeln**: bei eigenem Treffer oder naher Explosion, dezent, abschaltbar.
- **Bessere Schubflamme**: flackernder Kegel mit Hitzefarbverlauf statt nur Debris-Punkten.
- **Schild-Schimmern**: Hexagonmuster, das beim Treffer aufblitzt.
- **Raketen-Rauchfahnen**, **Mündungsfeuer** an Kanonen, **Laser-Glow**.
- **Parallax-Sternenfeld** im Hintergrund (mehrere Ebenen).
- **Wurmloch-Strudel**, **Hyperjump-Streckeffekt**, **Ball-Tether-Glühen**.

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

1. ✅ FX-System + Funken + Feuerbälle — Optionen `fxExplosions`, `fxSparks`, `fxAmount`
2. Glow + Kondensstreifen
3. Hügellandschaft in Wänden (aufwendigster Teil: Polygon- und Block-Maps getrennt)
4. Restliche Ideen nach Lust und Laune
