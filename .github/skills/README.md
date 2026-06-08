# Assistant Skills

These skill playbooks are task-focused runbooks intended for code-generating assistants working in this repository.
Use them when a request matches the scenario; keep `.github/copilot-instructions.md` as global guardrails.

## Available skills

1. `save-serialization-compat.md`
   - Use when adding/removing/changing serialized fields in C++.

2. `tactical-ai-debugging.md`
   - Use when debugging AI movement/combat behavior, danger plots, retreat logic.

3. `build-and-log-triage.md`
   - Use when building, validating artifacts, and triaging compile/link errors.

## Usage pattern

- Start from repository constraints in `.github/copilot-instructions.md`.
- Pick the closest skill and follow its checklist end-to-end.
- If multiple apply, run in this order:
  1) `build-and-log-triage.md` (establish build baseline)
  2) task-specific skill (`save-serialization-compat.md` or `tactical-ai-debugging.md`)
  3) `build-and-log-triage.md` (final verification)
