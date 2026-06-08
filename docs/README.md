# Custom Branch Documentation

This directory contains both current guidance and historical analysis produced while the custom branch was split, squashed, and compared against upstream/master.

## Current branch state

- Maintained branch: `custom/ai-gameplay-enhancements`
- Archived predecessor: `feature/copilot` is preserved by the `archive/feature-copilot` tag
- Current intent: keep changes that improve AI play and game feel, not just changes that reduce rebase effort
- Save/load policy: fresh-game-only for the current custom stack unless old-save compatibility is explicitly required

As of the current branch, `CvGlobals::SaveVersionTags` only has `SAVE_VERSION_LATEST = 0`. Earlier branch-local save-version gates were temporary migration aids and were removed after deciding to start fresh games.

## How to read older reports

Many reports in this directory reference `feature/copilot`, `feature/copilot-backup`, old commit hashes, or validation counts such as `28/28 builds`. Treat those as historical evidence from earlier review sessions, not as live status for the current branch.

When a document says the old branch was "current" or "fully synchronized", read that relative to the report date shown in that document. For current validation, rely on the latest branch commits and rerun the relevant build or gameplay checks.

## Current high-signal guidance

- Use `.github/copilot-instructions.md` for repository-wide assistant/build constraints.
- Use `.github/skills/build-and-log-triage.md` when validating a code change.
- Use `.github/skills/save-serialization-compat.md` only when a change must preserve compatibility with older saves.
- Use `.github/skills/tactical-ai-debugging.md` when investigating AI movement, danger plots, retreat logic, or tactical combat behavior.

## Historical reports most likely to look stale

These documents intentionally preserve old branch names and dated conclusions:

- `docs/analysis/CHANGES_TO_REASSESS.md`
- `docs/analysis/BACKUP_BRANCH_DELTA_ANALYSIS.md`
- `docs/verification/UPSTREAM_COMMIT_VERIFICATION_REPORT.md`
- `docs/build-system/BUILD_SYSTEM_COMPARISON.md`
- `docs/COMMIT_48E59915A_COMPARISON_REPORT.md`
- `docs/implementation/COMPLETION_SUMMARY.md`
- `docs/implementation/SESSION_6_COMPLETION_REPORT.md`
- `docs/implementation/SELECTIVE_REIMPLEMENTATION_STRATEGY.md`
- `docs/build-system/GLOBALS_CLEANUP_ANALYSIS.md`
- `docs/city-management/CITIZEN_MANAGEMENT_ENHANCEMENTS_ANALYSIS.md`
- `docs/core-game-systems/GAME_SYSTEMS_AND_DATA_PHASE_ANALYSIS.md`
- `docs/diplomacy-ai/DIPLOMACY_ENHANCEMENTS_ANALYSIS.md`
- `docs/minidumps/MINIDUMP_APPROACHES_COMPARISON.md`
- `docs/pathfinding/PATHFINDING_PHASE_ANALYSIS.md`
- `docs/policy-ai/POLICY_SYSTEM_ENHANCEMENTS_ANALYSIS.md`
- `docs/tech-ai/TECH_SYSTEM_ENHANCEMENTS_ANALYSIS.md`
- `docs/COMMIT_48E59915A_COMPARISON_REPORT.md`
- `docs/cpp-changes-reference-48e59915a.md`

Do not use those reports as proof that the current branch still has the same commit count, save compatibility behavior, or upstream synchronization state.
