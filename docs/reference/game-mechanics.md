I'll list organized categories of Civ5 game mechanics you can review later.

**Game Mechanics Categories**
- **Diplomacy:** trade deals, declarations of war/peace, denouncements, agreements, alliances, AI attitudes.  
- **Trade & Economy:** gold, trade routes, merchants, maintenance, city connections, market mechanics.  
- **City Management:** production, build queues, specialist assignment, city guard/garrison, automation.  
- **Population & Growth:** food, housing, growth rate, citizen assignment, yields per citizen.  
- **Buildings & Wonders:** building effects, wonder construction rules, maintenance, prerequisites.  
- **Production & Construction:** production points, project/unit/building costs, rush-buying, queue logic.  
- **Unit Combat:** combat resolution, damage, promotions, flanking, combat odds, city bombardment.  
- **Unit Movement & Pathfinding:** movement points, ZOC, embark/disembark, A* pathfinder, movement flags.  
- **Promotions & Experience:** unit XP, promotion trees, veteran bonuses, unit upgrades.  
- **Terrain & Tile Effects:** terrain/feature movement costs, defensive modifiers, resources on tiles.  
- **Resources:** strategic/luxury/bonus resources, yields, consumption, trade, resource reveal rules.  
- **Improvements & Workers:** tile improvements, build actions, build times, builder AI.  
- **Routes & Infrastructure:** roads/railroads/routes, maintenance, route-based yields and movement.  
- **Naval & Air Mechanics:** naval combat, carriers, naval movement, air units, airlift/rebase/airfields.  
- **Culture & Borders:** culture accumulation, border expansion, culture pressure, flip mechanics.  
- **Tourism & Great Works:** tourism, Great Works, theming, influence and culture victory mechanics.  
- **Religion & Faith:** founding/spreading religion, belief effects, inquisitors, missionary logic.  
- **Espionage:** spy actions, missions, counterespionage, tech stealing.  
- **City-States / Minor Civs:** influence mechanics, quests, suzerainty, minor AI behavior.  
- **Policies / Ideologies:** social policies, ideology effects, tenets, policy-tree choices.  
- **Technology / Science:** tech costs, research progress, beakers, tech prerequisites and effects.  
- **Great People & Unique Units:** generation, abilities, recruitment, unique unit/building handling.  
- **Happiness / Amenities:** empire happiness mechanics, penalties, luxury distribution.  
- **Health / Food Penalties:** corruption (mods), health mechanics that affect growth/productivity.  
- **Barbarians & Random Events:** barbarian spawning/behavior, random map/game events.  
- **Victory Conditions & Scoring:** domination/science/culture/diplomacy/score victory triggers.  
- **AI Systems & Difficulty:** AI flavors, behavior modules, handicap/cheat mechanics, strategic weighting.  
- **Tactical Systems & Combat AI:** tactical decision-making, target selection, retreat/engage heuristics.  
- **World & Map Rules:** map generation, landmass/island handling, climate, start biases.  
- **UI, WorldBuilder & Mod Hooks:** setup screens, LUA/C++ scripting hooks, modinfo/.mod files.  
- **Persistence & Save/Load:** game state serialization and DB/SQL-driven data.  
- **Performance, Caching & Tools:** pathfinder caches, turn-time optimizations, profiling hooks.

Further considerations (short):
- Group reviews by priority: (1) AI/Pathfinding/Combat, (2) City/Production/Trade, (3) Diplomacy/Religion/Policies, (4) UI/Mod hooks.  
- Tell me which category to deep-dive first and I’ll produce file/symbol pointers and concrete checklist for review.

## Completed Reviews
- [Unit Combat Review](docs/reference/unit-combat-review.md) — combat resolution, damage math, flanking, promotion triggers.
- [Promotions & Experience Review](docs/reference/promotions-experience-review.md) — XP system, promotion trees, veteran bonuses, unit upgrades.
- [Terrain & Tile Effects Review](docs/reference/terrain-tile-effects-review.md) — movement costs, defensive modifiers, resources on tiles, 8 issues tracked.