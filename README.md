# demute-imgui

Bibliothèque d'interface commune aux outils Demute (RAV, RAW) : thème, widgets et icônes Dear ImGui.
Licence MIT. Nom provisoire.

## Cibles CMake

- `theme` : thème, widgets, mise en page adaptative, icônes vectorielles. Aucune inclusion de REAPER ni de `windows.h`.
  Fournit Dear ImGui (cœur, sans backend), épinglée à 1.92.9b par `FetchContent`.
- `host` (Windows seulement) : panneau docké (`rav::host::DockedPanel`), fenêtres flottantes (`rav::FloatUiWindow`),
  contexte OpenGL et contexte ImGui d'une fenêtre, polices Windows. Dépend de `theme` et des backends Win32 et
  OpenGL3 d'ImGui (`demute_imgui_backends`). Aucune inclusion du SDK REAPER : REAPER est joint par des services
  injectés. Hors Windows, la cible n'est pas configurée ; ses parties pures (`host_prefs.h`, `float_geometry.h`,
  `float_key_route.h`) et les services sont testés partout (`host_pure_test`).

Alias : `demute_imgui::theme`, `demute_imgui::host`.

## Tests (sans fenêtre)

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure

Le build par défaut est Debug : les assertions d'ImGui sont actives. `theme_headless_test` crée un contexte
ImGui sans backend, applique le thème et dessine des frames avec chaque widget et chaque icône.

## Cible `host`

### Services à injecter (une fois, à l'entrée du plugin)

```cpp
#include "host_services.h"

rav::host::HostServices s;
s.get_ext_state = GetExtState;      // ou s.load_pref_float / s.save_pref_float (les fonctions de l'outil)
s.set_ext_state = SetExtState;      // écrit "%.1f", persist = true
s.ext_section   = "MonOutil";       // copiée
s.dock_add_ex   = DockWindowAddEx;
s.dock_activate = DockWindowActivate;
s.dock_remove   = DockWindowRemove;
s.log = [](rav::host::LogLevel level, const char* text) { /* console de l'outil */ };
rav::host::SetHostServices(s);
```

Les signatures sont celles des pointeurs de REAPER : on les assigne directement. Sans services (ou un service
nul), une préférence vaut sa valeur par défaut et une écriture, un appel au docker ou une ligne de log ne font
rien. Les services sont globaux : les `FloatUiWindow` construites en statiques globales gardent leur constructeur.

### Panneau docké

`DockedPanel(config, callbacks)` : classe de fenêtre, titre, `dock_name` (onglet), `dock_ident` (clé de position du
docker, à ne jamais changer), taille initiale, timer de 15 ms. L'outil branche ses étapes par callbacks, dans
l'ordre de RAV : contexte GL → `on_gl_ready` (false = échec) → vsync 0 → timer → ImGui (non fatal) →
`on_imgui_ready` → un tick. Masqué par le docker, le panneau se met en pause (`on_parked`, puis `on_hidden_tick`)
sans être détruit ; `on_resumed` au retour. `Open` / `Close` / `Toggle` ne lèvent jamais d'exception.
`SetDocked(false)` : hors docker (plein écran), `Open` met au premier plan et `Close` n'appelle pas
`DockWindowRemove`.

### Ordre de destruction

1. `on_before_shutdown` (GL courant, contexte ImGui encore là) : détruire chaque `FloatUiWindow`
   (`Destroy`), elles partagent l'atlas de polices et le contexte GL du panneau.
2. Backends OpenGL3 puis Win32, puis le contexte ImGui (le propriétaire de l'atlas se détruit en dernier ; ImGui
   1.92 ne détruit une texture que lorsqu'un seul contexte l'utilise encore).
3. `on_gl_shutdown` (GL encore courant) : objets GL de l'outil.

`on_before_shutdown` et `on_gl_shutdown` sont aussi appelés après une ouverture ratée (pas de contexte GL,
`on_gl_ready` faux, ImGui pas encore créé), comme le `StopRendering` de RAV : ils doivent tolérer un état à
moitié construit (`Rc()` / `ImGuiCtx()` peuvent être nuls).
4. Contexte GL et DC.

Après la fermeture de toutes les fenêtres : `FloatUiWindow::UnregisterWindowClass()` (au déchargement de la DLL).

### Clavier des fenêtres flottantes

Dans le crochet d'accélérateurs de l'outil (`accelerator_register_t`, créé par l'outil) :

```cpp
bool is_float = false;
const int route = rav::FloatUiWindow::RouteKey(msg, &is_float);
if (is_float) return route;  // 0 REAPER, -1 la fenêtre, -20 la fenêtre (touche système)
```

### Consommer la bibliothèque depuis un dossier local

    FetchContent_Declare(demute_imgui SOURCE_DIR /chemin/vers/demute-imgui)
    FetchContent_MakeAvailable(demute_imgui)
    target_link_libraries(mon_outil PRIVATE demute_imgui::host)

RAW : `-DRAW_WITH_UI=ON -DRAW_DEMUTE_IMGUI_SOURCE=/chemin/vers/demute-imgui` (voir `cmake/raw_dependencies.cmake` de RAW). Le projet
qui consomme fixe son `CMAKE_MSVC_RUNTIME_LIBRARY` ; compilée seule, la bibliothèque prend `/MT` (`/MTd` en Debug).

### Compilation Windows (MSVC)

    cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
    cmake --build build-msvc --config Release
    ctest --test-dir build-msvc -C Release --output-on-failure

`host_compile_check` référence toute l'API de `host`, n'ouvre aucune fenêtre et renvoie 0.

### Compilation croisée de contrôle (MinGW, sous Linux / WSL)

    cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake -DMINGW_ROOT=<préfixe>
    cmake --build build-mingw -j8

Vérifie que `host` et `host_compile_check.exe` compilent et lient (les exécutables ne sont pas lancés).

## Origine

Fichiers repris de FbxAnimationViewer (RAV) au commit 283dbee, importés tels quels au premier commit
(voir `git log`), puis adaptés à ImGui 1.92 et découplés de RAV.
La cible `host` reprend `float_ui_window` (importé tel quel, RAV 47558f9) et la partie docking / GL / ImGui de
`viewer_window.cpp`.

## Contrat pour l'hôte (ImGui 1.92)

- Chaque rôle de police (`ui::FontSet`) est un `ImFont*` ajouté à sa propre taille (`SizePixels` > 0), car le
  thème lit la taille dans `ImFont::LegacySize`. Utiliser `ui::FontScope`, pas `ImGui::PushFont(font)`.
- Le backend de rendu doit gérer les textures de 1.92 (`ImGuiBackendFlags_RendererHasTextures`) et les
  décalages de sommets (`RendererHasVtxOffset`).
- `SoundNodeType` et `FoldScopeButtons` sont copiés de RAV ; RAV supprimera ses doublons au passage (story 1.3).
