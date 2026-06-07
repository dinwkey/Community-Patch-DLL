# Skill: Build and Log Triage (VC9-compatible)

## When to use

Use this skill whenever C++ files were changed and you need compile verification, artifact checks, or failure triage.

## Inputs to collect first

- What changed (`.cpp` only vs any `.h`/PCH)
- Desired config (default `debug` unless explicitly asked for `release`)
- Toolchain environment (`VS90COMNTOOLS` present)

## Required workflow

1. **Choose build mode correctly**
   - Use incremental `build_vp_clang.ps1` only for `.cpp`-only edits.
   - Use full rebuild (`python build_vp_clang.py --config debug`) when headers/PCH/toolchain/config changed or prior build was interrupted.

2. **Run build in foreground**
   - Do not detach/background long build commands.
   - Preferred default:
     - `python build_vp_clang.py --config debug` (full)
     - or `./build_vp_clang.ps1 -Config debug` (incremental)

3. **Check artifacts and logs**
   - Verify `clang-output/<config>/CvGameCore_Expansion2.dll` exists.
   - Inspect tail of `clang-output/<config>/build.log`.
   - Search for `error`, `FAILED`, `Traceback` on failure.

4. **MSBuild fallback (when needed)**
   - Use for linker/resource/toolset-compat failures clang cannot represent.
   - Initialize VC2008 env and build solution with .NET 4 MSBuild.

5. **Report outcome clearly**
   - State config used, build type (incremental/full), pass/fail, artifact path, and first actionable error (if any).

## Output checklist

- [ ] Correct build mode selected for change type
- [ ] Build executed to completion in foreground
- [ ] DLL artifact existence confirmed
- [ ] Log reviewed and summarized
- [ ] Fallback path attempted when appropriate
