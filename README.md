# demute-imgui

Bibliothèque d'interface commune aux outils Demute (RAV, RAW) : thème, widgets et icônes Dear ImGui.
Licence MIT. Nom provisoire.

## Cibles CMake

- `theme` : thème, widgets, mise en page adaptative, icônes vectorielles. Aucune inclusion de REAPER ni de `windows.h`.
  Fournit Dear ImGui (cœur, sans backend), épinglée à 1.92.9b par `FetchContent`.
- `host` (story 1.2, à venir) : panneau docké, fenêtres flottantes, contexte GL.

## Tests (sans fenêtre)

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure

Le build par défaut est Debug : les assertions d'ImGui sont actives. `theme_headless_test` crée un contexte
ImGui sans backend, applique le thème et dessine des frames avec chaque widget et chaque icône.

## Origine

Fichiers repris de FbxAnimationViewer (RAV) au commit 283dbee, importés tels quels au premier commit
(voir `git log`), puis adaptés à ImGui 1.92 et découplés de RAV.
