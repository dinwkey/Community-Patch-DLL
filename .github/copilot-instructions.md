# Copilot / AI assistant instructions for Community-Patch-DLL

Purpose: give targeted, actionable guidance to code-generating assistants so they can be immediately productive in this repository.

File creation safety
- ALWAYS create or modify files only under the repository workspace paths shown in the workspace view (for example, the repo root and its subfolders). Do NOT create files using incorrect absolute prefixes such as `C:\c\Users\...` or any path outside the workspace.
- Use repository/workspace-relative paths when writing files and prefer the exact repository path shown by your workspace view (for example, `C:\Users\Thomson\source\repos\Community-Patch-DLL\...`).
- When updating files programmatically, use the repository's `apply_patch` mechanism or other workspace-aware APIs so changes land inside the workspace. After writing, verify the file appears in the workspace listing.
- If a path looks unexpectedly absolute or contains doubled roots like `C:\c\Users`, stop and ask for confirmation before writing.

## Skills for VS Code use

Use `.github/copilot-instructions.md` for always-on repository constraints (toolchain, safety, ABI, path rules).
Use task-focused Skills for procedural workflows:

- `.github/skills/save-serialization-compat.md` — adding/removing/changing serialized fields with backward compatibility.
- `.github/skills/tactical-ai-debugging.md` — tactical/homeland AI movement/combat debugging workflow.
- `.github/skills/build-and-log-triage.md` — choosing build mode, running builds, and triaging logs.

Start with the relevant skill and return here for global constraints.

- **Big picture:** This repo builds a modified Civ V game core DLL (C++), plus a set of mods (Lua/SQL/XML). The C++ game core lives in `CvGameCoreDLL_Expansion2` and is linked with helper projects (`CvGameCoreDLLUtil`, `CvGameDatabase`, `CvLocalization`, `FirePlace`, `ThirdPartyLibs`). The playable mods and content live in the top-level mod folders (e.g. `(1) Community Patch/`, `(2) Vox Populi/`) — most gameplay changes are in those files and do not require rebuilding the DLL.

 - **Primary build paths (MANDATORY VC9 compilation):**
   - Visual Studio (required for official builds): open `VoxPopuli_vs2013.sln` (or `Vox Populi.civ5sln`) and build `DEBUG` / `RELEASE`. You MUST use the Visual C++ 2008 SP1 (VC9) toolset for linking and producing the final `CvGameCore_Expansion2.dll`. If Visual Studio prompts to retarget projects, choose **No Upgrade**.
     - Rationale: Civilization V requires binaries produced with the VC9 runtime and toolset for correct ABI and compatibility.
     - See `DEVELOPMENT.md` for troubleshooting around precompiled headers (PCH), Whole Program Optimization (WPO), and the hidden `.vs` folder.
   - Clang-based script (`build_vp_clang.ps1`): **preferred for iterative development and faster local builds.** It invokes `clang-cl`/`lld-link` and can be configured to target the Visual C++ 2008 (VC9) ABI while compiling as a C++03/TR1-compatible toolchain; use it for fast verification and CI-style checks. Despite this preference, Visual Studio / MSVC 2008 remains the authoritative toolchain for official builds and final linkage — always verify VC9 compatibility when preparing release artifacts.
      - Example: `.\build_vp_clang.ps1 -Config release` (default config is `debug`).

- **Key files and examples:**
  - Game core C++ sources: `CvGameCoreDLL_Expansion2/` (huge list of .cpp/.h files). Precompile unit `CvGameCoreDLL_Expansion2/_precompile.cpp` and PCH header `CvGameCoreDLLPCH.h` are used heavily.
  - Build script: `build_vp_clang.ps1` — read it for compiler/linker flags, include dirs, `LIBS` and `DEFAULT_LIBS` used for linking.
  - Commit id helper: `update_commit_id.bat` (invoked by the clang build script).
  - 43-Civ toggle: edit `CvGameCoreDLLUtil/include/CustomModsGlobal.h` as described in `DEVELOPMENT.md`.

- **Debugging workflow (exact steps):**
  1. Build `DEBUG` configuration in Visual Studio.
 2. Copy the generated `CvGameCore_Expansion2.dll` and its `.pdb` from `BuildOutput` (or `clang-output/<config>`) into the mod folder (Community Patch Core) to replace the mod's dll.
 3. Launch `Civilization5.exe` and in Visual Studio: Debug > Attach to Process > select `Civilization5.exe`.
 4. Enable logging for deeper clues by editing `My Games\Sid Meier's Civilization V\config.ini` and turning on the listed logging flags (see `DEVELOPMENT.md`). Logs appear in `My Games\Sid Meier's Civilization V\Logs`.

