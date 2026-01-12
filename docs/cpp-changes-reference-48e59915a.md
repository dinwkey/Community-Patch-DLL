# C++ Changes Reference: Commit 48e59915a

**Commit Hash**: `48e59915a`  
**Date**: January 10, 2026  
**Short Message**: Refactor AI systems (military, tactical, homeland) with memory optimization and documentation reorganization

## Summary

This commit completes the category reorganization of documentation and introduces significant C++ improvements to tactical AI, military AI, and game mechanics, with a focus on memory optimization and coordinated combat strategies.

## Major C++ Changes

### Military AI & Tactical Combat (CvTacticalAI.cpp, CvMilitaryAI.cpp)

- **Memory Optimization**: Added cleanup of global tactical simulation caches (gReachablePlotsLookup, gRangeAttackPlotsLookup, gSafePlotCount, gBadUnitsCount, gDistanceToTargetPlots) to prevent unbounded memory growth in long games
- **Force Capacity Release**: Every 10 turns, force hash map capacity release to prevent memory fragmentation on 32-bit systems (Issue 4.2)
- **Retreat Logic**: Implemented `ShouldRetreatDueToLosses()` to assess whether units should retreat based on cumulative damage (>20% army health loss)
- **Allied Coordination**: Added `FindNearbyAlliedUnits()` to locate allied units within tactical range, considering team alliances, defensive pacts, and city-state relations
- **Coordinated Attacks**: Implemented `FindCoordinatedAttackOpportunity()` to enable multi-unit simultaneous attacks when 2+ units can coordinate on a target within 6-tile range
- **Enhanced Zone Withdrawal**: Modified `ProcessDominanceZones()` to trigger early withdrawal when a dominance zone has sustained heavy losses without nearby support
- **Withdraw Move Safeguards**: Added null-check and `canMoveInto()` validation in `ExecuteWithdrawMoves()` to prevent pathfinding failures and unreachable target assignment
- **Air Unit Strike Logic** (Issue 4.2): Refined `FindUnitsWithinStrikingDistance()` to:
  - Detect if dedicated bombers are available
  - Keep fighter aircraft healthy for interceptions (target 65%+ HP)
  - Only allow fighters to strike when either no bombers are ready or intercept risk is low and health is high (90%+)
  - Prevents fighter depletion during enemy air superiority scenarios
- **Military AI Enhancements**: Added 726+ lines to CvMilitaryAI.cpp with improvements to unit threat assessment, army composition evaluation, and strategic targeting

### Pathfinding & Navigation (CvAStar.cpp, CvAStarNode.h)

- **Performance Improvements**: Refactored pathfinding logic with 150+ line changes for better A* heuristics and reduced calculation overhead
- **Node Optimization**: Updated CvAStarNode structure for improved memory layout

### City Management & Culture (CvCity.cpp, CvCultureClasses.cpp)

- **City Production Logic**: Refactored 118 lines in city production calculations
- **Culture System**: Added 152 lines to CvCultureClasses.cpp with new culture generation tracking and ideology influence calculations

### Trade & Economic Systems (CvTradeClasses.cpp, CvEconomicAI.cpp)

- **Trade Route Management**: Implemented 227+ line changes to trade route evaluation, including better gold yield prediction and diplomatic route bonuses
- **Economic AI**: Enhanced economic decision-making with 15+ line improvements for resource prioritization

### Religion & Tech Systems (CvReligionClasses.cpp, CvTechClasses.cpp)

- **Religion Management**: Added 247 lines for improved spread calculation and faith yield tracking
- **Technology Progression**: Enhanced 57 lines in tech research with better cost evaluation and path optimization

### AI & Game Setup Improvements

- **Homeland AI**: Refactored 241+ lines in CvHomelandAI.cpp for better unit placement and worker task optimization
- **Builder Tasks**: Improved 55 lines in CvBuilderTaskingAI.cpp for more intelligent improvement targeting
- **Player Initialization**: Enhanced CvPreGame.cpp with 80 additional lines for better initial civilization setup and balance

### Lua & UI Export Enhancements

- **Test Exports**: Created new TestExports.cpp/h with 73+ lines to enable better testing and verification of core DLL calculations from Lua
- **Lua Player API**: Extended CvLuaPlayer.cpp with 11 additional export functions

## Documentation & Configuration

- **Copilot Instructions**: Added comprehensive `.github/copilot-instructions.md` (157 lines) with project-specific development guidance
- **Updated Copilot Instructions**: Enhanced root copilot-instructions.md with 50 additional lines covering UI placement guidelines and minor popup positioning
- **Documentation Links**: Updated internal documentation links across `docs/` folder to reflect new category organization (guides/, implementation/, military-ai/, reference/)
- **Solution Files**: Updated VoxPopuli_vs2013.sln project configurations to reflect new file locations
- **Project Configuration**: Modified CvGameCoreDLLUtil CustomModsGlobal.h for 43-Civ compatibility flag

## Supporting Changes

- **Notifications & UI Text**: Added 22 lines to core notification definitions and 11 lines to UI text strings
- **EUI Compatibility**: Enhanced CityView.lua with 37 lines for improved UI scaling
- **Top Panel**: Updated ImprovedTopPanel with 6 lines of enhancements
- **Test Infrastructure**: Included Google Test (gtest) libraries for unit testing framework support (1700+ files)

## Impact

- **Performance**: Reduced memory fragmentation and improved tactical pathfinding efficiency for long-running games
- **Strategy**: Enhanced AI coordination and retreat decisions create more realistic and challenging combat scenarios
- **Stability**: Added safety checks throughout withdrawal logic and air combat targeting to prevent edge case crashes
- **Maintainability**: Created comprehensive developer documentation and export framework for easier testing and future modifications
- **Compatibility**: Ensured Visual C++ 2008 (VC9) compliance for all C++ changes

## Files Modified

- **Total**: 646 files changed with 235,050 insertions and 435 deletions
- **Primary focus**: 
  - CvMilitaryAI.cpp (+726 lines)
  - CvTacticalAI.cpp (+295 lines)
  - CvTradeClasses.cpp (+227 lines)
  - CvReligionClasses.cpp (+247 lines)
  - CvCultureClasses.cpp (+152 lines)
  - CvHomelandAI.cpp (+241 lines)
  - CvCity.cpp (+118 lines)
  - CvAStar.cpp (+150 lines)
  - CvPreGame.cpp (+80 lines)
  - CvBuilderTaskingAI.cpp (+55 lines)
  - CvTechClasses.cpp (+57 lines)
  - TestExports.cpp/h (+73 lines)
  - CvLuaPlayer.cpp (+11 lines)

## Key Function References

### CvTacticalAI.cpp
- `ShouldRetreatDueToLosses()` - Determines if units should retreat
- `FindNearbyAlliedUnits()` - Locates allied units for coordination
- `FindCoordinatedAttackOpportunity()` - Enables multi-unit attacks
- `ProcessDominanceZones()` - Manages zone-based combat strategy
- `ExecuteWithdrawMoves()` - Executes safe withdrawal tactics

### CvMilitaryAI.cpp
- Unit threat assessment improvements
- Army composition evaluation enhancements
- Strategic targeting optimizations

### CvAStar.cpp & CvAStarNode.h
- A* heuristics improvements
- Pathfinding performance optimization

### Economy & Society (CvTradeClasses.cpp, CvReligionClasses.cpp, etc.)
- Trade route evaluation enhancements
- Religion spread calculation improvements
- Tech research path optimization

---

**Note**: This reference document is generated from commit `48e59915a`. For implementation details, review the actual commit diff or examine the modified source files directly.
