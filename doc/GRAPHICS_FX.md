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

### Weitere Ideen
- **Bloom/Glow für Vektorlinien**: Linien mehrfach breit, additiv, mit wenig Alpha zeichnen.
  Ein erster Versuch als Fullscreen-Bloom (Welt in Render-Target, Helligkeitsschwelle per
  MOD-Potenzkurve c⁶, Downsample + Kawase-Blur, additiv zurück) war technisch sauber, aber der
  Effekt auf 1-px-Vektorlinien blieb minimal – wieder entfernt (siehe Git-Historie, Commit 8d01fc9).
  Lohnt sich eher mit echten Shadern oder dickeren Linien.
  **Minen Explosionen** Minen explodieren mit speziellem Feuerball, Clusterminen mit "Praxis-Effekt" (scheibenförmig), Nuklearminen auch mit speziellem Effekt, alle je nach Explosionsstärke kleiner oder größer
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

1. ✅ FX-System + Funken + Feuerbälle — eigener Reiter „FX“ im Menü: `classicGraphics` (alles aus), `fxExplosions`, `fxSparks`, `fxAmount`
2. ✅ Kondensstreifen (`fxContrails`) — Glow verschoben zu „Weitere Ideen“
3. ✅ Hügellandschaft in Wänden (`fxTerrain`, `fxTerrainMesh`), nur Polygonkarten
4. Restliche Ideen nach Lust und Laune