- **Copying Lua/XML/SQL to game folders (follow `VPSetupData.iss` paths):**
  - **MODS root:** `Documents\My Games\Sid Meier's Civilization 5\MODS`.
  - **Steam DLC root:** `...\Steam\steamapps\common\Sid Meier's Civilization V\Assets\DLC`.
  - **With EUI (FullEUI / Civ43EUI):**
    - Do **NOT** copy `(1) Community Patch\LUA` or `(2) Vox Populi\LUA` into MODS (installer excludes them).
    - Copy Lua changes for EUI UI into **(3a) VP - EUI Compatibility Files\LUA** under MODS.
    - Copy UI changes for EUI core into **DLC\UI_bc1\...** (e.g., `Core\EUI_core_library.lua`, `CityBanners\CityBannerManager.lua`, `CityView\CityView.lua`, `UnitPanel\UnitPanel.lua`).
    - Copy SQL/XML text changes into the relevant MODS subfolders (e.g., `(2) Vox Populi\Database Changes\Text\...`).
  - **Without EUI (FullNoEUI / Civ43NoEUI):**
    - Copy `(1) Community Patch\LUA` and `(2) Vox Populi\LUA` into MODS.
    - Do **NOT** copy UI_bc1 to DLC.
  - Always mirror the installer destinations in `VPSetupData.iss` (not the repo folder names) when copying to game folders.

- **Repo conventions & patterns to follow:**
  - Large, monolithic C++ codebase using precompiled headers and many translation units — prefer small, localized edits and rebuild only what you must.
  - Most gameplay-level changes live in the mod folders (Lua/SQL/XML); modify those when you can to avoid rebuilding DLL.
  - Follow existing naming conventions: core module prefix `Cv` (e.g. `CvPlayer.cpp`, `CvUnit.cpp`) and serialization helpers (`CvGameCoreEnumSerialization.cpp`). Use existing helper functions rather than duplicating logic.

- **Integration points & external deps:**
  - Lua (ThirdPartyLibs/Lua51) — used for game scripting.
  - FirePlace, CvGameDatabase, CvLocalization projects — they build as libs/objects and are linked into the DLL (see `build_vp_clang.py` `LIBS`).
  - FireTuner (SDK tool) can be used for autoplay when debugging AI.

- **Common pitfalls to avoid:**
  - Opening the solution and accepting automatic upgrade may break the historic VC9 configuration — choose **No Upgrade**.
  - Whole Program Optimization (WPO) causes long stalls in Release builds; it can be disabled per-project in VS for faster iteration.
  - If build/link errors mention missing VC9 headers, ensure VC++ 2008 SP1 toolset is installed.

- **Strict compilation rule for agents:**
  - Prefer using the clang-based workflow (`build_vp_clang.ps1` / `clang-cl`) for local development and CI checks because it gives faster iteration. **However, all code and binaries must be written and verified to target Visual C++ 2008 SP1 (VC9) ABI and language constraints (C++03 with only TR1 where already available).** Do not assume modern MSVC toolsets (2015/2017/2019/2022) are compatible for release artifacts.
  - **Default build config rule:** always build **Debug** unless the user **explicitly** asks for a Release build. Even if the user reports a bug that occurs in Release, still run Debug by default to quickly validate build errors unless the user explicitly requests Release.
  - After making any change to C++ sources, run the clang-based build script to verify compilation before committing or opening a PR: `.\\build_vp_clang.ps1 -Config debug` (use `-Config release` only when the user explicitly requests a Release build). Confirm `clang-output/<config>/CvGameCore_Expansion2.dll` and `.pdb` are created and check `clang-output/<config>/build.log` for errors; if the build fails, fix locally and re-run until it succeeds. Additionally, when possible, validate the produced artifacts against a VC9 linker or perform an MSVC2008 build to confirm ABI/linkage compatibility. Add a short note in the commit/PR indicating the clang build passed (e.g., `clang-build: debug successful`) and note any VC9 verification performed.
  - **CRITICAL: Run the clang build blocking and wait for completion.** When invoking .\build_vp_clang.ps1 -Config debug, run it interactively in the foreground and stream its output until it finishes; do not start it in the background or detach it. The compilation of hundreds of .cpp files is CPU-intensive and can take 10-30 minutes. Do not interrupt the build once started — allow it to complete before performing follow-up checks or edits.
  - Required environment note: `VS90COMNTOOLS` must point to a valid VS2008 installation for tooling and some scripts (e.g., `build_vp_clang.ps1`) to work.

