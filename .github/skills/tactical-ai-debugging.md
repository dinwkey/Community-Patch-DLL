# Skill: Tactical AI Debugging

## When to use

Use this skill for AI movement/combat bugs in tactical and danger evaluation systems:
- Bad retreat choices
- Wrong danger classification (especially embarkation/domain transitions)
- Unexpected unit assignment/targeting behavior

## Inputs to collect first

- Repro steps (turn, civ/player, unit types, map area)
- Whether issue occurs for AI players, human automation, or both
- Current DLL path and timestamp being loaded by Civ5
- Relevant logs enabled in `config.ini`

## Required workflow

1. **Identify owning subsystem**
   - Tactical combat decisions: `CvTacticalAI`
   - Non-combat/unit automation: `CvHomelandAI`
   - Per-turn sequencing reference: `CvPlayerAI::AI_unitUpdate()`

2. **Add targeted logging first**
   - Tactical: `LogTacticalMessage()` -> `PlayerTacticalAILog.csv`
   - Homeland: `LogHomelandMessage()` -> `PlayerHomelandAILog.csv`
   - Log branch decisions, candidate plots, danger values, chosen action

3. **Validate danger-model assumptions**
   - `CvUnit::GetDanger()` is evaluated from the unit/domain context.
   - For embarkation transitions, explicitly evaluate `bWouldEmbark`/`needsEmbarkation()` scenarios.
   - If needed, scan nearby threats manually (`RING2_PLOTS` to `RING5_PLOTS`) with domain-aware filters.

4. **Check high-impact helper paths**
   - `TacticalAIHelpers::FindSafestPlotInReach()`
   - `CvTacticalAI::ExecuteMovesToSafestPlot()`
   - `CvTacticalAI::ExecuteWithdrawMoves()`
   - `CvTacticalAI::MoveToEmptySpaceNearTarget()`
   - `TacticalAIHelpers::FindBestUnitAssignments()`

5. **Use correct plot/unit iteration**
   - Prefer full plot iteration with `CvPlot::getUnitByIndex()` when checking all threats.
   - Do not rely only on `getBestDefender()` for threat census.

6. **Validate with live run**
   - Rebuild debug DLL.
   - Ensure Civ5 is loading that DLL.
   - Reproduce and compare log deltas before/after change.

## Output checklist

- [ ] Subsystem owner identified (Tactical vs Homeland)
- [ ] Instrumentation added to decisive branches
- [ ] Domain/embarkation edge cases explicitly checked
- [ ] Threat scanning uses complete unit enumeration where needed
- [ ] Fix verified with logs and in-game repro
