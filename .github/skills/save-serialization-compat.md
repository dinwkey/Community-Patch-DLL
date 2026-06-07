# Skill: Save Serialization Compatibility

## When to use

Use this skill when changing any serialized C++ state that can affect save/load compatibility:
- Adding fields to `Serialize()`, `operator>>`, or `operator<<`
- Removing previously serialized fields
- Reordering serialized fields
- Touching save-version gates or `MOD_SERIALIZE_*` usage

## Inputs to collect first

- Target class/files and exact serialized members being changed
- Whether serialization pattern is visitor-based (`Serialize` + visitor) or operator-based (`>>`/`<<`)
- Current `CvGlobals::SaveVersionTags` state in `CvGlobals.h`
- Whether class also uses legacy per-class version (`uiVersion` / `uiDllSaveVersion`)

## Required workflow

1. **Version strategy**
   - For new global changes, add a new `SAVE_VERSION_*` tag in `CvGlobals::SaveVersionTags`.
   - Move `SAVE_VERSION_LATEST` to the new tag.

2. **Read/write gating**
   - Visitor pattern:
     - `if (GC.getSaveVersion() >= CvGlobals::SAVE_VERSION_X) visitor(field);`
     - `else if (bLoading) default-initialize field`
   - Operator pattern:
     - Read: gate with `if (...) loadFrom >> field; else field = default;`
     - Write: write field under the same gate (write path runs at latest version)

3. **Removing old fields safely**
   - On load, consume legacy bytes for old saves with `< SAVE_VERSION_REMOVE_X` branch.
   - Do not keep removed bytes unread; stream alignment must stay exact.

4. **Legacy class-version handling**
   - If class has `uiVersion`, bump writer version and gate reader by `uiVersion >= N`.
   - Keep compatibility with both global save version and local class version where relevant.

5. **Forward-only stream rule (critical)**
   - Treat stream as forward-only.
   - Do not design probe-and-rewind logic with `GetPosition()`/`SetPosition()`.

6. **Validation**
   - Build debug after edits.
   - Verify load of at least one old save and one new save path.
   - If sentinel mismatch (`0xDEADBEEF`) appears, re-check field order and gating.

## Output checklist

- [ ] New save tag added (if needed) and `SAVE_VERSION_LATEST` updated
- [ ] New/changed fields are version-gated on read
- [ ] Defaults are explicit for missing fields
- [ ] Removed fields are consumed for older saves
- [ ] Build succeeds
- [ ] Old save load sanity-tested