- **Incremental vs Full Build — when to use each:**
  - **Incremental build (`build_vp_clang.ps1`):** Faster, only recompiles `.cpp` files whose timestamps are newer than their `.obj` outputs. Use for quick iteration when you **only changed `.cpp` files** (no headers).
  - **Full build (`python build_vp_clang.py --config debug`):** Always rebuilds everything from scratch. Slower but guaranteed correct.
  - **MUST use full build when:**
    1. You changed **any header file (`.h`)** — the incremental script does NOT track header dependencies; it only checks `.cpp` → `.obj` timestamps. Changing a header will silently produce stale/incorrect `.obj` files.
    2. You changed the **PCH header (`CvGameCoreDLLPCH.h`)** — the incremental script rebuilds the PCH itself but does NOT invalidate all `.obj` files that depend on it.
    3. A **previous build was interrupted** (Ctrl+C, crash, timeout) — partial/corrupt `.obj` files may exist and appear "up-to-date" by timestamp.
    4. You **updated clang, MSVC SDK, or toolchain** — old `.obj` files may have ABI mismatches.
    5. You **changed preprocessor defines** in the build script (e.g., switching `VPDEBUG`/`NDEBUG`) — existing objects won't reflect the new defines. **CRITICAL: Always use full build when switching between Debug and Release configurations** — the incremental build will skip recompilation of `.cpp` files, silently linking Release against Debug-built `.obj` files that lack Release optimizations.
    6. You're seeing **unexplained link errors, runtime crashes, or "it worked before"** issues after incremental builds.
  - **Quick way to force full rebuild with PS1:** delete the build cache folder before running:
    ```powershell
    Remove-Item -Recurse -Force .\clang-build\Debug
    .\build_vp_clang.ps1 -Config debug
    ```
  - **Switching Debug ↔ Release: always use full build or clear cache.** The incremental build only tracks `.cpp` file timestamps and skips recompilation if `.obj` files are "newer." When you switch configs (Debug → Release or vice versa), preprocessor defines change but the incremental script doesn't know to recompile. Result: **Release DLL linked against Debug `.obj` files (no optimizations)** or Debug DLL with Release flags (mismatched behavior). Safe approach: use `python build_vp_clang.py --config release` for Release, or explicitly clean the cache first:
    ```powershell
    Remove-Item -Recurse -Force .\clang-build\Release
    .\build_vp_clang.ps1 -Config release
    ```
  - **Incremental build may hang/timeout** on large files (`CvDiplomacyAI.cpp`, `CvPlayer.cpp`) or under low disk space. The PS1 script has a 5-minute per-file and 10-minute total timeout. If builds hang, use the Python full build instead.

  - **MSBuild fallback (when to use):** Prefer the clang-based workflow, but fall back to MSBuild/VS2008 when failures appear to be unrelated to C++ syntax (for example: linker issues, resource compiler errors, ToolsVersion/VS project compatibility, or VC9 ABI/linker behavior that clang can't reproduce). When using MSBuild as a fallback, set the VC2008 environment first and use the .NET 4 MSBuild executable to drive the solution. Example PowerShell commands:

    ```powershell
    & "$env:VS90COMNTOOLS\..\..\VC\vcvarsall.bat" x86
    & 'C:\Windows\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe' .\VoxPopuli_vs2013.sln /m /p:Configuration=Debug /p:Platform=Win32 /v:m
    ```

  - **What to check after MSBuild:** ensure `BuildOutput\\Debug\\CvGameCore_Expansion2.dll` (or the Release path) is produced, and review the MSBuild log for linker/resource errors. Treat MSBuild as a verification step for platform/VC9-specific issues — keep clang as the primary fast/CI build.
- **OS-aware command execution (CRITICAL):**
  - ALWAYS check the current OS first before running commands. Refer to the `<environment_info>` to confirm whether the OS is Windows, macOS, or Linux.
  - On Windows: prefer PowerShell cmdlets and Windows-native tools (e.g., `Get-ChildItem`, `Test-Path`, MSBuild, batch/PowerShell scripts). Use `\` path separators as appropriate.
  - On non-Windows: use bash/sh equivalents (e.g., `ls`, `test -f`, Make, shell scripts) and forward-slash paths.
  - Do NOT run Linux commands (bash, grep, ls, etc.) on Windows without an explicit tool like WSL, Git Bash, or MinGW; instead use PowerShell equivalents.
  - Example: Instead of `ls -la`, use `Get-ChildItem -Force` on Windows; instead of `mkdir -p`, use `New-Item -ItemType Directory -Force` or PowerShell paths.
 **Build and log verification (for agents):**
   - **Run build:** start the clang script and run it *in the foreground* (blocking) so the process completes before taking any further actions. Example (PowerShell):
       - `\.\build_vp_clang.ps1 -Config debug`
     Run the command interactively and wait for it to finish; **do not** run background log-watchers that read the log while the build is active because they can interfere with or interrupt the build.
  - **Check logs after completion:** once the build finishes, inspect the last lines of the log to confirm success or capture errors:
    - `Get-Content clang-output/Debug/build.log | Select-Object -Last 200`
  - **Success checks:** verify either the DLL exists or the log contains a success marker such as `clang-build: debug successful`, `BUILD SUCCEEDED`, `Finished`, or `SUCCESS`.
  - **Failure triage:** if errors are suspected, search the log for `error`, `FAILED`, or `Traceback` and include the last ~200 lines when reporting back. Use `Select-String` to filter recurring patterns.

- **Where to look for more context:**
  - `DEVELOPMENT.md` — build, debug, profiling steps and tips.
  - `build_vp_clang.py` — exact compiler/linker flags, include paths, and build flow used by the clang-based build.
  - `CvGameCoreDLL_Expansion2/` — canonical examples for coding patterns and how systems are organized.

- **Generated documentation placement (new):**
  - **Do not** place generated Markdown files under `.github/` — that directory is for GitHub configs (workflows, issue/PR templates) and instructions only.
  - **Preferred output path:** `docs/<category>/` for hand-organized docs (e.g., `docs/military-ai/`, `docs/policies-ideologies/`) or `docs/generated/<category>/` for machine-generated artifacts. Use a consistent category-based layout so readers can find related docs easily.
  - **If generated files are large or regenerated in CI:** add them to `.gitignore` and generate/publish from CI (e.g., GitHub Actions → `gh-pages`), or commit them only when review is needed. Prefer CI publishing when possible.
  - **Workflows & generators:** update any generator scripts and GitHub Actions to write output to the `docs/` path and to publish from there (or to a `gh-pages` branch). Keep docs self-contained and ensure internal links use relative paths under `docs/`.

If anything here is unclear or you want more detail about a particular area (build flags, a subsystem, or common refactoring locations), tell me which piece to expand and I will update this file.

## Save Game Compatibility

When adding, removing, or changing serialized fields, you **must** maintain backward compatibility with older save files. Breaking serialization will crash the game on save load.

### Architecture overview

- **Global save version**: `CvGlobals::SaveVersionTags` enum in `CvGlobals.h` (~line 164). Current tags:
  ```cpp
  enum SaveVersionTags
  {
      SAVE_VERSION_PLOT_FREE_MOVE_ACROSS = 1,
      SAVE_VERSION_ATTACK_TARGET_FIELDS = 2,
      SAVE_VERSION_ESPIONAGE_SPY_NAME_REMOVAL = 3,
      SAVE_VERSION_LATEST = SAVE_VERSION_ESPIONAGE_SPY_NAME_REMOVAL,
  };
  ```
  `SAVE_VERSION_LATEST` is always aliased to the highest tag.

- **Legacy per-class version**: Many classes also have an internal `uiDllSaveVersion` (written via `MOD_SERIALIZE_INIT_WRITE`, currently `MOD_DLL_VERSION_NUMBER = 148` in `CustomMods.h`). This is the older Whoward-era system. Both systems coexist; use the global `SaveVersionTags` for new changes.

- **Read/write location**: `CvGame::Read()` reads the save version as the very first thing (`kStream >> saveVersion; GC.setSaveVersion(saveVersion);`). `CvGame::Write()` sets it to `SAVE_VERSION_LATEST` before writing.

- **Stream is forward-only**: `FDataStream::GetPosition()` and `SetPosition()` are **no-ops** in the Civ5 engine (the implementation is in a pre-compiled Firaxis binary `FireWorksWin32.lib`). You **cannot** seek, rewind, or backpatch. All version gating must be done at the point of reading/writing.

### How to add a new serialized field

1. **Add a new tag** to `SaveVersionTags` in `CvGlobals.h`:
   ```cpp
   SAVE_VERSION_MY_NEW_FIELD = 4,
   SAVE_VERSION_LATEST = SAVE_VERSION_MY_NEW_FIELD,
   ```

2. **Gate the read/write** in the class's `Serialize()` (or `operator>>` / `operator<<`):

   **Visitor-based pattern** (modern, used in CvPlot etc.):
   ```cpp
   if (GC.getSaveVersion() >= CvGlobals::SAVE_VERSION_MY_NEW_FIELD)
       visitor(obj.m_iNewField);
   else if (bLoading)
       mutObj.m_iNewField = 0;  // explicit default for old saves
   ```

   **Operator-based pattern** (legacy, used in CvEspionageClasses etc.):
   ```cpp
   // Read side:
   if (GC.getSaveVersion() >= CvGlobals::SAVE_VERSION_MY_NEW_FIELD)
       loadFrom >> writeTo.m_iNewField;
   else
       writeTo.m_iNewField = 0;  // explicit default

   // Write side (version is always LATEST, so always writes):
   if (GC.getSaveVersion() >= CvGlobals::SAVE_VERSION_MY_NEW_FIELD)
       saveTo << readFrom.m_iNewField;
   ```

3. **Always provide explicit defaults** in the `else if (bLoading)` / `else` branch — never leave the field uninitialized.

### How to remove/skip a serialized field

When a field is removed from the code but old saves still contain it, you must **consume the bytes** on load to keep the stream in sync:

```cpp
// Old saves wrote m_iLegacyField; consume it but discard
if (GC.getSaveVersion() < CvGlobals::SAVE_VERSION_FIELD_REMOVAL)
{
    int iLegacy = 0;
    loadFrom >> iLegacy;  // consume bytes, discard value
}
```

For vectors/arrays:
```cpp
if (GC.getSaveVersion() < CvGlobals::SAVE_VERSION_FIELD_REMOVAL)
{
    std::vector<int> vLegacy;
    loadFrom >> vLegacy;  // consume entire vector from stream
}
```

### Key rules and pitfalls

- **Never add a field to serialization without a version gate.** Even one extra byte will misalign every subsequent read for the rest of the save, causing cascading corruption and crashes.
- **Order matters**: fields must be read in the exact same order they were written. If you insert a new field between existing fields, gate it so old saves skip it.
- **On write, version is always `SAVE_VERSION_LATEST`**, so `>= SAVE_VERSION_FOO` always evaluates true during save. The gate only matters during load.
- **`MOD_SERIALIZE_READ` macro** (legacy system): `MOD_SERIALIZE_READ(version, stream, member, default)` expands to version-gated read with a default. Use this when working with classes that already use the `MOD_SERIALIZE_*` pattern.
- **Sentinel validation**: `MOD_SERIALIZE_INIT_WRITE` writes `0xDEADBEEF` after the class version; `MOD_SERIALIZE_INIT_READ` validates it. A mismatch means the stream is desynchronized — check your field ordering.
- **When bumping internal class versions** (e.g., `uiVersion` in `CvEspionageSpy`), bump the number in the write path and gate new fields with `if (uiVersion >= N)` in the read path.
- **Test with an old save**: always test loading a save created before your change to verify compatibility.

### Serialization patterns reference

| Pattern | Where used | Example |
|---|---|---|
| `visitor(field)` template | `CvPlot::Serialize`, `CvCity::Serialize`, etc. | `visitor(plot.m_iArea);` |
| `operator>> / operator<<` | `CvEspionageSpy`, `CvAttackTarget`, etc. | `loadFrom >> writeTo.m_iField;` |
| `MOD_SERIALIZE_READ(ver, stream, member, default)` | Classes using DLL version macros | `MOD_SERIALIZE_READ(23, loadFrom, writeTo.m_bPassive, false);` |
| `visitor.as<Type>()` | Type-casting during serialize | `visitor.template as<BuildTypes>(plot.m_eBuildProgress[i].first);` |

---

## AI/Tactical Bug Debugging Guidelines

### System architecture

- **CvTacticalAI** handles **combat/military moves**: target identification, zone control, tactical combat simulations. Runs for AI players only.
- **CvHomelandAI** handles **non-combat/peacetime moves**: exploration, worker improvements, healing, upgrading, sentry duty, etc. Runs for both AI and human players (for automated units).
- **Per-turn flow** (in `CvPlayerAI::AI_unitUpdate()`):
  1. AI players: `GetTacticalAI()->Update()` → `GetHomelandAI()->Update()` → `GetTacticalAI()->CleanUp()`
  2. Human players: only `GetHomelandAI()->Update()` (for automated units)
- **CvTacticalAI::Update()** flow: `UpdateVisibility()` → `DropOldFocusAreas()` → `FindTacticalTargets()` → `RecruitUnits()` → `ProcessDominanceZones()`
- **CvHomelandAI::Update()** flow: `RecruitUnits()` → `PlanImprovements()` / `PlanWorkerDistribution()` → `FindHomelandTargets()` → `AssignHomelandMoves()`

### Danger plots system

- **`CvDangerPlots`** (defined in `CvDangerPlots.h`) computes per-tile danger values for a player. Owned by `CvPlayer` as `m_pDangerPlots`.
- **No direct accessor** — `CvPlayer` exposes danger via three `GetPlotDanger()` overloads which lazy-update the danger data if dirty.
- **`CvUnit::GetDanger(pPlot)`** delegates to `GET_PLAYER(getOwner()).GetPlotDanger(...)`.
- **Update flow**: `CvPlayer::UpdateDangerPlots()` → `CvDangerPlots::UpdateDanger()` → `UpdateDangerInternal()`. Optionally uses `MOD_COMBATAI_TWO_PASS_DANGER` (units likely to be killed are excluded from ZOC in a second pass).

### Debugging steps

1. **Logging is essential**: The first step for AI bugs should be adding logging to identify WHICH code path is executing. Key log files:
   - `PlayerTacticalAILog.csv` — tactical AI decisions (use `LogTacticalMessage()`)
   - `PlayerHomelandAILog.csv` — homeland AI decisions (use `LogHomelandMessage()`)
   - Enable logging in `config.ini` per `DEVELOPMENT.md`

2. **Verify the DLL is actually being used**: After building, check:
   - The DLL timestamp in the mod folder matches your build
   - The file size is reasonable (release ~13MB, debug ~25MB)
   - Civ5 must be closed before copying the DLL

3. **Domain mismatch bugs are common**: When land units consider water plots (or vice versa), danger calculations often fail because:
   - `CvUnit::GetDanger(pPlot)` evaluates danger from the unit's CURRENT domain perspective
   - A trebuchet on land won't see naval threats because naval units can't attack land units on land
   - The danger system doesn't simulate "what if this unit changed domain"
   - Always check for `bWouldEmbark`, `needsEmbarkation()` (a `CvPlot` method), or domain transitions

4. **Zero-danger classification pitfalls**:
   - `bIsZeroDanger = (iDanger <= 0)` can be wrong for domain transitions
   - Water plots may appear zero-danger to land units because naval units can't attack them YET
   - Always add explicit checks for domain transitions with nearby threats

### Key functions reference

**Retreat/safety:**
| Function | Location | Purpose |
|---|---|---|
| `TacticalAIHelpers::FindSafestPlotInReach()` | `CvTacticalAI.cpp` | Evaluates safe retreat plots (static helper, not a member) |
| `CvTacticalAI::ExecuteMovesToSafestPlot()` | `CvTacticalAI.cpp` | Executes retreat moves |
| `CvTacticalAI::ExecuteWithdrawMoves()` | `CvTacticalAI.cpp` | Withdrawal from zones |
| `CvTacticalAI::MoveToEmptySpaceNearTarget()` | `CvTacticalAI.cpp` | Moving toward targets |

**Combat simulation & scoring:**
| Function | Location | Purpose |
|---|---|---|
| `TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit()` | `CvTacticalAI.cpp` | Simulate unit-vs-unit damage |
| `TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity()` | `CvTacticalAI.cpp` | Simulate unit-vs-city damage |
| `TacticalAIHelpers::IsAttackNetPositive()` | `CvTacticalAI.cpp` | Whether attacking is worth it |
| `TacticalAIHelpers::FindBestUnitAssignments()` | `CvTacticalAI.cpp` | Core combinatorial assignment solver |
| `TacticalAIHelpers::EstimateLocalUnitPower()` | `CvTacticalAI.cpp` | Local force comparison |

**Plot queries:**
| Function | Location | Purpose |
|---|---|---|
| `TacticalAIHelpers::GetAllPlotsInReachThisTurn()` | `CvTacticalAI.cpp` | All plots a unit can reach this turn |
| `TacticalAIHelpers::GetPlotsUnderRangedAttackFrom()` | `CvTacticalAI.cpp` | Plots attackable from a position |
| `TacticalAIHelpers::GetTargetsInRange()` | `CvTacticalAI.cpp` | Enemies reachable for attack |
| `CvPlot::GetNumEnemyUnitsAdjacent()` | `CvPlot.cpp` | Adjacent enemy count (also on `CvUnit` as convenience wrapper) |

**Constants** (defined in `CvDefines.h`):
- `RING0_PLOTS (1)`, `RING1_PLOTS (7)`, `RING2_PLOTS (19)`, `RING3_PLOTS (37)`, `RING4_PLOTS (61)`, `RING5_PLOTS (91)`
- Movement cost: `GD_INT_GET(MOVE_DENOMINATOR)`

### FindSafestPlotInReach() specifics

- **Static helper** in `TacticalAIHelpers::`, NOT a member of `CvTacticalAI` — `m_pPlayer` is unavailable; use `GET_PLAYER(pUnit->getOwner())` instead
- **Scoring**: lower score = better plot. Lists are sorted **descending** via `OptionWithScore::operator<` (which uses `score > rhs.score`), so `.back()` picks the **lowest/best** score
- **Plot categories by priority**: `aCityList` > `aCoverList` > `aZeroDangerList` > `aDangerList` > `aEmbarkList`
- `aEmbarkList` is a separate last-resort bucket used only when no land plots are available

### Tactical targets

- **`AITacticalTargetType`** enum in `CvEnums.h` (`CvGameCoreDLLUtil/include/CvEnums.h`): `AI_TACTICAL_TARGET_NONE`, `_ENEMY_CITY`, `_BARBARIAN_CAMP`, `_IMPROVEMENT`, `_BLOCKADE_POINT`, `_ENEMY_COMBAT_UNIT`, `_FRIENDLY_CITY`, `_IMPROVEMENT_TO_DEFEND`, `_DEFENSIVE_BASTION`, `_HIGH_PRIORITY_CIVILIAN`, `_LOW_PRIORITY_CIVILIAN`, `_TRADE_UNIT_SEA`, `_TRADE_UNIT_LAND`, `_ENEMY_CITADEL`, `_IMPROVEMENT_RESOURCE`, `_GOODY`
- **`CvTacticalTarget`** class in `CvTacticalAI.h` wraps target type, position, and scoring

### AI "memory" system for vanished units

- `CvDangerPlots::m_vanishedUnits` stores `(PlayerID, UnitID)` pairs of units seen last turn but no longer visible
- Cleared every turn — AI remembers for exactly ONE turn (like a human would)
- Access via `CvPlayer::GetVanishedUnits()` or `CvPlayer::IsVanishedUnit(const IDInfo& id)`
- When queried, the **live unit** is looked up — so current stats (including promotions) are used
- Use this to detect threats that moved into fog-of-war

### Checking for enemy units

- `CvPlot::GetNumEnemyUnitsAdjacent()` only checks adjacent plots. `CvUnit` has a convenience wrapper.
- For ranged threats, scan `RING2_PLOTS` or `RING3_PLOTS` manually
- Use `CvPlot::getUnitByIndex()` loop to find ALL units, not `getBestDefender()` which may miss some
- Always specify the correct domain filter (or `NO_DOMAIN` for all)

### Naval threat scanning for embarkation

- Use `RING5_PLOTS` (91 plots, 5 tiles) as maximum scan area
- Calculate per-unit threat range: `maxMoves() / GD_INT_GET(MOVE_DENOMINATOR)` + `GetRange()` if ranged
- Era-based fallback ranges: Ancient/Classical=5, Medieval/Renaissance=7, Industrial+=8
- Check both visible units AND vanished units for comprehensive threat detection

### UnitSet typedef

- Defined in `CvDangerPlots.h` as `typedef std::set<std::pair<PlayerTypes,int>> UnitSet`
- Duplicated in `CvPlayer.h` (with `#include <set>`) for self-containment

