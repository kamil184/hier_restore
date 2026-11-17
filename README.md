# hier_restore

Nach der Synthese zeigt der NetlistViewer in OneWare alle Zellen in einem einzigen Rahmen, weil yosys die Module auflöst.
Das yosys-Plugin `hier_restore` verteilt die Zellen wieder auf die Instanzen, aus denen sie stammen, und baut daraus wieder Module.
Im NetlistViewer gibt es dafür den Menüpunkt **View Post-Synthesis Netlist with Restored Hierarchy**.

Wie das Plugin arbeitet, erklärt [diese Seite](https://claude.ai/artifact/Rw7ZtZkj9RWBGjyMDF7cRr) am Beispiel des Projekts `demo`.

## Installation

Vorausgesetzt sind OneWare mit der OSS CAD Suite 2026.08.17 und dem FEntwumS NetlistViewer aus dem Paketmanager.
Das Plugin passt nur zu der yosys-Version, mit der es gebaut wurde.

1. Unter [Releases](../../releases) die Datei des Plugins für das eigene System herunterladen, `hier_restore-windows-x64.so` oder `hier_restore-macos-arm64.so`, und an einem festen Ort ablegen.
2. OneWare schließen und den NetlistViewer mit dem neuen Menüpunkt aus [unserem Fork](https://github.com/kamil184/FEntwumS.NetlistViewer/tree/restored-hierarchy-view) bauen, dafür wird das .NET 10 SDK gebraucht.
   Die Ausgabe ersetzt den installierten NetlistViewer im Benutzerordner:

   ```sh
   git clone -b restored-hierarchy-view https://github.com/kamil184/FEntwumS.NetlistViewer
   dotnet build FEntwumS.NetlistViewer/src/FEntwumS.NetlistViewer/FEntwumS.NetlistViewer.csproj -c Release -o ~/OneWareStudio/Packages/Plugins/FEntwumS.NetlistViewer
   ```

   Unter Windows ist der Zielordner `%USERPROFILE%\OneWareStudio\Packages\Plugins\FEntwumS.NetlistViewer`.
3. OneWare starten und unter **Settings → Netlist Viewer → Experimental → Path to hier_restore yosys plugin** den Pfad zur heruntergeladenen Datei eintragen.
4. Im Projekt mit der rechten Maustaste auf die Datei der Top-Entity klicken und **View Post-Synthesis Netlist with Restored Hierarchy** wählen.

Aktualisiert OneWare den NetlistViewer, wird Schritt 2 überschrieben und muss wiederholt werden.

## Beispiel

Der Ordner `demo` ist ein OneWare-Projekt für das iCEBreaker-Board, dafür wird das Paket *iCEBreaker Support* gebraucht.
Es ist das Beispiel der Erklärseite: ein Zähler `u_cnt` und eine ALU `u_alu` mit den Instanzen `u_add` und `u_logic`.

## Selbst bauen

Auf macOS und Linux mit der OSS CAD Suite im `PATH`:

```sh
make
```

Für Windows baut `scripts/build-windows.sh` das Plugin unter Linux mit dem MinGW-Compiler, mit dem auch die OSS CAD Suite für Windows gebaut ist.

Ohne OneWare läuft das Plugin direkt in yosys:

```sh
yosys -m ./hier_restore.so -p "read_verilog demo/design.v; synth_ice40 -top top; hier_restore; write_json netlist.json"
```

`make test` prüft das Plugin an den Designs in `tests`.
