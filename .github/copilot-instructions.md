# Copilot / AI assistant instructions for Community-Patch-DLL

Purpose: give targeted, actionable guidance to code-generating assistants so they can be immediately productive in this repository.

File creation safety
- ALWAYS create or modify files only under the repository workspace paths shown in the workspace view (for example, the repo root and its subfolders). Do NOT create files using incorrect absolute prefixes such as `C:\c\Users\...` or any path outside the workspace.
- Use repository/workspace-relative paths when writing files and prefer the exact repository path shown by your workspace view (for example, `C:\Users\Thomson\source\repos\Community-Patch-DLL\...`).
- When updating files programmatically, use the repository's `apply_patch` mechanism or other workspace-aware APIs so changes land inside the workspace. After writing, verify the file appears in the workspace listing.
- If a path looks unexpectedly absolute or contains doubled roots like `C:\c\Users`, stop and ask for confirmation before writing.

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

- **Repo conventions & patterns to follow:**
  - Large, monolithic C++ codebase using precompiled headers and many translation units — prefer small, localized edits and rebuild only what you must.
  - Most gameplay-level changes live in the mod folders (Lua/SQL/XML); modify those when you can to avoid rebuilding DLL.
  - Follow existing naming conventions: core module prefix `Cv` (e.g. `CvPlayer.cpp`, `CvUnit.cpp`) and serialization helpers (`CvGameCoreEnumSerialization.cpp`). Use existing helper functions rather than duplicating logic.

- **Integration points & external deps:**
  - Lua (ThirdPartyLibs/Lua51) — used for game scripting.
  - FirePlace, CvGameDatabase, CvLocalization projects — they build as libs/objects and are linked into the DLL (see `build_vp_clang.py` `LIBS`).
  - FireTuner (SDK tool) can be used for autoplay when debugging AI.

 -- **Common pitfalls to avoid:**
  - Opening the solution and accepting automatic upgrade may break the historic VC9 configuration — choose **No Upgrade**.
  - Whole Program Optimization (WPO) causes long stalls in Release builds; it can be disabled per-project in VS for faster iteration.
  - If build/link errors mention missing VC9 headers, ensure VC++ 2008 SP1 toolset is installed.

- **Strict compilation rule for agents:**
  - Prefer using the clang-based workflow (`build_vp_clang.ps1` / `clang-cl`) for local development and CI checks because it gives faster iteration. **However, all code and binaries must be written and verified to target Visual C++ 2008 SP1 (VC9) ABI and language constraints (C++03 with only TR1 where already available).** Do not assume modern MSVC toolsets (2015/2017/2019/2022) are compatible for release artifacts.
  - After making any change to C++ sources, run the clang-based build script to verify compilation before committing or opening a PR: `.\\build_vp_clang.ps1 -Config debug` (use `-Config release` for release-targeted changes). Confirm `clang-output/<config>/CvGameCore_Expansion2.dll` and `.pdb` are created and check `clang-output/<config>/build.log` for errors; if the build fails, fix locally and re-run until it succeeds. Additionally, when possible, validate the produced artifacts against a VC9 linker or perform an MSVC2008 build to confirm ABI/linkage compatibility. Add a short note in the commit/PR indicating the clang build passed (e.g., `clang-build: debug successful`) and note any VC9 verification performed.
  - **CRITICAL: Run the clang build blocking and wait for completion.** When invoking .\build_vp_clang.ps1 -Config debug, run it interactively in the foreground and stream its output until it finishes; do not start it in the background or detach it. The compilation of hundreds of .cpp files is CPU-intensive and can take 10-30 minutes. Do not interrupt the build once started — allow it to complete before performing follow-up checks or edits.
  - Required environment note: `VS90COMNTOOLS` must point to a valid VS2008 installation for tooling and some scripts (e.g., `build_vp_clang.ps1`) to work.

  - **MSBuild fallback (when to use):** Prefer the clang-based workflow, but fall back to MSBuild/VS2008 when failures appear to be unrelated to C++ syntax (for example: linker issues, resource compiler errors, ToolsVersion/VS project compatibility, or VC9 ABI/linker behavior that clang can't reproduce). When using MSBuild as a fallback, set the VC2008 environment first and use the .NET 4 MSBuild executable to drive the solution. Example PowerShell commands:

    ```powershell
    & "$env:VS90COMNTOOLS\..\..\VC\vcvarsall.bat" x86
    & 'C:\Windows\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe' .\VoxPopuli_vs2013.sln /m /p:Configuration=Debug /p:Platform=Win32 /v:m
    ```

  - **What to check after MSBuild:** ensure `BuildOutput\\Debug\\CvGameCore_Expansion2.dll` (or the Release path) is produced, and review the MSBuild log for linker/resource errors. Treat MSBuild as a verification step for platform/VC9-specific issues — keep clang as the primary fast/CI build.

 **Build and log verification (for agents):**
   - **Run build:** start the clang script and run it *in the foreground* (blocking) so the process completes before taking any further actions. Example (PowerShell):
       - `\.\build_vp_clang.ps1 -Config debug`
     Run the command interactively and wait for it to finish; **do not** run background log-watchers that read the log while the build is active because they can interfere with or interrupt the build.
  - **Check logs after completion:** once the build finishes, inspect the last lines of the log to confirm success or capture errors:
    - `Get-Content clang-output/Debug/build.log | Select-Object -Last 200`
  - **Automated wait/poll:** if automation is required, use `tools/wait_for_clang_build.ps1` (it safely polls the log and exits when the build finishes) or write a poller that checks for the output DLL path `clang-output/Debug/CvGameCore_Expansion2.dll`. Run it with ExecutionPolicy bypass if needed:
  - `build_vp_clang.ps1` — exact compiler/linker flags, include paths, and build flow used by the clang-based build.
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




