# Copilot instructions — CvGameCoreDLL_Expansion2

Purpose: Provide concise, actionable guidance for Copilot edits to the CvGameCoreDLL_Expansion2 sources.

Key rules:
- Do not modify the `CVPREGAME` Lua export. The `CVPREGAME` export is provided by the engine/upstream Lua bindings and must remain unchanged to preserve compatibility with existing Lua code and external mods.
- Prefer using the clang-based workflow (`build_vp_clang.ps1` / `clang-cl`) for iterative development and faster local builds; clang-cl can be configured to target the MSVC2008/VC9 ABI while compiling as a C++03/TR1-compatible toolchain. **Important:** Visual Studio 2008 (MSVC 9.0 / VC9) remains the required toolset for final linking and official release builds—always verify changes are compatible with MSVC2008/VC9 and the C++03/TR1 constraints.
- **C++ Standard**: Code must be compatible with C++03 (ISO/IEC 14882:2003). MSVC 2008 was released in 2008, before C++11 (2011), and does NOT support C++11 features. Optional: C++ Technical Report 1 (TR1) extensions may be used if available in MSVC 2008, but assume C++03 baseline for maximum compatibility.
- **When referencing UI/Lua code**: Use files from `(3a) EUI Compatibility Files\` (Enhanced User Interface) instead of `(2) Vox Populi\LUA\`. The game is played with EUI enabled, so EUI-compatible Lua files are the authoritative versions for UI behavior and examples. If a file exists in both locations, prefer the EUI version.

MSVC C++ Standard Limitation: MSVC 2008 (released 2008) supports only C++03, NOT C++11 or later. C++11 was standardized in 2011, three years after MSVC 2008 release. Do NOT use any C++11 features.
- **Optional TR1 Support**: C++ Technical Report 1 (TR1) features may be partially available in MSVC 2008 (e.g., `<memory>` for `shared_ptr`, `<regex>`, tuples), but assume C++03 baseline for compatibility. Do not rely on TR1 unless already used elsewhere in the codebase.
- **No C++11 features** (ABSOLUTELY NO exceptions): 
  - ❌ No range-based for loops: `for(const CvUnit* pUnit : vUnits)` 
  - ❌ No auto type deduction: `auto pUnit = GetUnit();`
  - ❌ No nullptr: use `NULL` or `0` instead
  - ❌ No lambda expressions: `[](a,b){return a<b;}`
  - ❌ No std::move or rvalue references
  - ❌ No constexpr or compile-time constants beyond enum
  - ❌ No variadic templates or template aliases
  - ❌ No override/final keywords
  - ❌ No std::initializer_list or brace initialization
  - ❌ No deleted/defaulted functions (= delete, = default)
- **Use C++03 equivalents**:
  - ✅ Traditional indexed loops: `for(size_t i = 0; i < vUnits.size(); i++)`
  - ✅ Explicit types: `CvUnit* pUnit = vUnits[i];`
  - ✅ Pointer arithmetic instead of auto
  - ✅ Manual loop finding best score instead of lambdas
- **Example - WRONG (C++11)**:
  ```cpp
  for(const CvUnit* pUnit : vUnits) { ... }  // Range-based for
  auto result = Calculate(x);                 // auto
  std::sort(v.begin(), v.end(), [](a,b){return a.score > b.score;});  // Lambda
  ```
- **Example - CORRECT (C++03)**:
  ```cpp
  for(size_t i = 0; i < vUnits.size(); i++) { const CvUnit* pUnit = vUnits[i]; ... }
  int result = Calculate(x);  // Explicit type
  // Manual sorting or functor class for comparison
  ```
- **No COMPILE_ASSERT macro**: MSVC 2008 does not support compile-time assertions. Use comments with manual verification instead.
- **Example - YES (sorting)**: Manual loop finding best score, or use older-style comparison operators
- **Health access**: Use `GetCurrHitPoints()` / `GetMaxHitPoints()` (and city `getDamage()`/`GetMaxHitPoints()`) instead of non-existent `getHealth()` / `maxHitPoints()` helpers.

When in doubt:
- If you think a change to `CVPREGAME` is required, discuss it with engine maintainers and document the reason in an issue first.
- Prefer adding new exports or wrapper functions rather than changing or removing existing exports.

Location notes:
- Lua-facing exports and bindings are closely tied to the engine; review `Lua`-facing headers and any existing `Lua` registration code before editing.

Style:
- Keep comments minimal and factual.
- When adding public APIs, include a short justification and compatibility notes.

Testing:
- Changes that affect Lua bindings must be validated in-game (run the game with the mod enabled) or via the project's Lua test harness if available.

API Naming Conventions (CRITICAL):
- **Times100 suffix pattern**: Many yield and production functions use `*Times100()` naming (e.g., `getYieldRateTimes100()`, `getProductionTimes100()`) to preserve precision. These return values multiplied by 100; divide by 100 for actual values.
- **No getYieldRate()**: There is no `getYieldRate()` function in CvCity. Always use `getYieldRateTimes100(YieldTypes eYield) / 100` to get yield per turn.
- **Production functions**: For production per turn with modifiers, use `getYieldRateTimes100(YIELD_PRODUCTION) / 100`. For raw production (no modifiers), use `getRawProductionPerTurnTimes100() / 100`.
- **Building-specific production**: Use `getProductionPerTurnForBuildingTimes100(BuildingTypes eBuilding) / 100` to get production accounting for building-specific modifiers.
- **Common yield calculation pattern**: `getYieldRateTimes100(YieldTypes eYield, bIgnoreTrade, bIgnoreProcess, iAssumeExtraModifier, bAssumeFoodProduction)` — all boolean/int parameters have specific meanings; check CvCity.h for details.
- **Search before guessing**: When uncertain about a function name, use grep_search or semantic_search on CvCity.h/CvCity.cpp to find the correct API rather than assuming a simplified name exists.

Accessing Game Data (CRITICAL):
- **GlobalDefines access**: Use `GD_INT_GET(DEFINE_NAME)` for integer defines, `GD_FLOAT_GET(DEFINE_NAME)` for floats. Example: `GD_INT_GET(NUKE_FEATURE)` not `GC.getNUKE_FEATURE()`.
- **Feature/Terrain/Resource data**: Access via info classes, NOT direct GC methods:
  - **Feature damage**: `GC.getFeatureInfo(eFeature)->getTurnDamage()` NOT `GC.getFEATURE_FALLOUT_DAMAGE()`
  - **Terrain damage**: `GC.getTerrainInfo(eTerrain)->getTurnDamage()`
  - **Resource yield**: `GC.getResourceInfo(eResource)->getYieldChange(eYield)`
  - **Improvement properties**: `GC.getImprovementInfo(eImprovement)->GetPropertyName()`
  - **Building properties**: `GC.getBuildingInfo(eBuilding)->GetPropertyName()`
  - **Unit properties**: `GC.getUnitInfo(eUnit)->GetPropertyName()`
- **Info class pattern**: Always use `GC.get[Type]Info([Type]Types e[Type])` to get the info class, then call methods on it. Never assume direct `GC.get[CONSTANT]()` methods exist.
- **Null checks**: Always check if info pointer is valid: `CvFeatureInfo* pkInfo = GC.getFeatureInfo(eFeature); if (pkInfo) { pkInfo->getTurnDamage(); }`
- **Plot-based data**: For plot properties, use plot methods directly: `pPlot->getTurnDamage(bIgnoreTerrain, bIgnoreFeature, bExtraTerrain, bExtraFeature)` calculates total damage from terrain + features.
- **Common mistake**: Writing `GC.getFEATURE_X()` or `GC.getUNIT_Y()` - these methods don't exist. Always go through info classes.
- **Search patterns**: Before accessing game data, search existing code for similar patterns using grep_search with terms like "FeatureInfo", "getTurnDamage", "getYieldChange" to find correct usage.

Macro Usage Patterns (CRITICAL):
- **GET_TEAM macro**: Always pass a `TeamTypes` parameter, NOT `PlayerTypes`. Common mistake:
  - ❌ `GET_TEAM(eLoopPlayer)` where `eLoopPlayer` is `PlayerTypes` - type mismatch error
  - ✅ `GET_TEAM(kLoopPlayer.getTeam())` - correctly pass TeamTypes
  - ✅ `GET_TEAM(eTeam)` where `eTeam` is already `TeamTypes`
  - Note: Use `.isAtWar(TeamTypes)` to check if at war, NOT `.isFriendlyTerritory()`
- **GET_PLAYER macro**: Pass `PlayerTypes` only
  - ✅ `GET_PLAYER(eLoopPlayer).getNumUnits()`
  - ✅ `GET_PLAYER(eLoopPlayer).getTeam()` - returns TeamTypes
- **Team vs Player distinction**: Keep this strict - team encompasses multiple players in same alliance
  - To check if not at war: `GET_TEAM(eTeam).isAtWar(eLoopTeam)` returns bool
  - To check if alive: `kLoopPlayer.isAlive()` - use player reference, not team
- **Always verify brace matching**: When editing a function, ensure all opening braces `{` have matching closing braces `}`. Count braces before and after your edit.
- **Preserve control flow structure**: When adding code inside if/else blocks, verify the entire block structure remains intact. Do not accidentally move statements from outside a block to inside it.
- **Complete all code paths**: When editing functions with multiple return paths (if/else if/else), ensure every path has a return statement and the function has a final closing brace.
- **Include sufficient context**: When using replace_string_in_file, include at least 5-8 lines of context before and after to uniquely identify the location and verify scope.
- **Read back after editing**: After making changes to a function, use read_file to verify the complete function structure including opening/closing braces.
- **Multi-line edits**: For complex functions, read the entire function first (50-100 lines) to understand its structure before making changes.
- **Verify function names and variable scope**: When adding code that references variables, ALWAYS verify:
  - You are editing the correct function (similar function names may exist, e.g., `DoExtraPlotDamage` vs `DoAdjacentPlotDamage`)
  - All variables referenced exist in the current scope (check loop variables, function parameters, local declarations)
  - Read at least 20-30 lines of surrounding context before making changes to confirm function boundaries
  - If unsure which function you're in, search for the function signature (e.g., `void CvUnit::FunctionName`)
- **Avoid duplicate symbols when adding members**: Before adding new getters/setters or member variables, search the header and cpp for existing `getX`/`setX` and member declarations. Ensure only one declaration, one definition, and one SYNC_ARCHIVE entry per member to prevent redefinition build errors.
- **Patch placement**: Avoid inserting gameplay logic into serialization helpers (`Read/Write/Serialize`) at the top of files. Anchor patches inside the intended gameplay function (e.g., `ScoreAirBase`, `ExecuteAircraftMoves`) by using context lines and re-reading the function after the edit.
- **Static class member functions**: When adding static member functions to a class:
  - Header declaration: `static ReturnType FunctionName(params);` inside the class definition
  - Implementation: `ReturnType ClassName::FunctionName(params) { ... }` (NOT `static ReturnType ClassName::...`)
  - File-local static functions: Use `static ReturnType FunctionName(params)` (no class scope) only for non-member helpers
  - **Never** mix declarations: if the header declares a static class member, the cpp MUST use `ClassName::` syntax without `static` keyword
- **Common pitfalls to avoid**:
  - Missing closing brace `}` at end of function
  - Missing return statements in if/else branches
  - Accidentally nesting statements inside conditional blocks that should be outside
  - Incomplete switch/case blocks
  - Forgetting the final return statement after if/else if chains
  - Declaring static class members but implementing as file-local static functions (causes linker errors)
  - **Referencing variables that don't exist in the current scope** (e.g., using `pSplashPlot` in a function that only has `pWhere`)
  - **Editing the wrong function** when multiple similar functions exist in the same file
  - **Unused variables**: If a variable is declared but never used, remove it (causes compiler warnings). Common cases:
    - Variables calculated but never referenced in logic (e.g., `int iLandThreat = CalculateProximityWeightedThreat(DOMAIN_LAND);` if `iLandThreat` is never used)
    - Constants defined for future use but not yet implemented (e.g., `SIEGE_UNIT_MULTIPLIER = 200` if you only use `RANGED_UNIT_MULTIPLIER`)
    - Team/player references that seemed necessary but are unused (e.g., `TeamTypes eTeam = GET_PLAYER(eOwner).getTeam();` if `eTeam` not referenced)

Class Member Variable Declaration (CRITICAL):
- **Member variables MUST be declared in the class definition**: All member variables must appear in the class body (typically in the protected/private section), NOT just in SYNC_ARCHIVE section.
- **SYNC_ARCHIVE_VAR is metadata, not declaration**: The `SYNC_ARCHIVE_VAR()` macros at the end of the header are for serialization only. They reference variables that must already be declared as class members.
- **Correct declaration order**:
  1. Declare the member variable in the class definition (e.g., `std::vector<int> m_myVariable;` in the protected/private section)
  2. Optionally add `SYNC_ARCHIVE_VAR(std::vector<int>, m_myVariable)` at the bottom if it needs to be saved/loaded
- **Cache/transient variables**: Variables that don't need saving (like performance caches) should ONLY be declared in the class definition, never in SYNC_ARCHIVE section.
- **Example - WRONG**:
  ```cpp
  // At end of header in SYNC_ARCHIVE section:
  std::vector<int> m_paiNewCache;  // ERROR: This is not a declaration!
  ```
- **Example - CORRECT**:
  ```cpp
  // In class definition (around line 3500):
  std::vector<int> m_paiNewCache;  // Actual member variable declaration
  
  // Optionally, at end of header if it needs serialization:
  SYNC_ARCHIVE_VAR(std::vector<int>, m_paiNewCache)
  ```
- **Finding the right location**: In CvPlayer.h, member variables are typically declared around lines 3000-3600. Search for similar variable types to find the appropriate section.

Function Overload Selection (CRITICAL):
- **Know the overloads**: Many core functions have multiple overloads with different parameter types. Always verify you're using the correct one.
  - **Example - GetPlotDanger**: There are overloads for `CvCity*` and `CvPlot*` - they take different types!
    - ✅ `GetPlotDanger(CvCity*)` - Pass city pointer directly
    - ✅ `GetPlotDanger(CvPlot*)` - Pass plot pointer directly  
    - ❌ `GetPlotDanger(*pCity->plot())` - WRONG: Dereferences then expects pointer (type mismatch)
    - ❌ `GetPlotDanger(*pPlot)` - WRONG: Dereferences plot (compile error)
- **Before using a function**: Search for its declaration in .h files to see all overloads and their signatures
- **Compiler error hints**: Mismatched overloads typically show "cannot convert parameter X from 'Type1' to 'Type2'" - read this carefully and check which overload you need
- **Common pattern**: Functions often have overloads for city vs. plot - check context to determine which is appropriate
