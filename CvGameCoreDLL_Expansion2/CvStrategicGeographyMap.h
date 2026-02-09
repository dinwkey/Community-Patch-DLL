/*	-------------------------------------------------------------------------------------------------------
	Sid Meier's Civilization V — Vox Populi
	Strategic Geography Map: persistent terrain-aware layer for AI defense allocation.
	See docs/military-ai/STRATEGIC_GEOGRAPHY_MAP_PLAN.md for design rationale.
	------------------------------------------------------------------------------------------------------- */
#pragma once

#ifndef CIV5_STRATEGIC_GEOGRAPHY_MAP_H
#define CIV5_STRATEGIC_GEOGRAPHY_MAP_H

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Defensive Layer Classification
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

enum eStrategicLayer
{
	STRATEGIC_LAYER_UNKNOWN,
	STRATEGIC_LAYER_FRONT_LINE,   // Within 4 tiles of hostile/neutral border
	STRATEGIC_LAYER_SECOND_LINE,  // 5-8 tiles from hostile border
	STRATEGIC_LAYER_REAR_AREA,    // 9-12 tiles from hostile border
	STRATEGIC_LAYER_CORE,         // >12 tiles from hostile border or deep interior
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Naval Phase 1: Coastal Exposure Classification
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

enum eCoastalExposure
{
	COASTAL_EXPOSURE_NONE,       // Not a coastal city (or lake-only)
	COASTAL_EXPOSURE_SHELTERED,  // 1-2 water tiles in RING1: minimal naval threat vector
	COASTAL_EXPOSURE_MODERATE,   // 3-4 water tiles in RING1: standard coastal city
	COASTAL_EXPOSURE_EXPOSED,    // 5-6 water tiles in RING1: peninsula tip, island, wide-open coast
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Naval Phase 2: Naval Chokepoint Classification
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

enum eNavalChokeType
{
	NAVAL_CHOKE_NONE,              // City has no nearby naval chokepoint
	NAVAL_CHOKE_NEAR_STRAIT,       // City is near a narrow water passage (within RING3)
	NAVAL_CHOKE_CANAL_CITY,        // City itself is a canal connecting 2 water areas (controllable chokepoint)
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Geographic Posture Classification (Player-level)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

enum eGeographicPosture
{
	GEO_POSTURE_UNKNOWN,
	GEO_POSTURE_CONTINENTAL,  // Core on large contiguous landmass
	GEO_POSTURE_COASTAL,      // Large landmass core with heavy coastal exposure
	GEO_POSTURE_PENINSULAR,   // Large landmass core with a narrow land connection
	GEO_POSTURE_ISLAND,       // All/most cities on a small landmass
	GEO_POSTURE_ARCHIPELAGO,  // Cities dispersed across multiple small landmasses
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Convoy Transit Risk Assessment (Phase I-5)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

enum eTransitRisk
{
	TRANSIT_RISK_UNKNOWN,
	TRANSIT_RISK_LOW,      // Peacetime, no hostile naval nearby, short crossing
	TRANSIT_RISK_MEDIUM,   // Hostile civ exists but no visible naval near route
	TRANSIT_RISK_HIGH,     // At war, enemy naval within ~10 tiles, or barbarian threat
};

// Pending transit: unit waiting for escort or already en route
struct PendingTransit
{
	int iUnitID;
	int iOriginPlotIndex;    // Embarkation point
	int iDestPlotIndex;      // Destination
	eTransitRisk eRisk;
	int iTurnQueued;         // Turn unit requested escort
	bool bHighPriority;      // Settler/GP = true, combat unit = false

	PendingTransit() : iUnitID(-1), iOriginPlotIndex(-1), iDestPlotIndex(-1), 
	                   eRisk(TRANSIT_RISK_UNKNOWN), iTurnQueued(-1), bHighPriority(false) {}
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Naval Phase 3: Water Area Connectivity — graph edge between two water areas
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

struct WaterAreaEdge
{
	WaterAreaEdge()
		: iOtherAreaID(-1)
		, eController(NO_PLAYER)
		, bViaCanalCity(false)
		, bViaFort(false)
	{
	}

	int iOtherAreaID;           // The water area on the other side of the transit
	PlayerTypes eController;    // Who controls the transit point (NO_PLAYER = open/uncontrolled)
	bool bViaCanalCity;         // Transit through a canal city (owner-only for military)
	bool bViaFort;              // Transit through a passable fort (open borders accessible)
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Per-enemy approach data (Phase 5)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

struct EnemyApproach
{
	EnemyApproach()
		: eEnemy(NO_PLAYER)
		, iDistanceFromEnemy(99)
		, iApproachDifficulty(0)
		, eLikelyApproachDirection(NO_DIRECTION)
	{
	}

	PlayerTypes eEnemy;
	int iDistanceFromEnemy;           // plotDistance from nearest enemy city to our city
	int iApproachDifficulty;          // 0-100: terrain difficulty for enemy to reach us (higher = harder)
	DirectionTypes eLikelyApproachDirection; // Direction FROM which the enemy approaches
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Per-city strategic analysis (Phase 1-5)
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

struct StrategicCityAnalysis
{
	StrategicCityAnalysis()
		: iCityID(-1)
		, eLayer(STRATEGIC_LAYER_UNKNOWN)
		, iDefensiveDepth(99)
		, bIsCapital(false)
		, bIsFrontLine(false)
		, bIsSecondLine(false)
		, bIsCore(false)
		, iChokePointCount(0)
		, iTerrainDefenseScore(0)
		, bIsSalient(false)
		, bIsDefensibleSalient(false)
		, bEnemyHasIndirectFire(false)
		, iHostileTilesRing3(0)
		, iFriendlyTilesRing3(0)
		, iAdjacentDefensiveTerrain(0)
		, bIsChokepointCity(false)
		, iApproachCorridors(6)
		, iNarrowCorridors(0)
		, bIsFloodgate(false)
		, iDependentCityCount(0)
		, iRoadPriority(0)
		, bNeedsStrategicRoad(false)
		, eExposure(COASTAL_EXPOSURE_NONE)
		, iWaterTilesRing1(0)
		, iWaterTilesRing2(0)
		, iDeepWaterTilesRing2(0)
		, iLandingZonesRing2(0)
		, bConnectedToOcean(false)
		, eNavalChoke(NAVAL_CHOKE_NONE)
		, bIsNavalCanalCity(false)
		, iStraitTilesNearby(0)
		, iWaterSeparatorLandNearby(0)
		, iNavalChokeWidth(0)
		, iPrimaryWaterAreaID(-1)
		, iConnectedWaterAreaCount(0)
		, bFleetCanReachOcean(false)
		, bEnemyBlocksNavalRoute(false)
		, iAmphibiousThreatScore(0)
		, iHostileSeaApproaches(0)
		, iEnemyNavalPower(0)
		, bVulnerableToAmphibious(false)
	{
	}

	// Phase 1 fields
	int iCityID;
	eStrategicLayer eLayer;
	int iDefensiveDepth;       // Min distance to nearest hostile/neutral border tile
	bool bIsCapital;
	bool bIsFrontLine;
	bool bIsSecondLine;
	bool bIsCore;
	int iChokePointCount;      // Number of adjacent IsChokePoint() tiles
	int iTerrainDefenseScore;  // Aggregate terrain defense bonus of surrounding tiles

	// Phase 2 fields — Salient Detection
	bool bIsSalient;           // City protrudes into hostile territory (hostile/friendly ratio > 2.0)
	bool bIsDefensibleSalient; // Salient but surrounded by forest/jungle — hedgehog viable pre-Indirect Fire
	bool bEnemyHasIndirectFire;// True if any enemy at war has RangeAttackIgnoreLOS units (degrades defensible salient)
	int iHostileTilesRing3;    // Count of hostile-owned tiles within RING3
	int iFriendlyTilesRing3;   // Count of friendly-owned tiles within RING3
	int iAdjacentDefensiveTerrain; // Count of adjacent (RING1) tiles with forest/jungle/hills+forest

	// Phase 3 fields — Chokepoint City Detection
	bool bIsChokepointCity;    // City controls a terrain chokepoint (few open approach corridors)
	int iApproachCorridors;    // Number of wide-open approach directions (0-6; fewer = more choked)
	int iNarrowCorridors;      // Number of directions with corridor width == 1 (partial choke)

	// Phase 4 fields — Floodgate/Dependency Detection
	bool bIsFloodgate;         // Losing this city would expose 2+ other cities to hostile territory
	int iDependentCityCount;   // Number of cities that depend on this city for protection
	std::vector<int> vDependentCities; // City IDs of cities that would become exposed if this city fell

	// Phase 5 fields — Approach Corridor Analysis + Road Priority
	std::vector<EnemyApproach> vEnemyApproaches; // Per-enemy approach data (wartime only)
	int iRoadPriority;         // Derived priority for strategic road building (higher = build first)
	bool bNeedsStrategicRoad;  // True if city lacks road/rail connection to capital

	// Naval Phase 1 fields — Coastal Exposure
	eCoastalExposure eExposure;    // Overall coastal exposure classification
	int iWaterTilesRing1;          // Water tiles in RING1 (0-6)
	int iWaterTilesRing2;          // Water tiles in RING2 only (0-12)
	int iDeepWaterTilesRing2;      // Deep water (ocean) tiles in RING1+RING2 — can receive ocean-going threats
	int iLandingZonesRing2;        // Flat coastal land tiles in RING2 adjacent to non-lake water (amphibious landing spots)
	bool bConnectedToOcean;        // True if adjacent water connects to non-lake ocean body (vs isolated lake)

	// Naval Phase 2 fields — Naval Chokepoint Detection
	eNavalChokeType eNavalChoke;    // Naval chokepoint classification
	bool bIsNavalCanalCity;        // THIS city connects 2+ different water areas (controllable chokepoint)
	int iStraitTilesNearby;        // Count of narrow-water-passage tiles within RING3
	int iWaterSeparatorLandNearby; // Count of IsWaterAreaSeparator() land tiles within RING2 (strait land bridges)
	int iNavalChokeWidth;          // Narrowest water passage width near this city (1-3; 0=none found)

	// Naval Phase 3 fields — Water Area Connectivity
	int iPrimaryWaterAreaID;         // Largest non-lake water area adjacent to city (-1 if inland)
	int iConnectedWaterAreaCount;    // Distinct non-lake water areas reachable from this city via open transits
	bool bFleetCanReachOcean;        // Fleet from this city can reach the global largest ocean body
	bool bEnemyBlocksNavalRoute;     // Enemy controls a transit point that blocks our route to the largest ocean

	// Naval Phase 4 fields — Amphibious Threat Assessment
	int iAmphibiousThreatScore;        // 0-100 composite score: higher = more vulnerable to amphibious attack
	int iHostileSeaApproaches;         // Number of enemy players at war whose fleet can reach this city's water area
	int iEnemyNavalPower;              // Sum of threatening enemy sea military power (normalized by our sea might)
	bool bVulnerableToAmphibious;      // True if score >= 40 and at least one enemy can reach by sea

	// Returns a priority modifier that can be added to threat criteria or zone values.
	// Higher = more strategically important to defend.
	int GetDefensePriorityModifier() const;
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  CLASS: CvStrategicGeographyMap
//!  \brief Persistent strategic layer for AI defense allocation.
//!  Owned by CvMilitaryAI, updated infrequently (every N turns or on city gain/loss).
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

class CvStrategicGeographyMap
{
public:
	CvStrategicGeographyMap();

	void Init(PlayerTypes ePlayer);
	void Update();             // Full recalculation
	bool NeedsUpdate() const;  // Check if it's time to recompute

	// Queries consumed by other systems
	const StrategicCityAnalysis* GetCityAnalysis(int iCityID) const;
	eStrategicLayer GetCityLayer(int iCityID) const;
	int GetDefensiveDepth(int iCityID) const;
	int GetDefensePriorityModifier(int iCityID) const;
	int GetChokePointCount(int iCityID) const;

	// Phase 2: Salient queries
	bool IsCitySalient(int iCityID) const;
	bool IsDefensibleSalient(int iCityID) const;
	bool IsExpendableSalient(int iCityID) const; // Salient AND not defensible AND not capital

	// Phase 3: Chokepoint queries
	bool IsCityChokepoint(int iCityID) const;
	int GetApproachCorridors(int iCityID) const;

	// Phase 4: Floodgate queries
	bool IsCityFloodgate(int iCityID) const;
	int GetDependentCityCount(int iCityID) const;

	// Phase 5: Approach & road queries
	int GetRoadPriority(int iCityID) const;
	bool CityNeedsStrategicRoad(int iCityID) const;
	const std::vector<EnemyApproach>* GetEnemyApproaches(int iCityID) const;
	DirectionTypes GetPrimaryThreatDirection(int iCityID) const;

	// Naval Phase 1: Coastal exposure queries
	eCoastalExposure GetCoastalExposure(int iCityID) const;
	bool IsCityCoastal(int iCityID) const;          // True if eExposure != NONE
	bool IsCityExposedCoast(int iCityID) const;      // True if EXPOSED (5-6 water in RING1)
	int GetLandingZoneCount(int iCityID) const;
	bool IsCityConnectedToOcean(int iCityID) const;

	// Naval Phase 2: Naval chokepoint queries
	eNavalChokeType GetNavalChokeType(int iCityID) const;
	bool CityControlsNavalChokepoint(int iCityID) const; // True if eNavalChoke != NONE
	bool IsCityNavalCanal(int iCityID) const;              // True if city IS a canal (connects 2 water areas)
	int GetNearbyStraitTileCount(int iCityID) const;
	int GetNavalChokeWidth(int iCityID) const;

	// Naval Phase 3: Water connectivity queries
	int GetPrimaryWaterArea(int iCityID) const;
	int GetConnectedWaterAreaCount(int iCityID) const;
	bool CanFleetReachOcean(int iCityID) const;
	bool IsNavalRouteBlocked(int iCityID) const;
	bool AreWaterAreasConnected(int iAreaA, int iAreaB) const;

	// Naval Phase 4: Amphibious threat queries
	int GetAmphibiousThreatScore(int iCityID) const;
	int GetHostileSeaApproaches(int iCityID) const;
	bool IsCityVulnerableToAmphibious(int iCityID) const;

	// Bulk queries
	bool HasAnyCityData() const { return !m_cityAnalysis.empty(); }
	int GetLastUpdateTurn() const { return m_iLastFullUpdate; }
	eGeographicPosture GetGeographicPosture() const { return m_eGeographicPosture; }
	const std::vector<int>& GetPatrolStations() const { return m_vPatrolStations; }

	// Convoy transit queries (Phase I-5)
	const std::vector<PendingTransit>& GetPendingTransits() const { return m_vPendingTransits; }
	void ClearCompletedTransits();

	// Phase I-6: Invasion convoy detection
	struct DetectedConvoy
	{
		PlayerTypes eEnemy;
		int iCenterPlotIndex;     // Approximate centroid of the cluster
		int iEmbarkCount;         // Number of embarked units in the cluster
		bool bHasSettler;         // Cluster contains a settler (highest priority target)
		int iNearestCoastDist;    // Distance from cluster center to our nearest coastal city

		DetectedConvoy() : eEnemy(NO_PLAYER), iCenterPlotIndex(-1), iEmbarkCount(0),
		                   bHasSettler(false), iNearestCoastDist(99) {}
	};
	const std::vector<DetectedConvoy>& GetDetectedConvoys() const { return m_vDetectedConvoys; }
	bool IsInvasionImminent() const { return !m_vDetectedConvoys.empty(); }

	// Phase 6: Logging
	void LogStrategicGeography() const;

private:
	PlayerTypes m_ePlayer;
	int m_iLastFullUpdate;
	std::map<int, StrategicCityAnalysis> m_cityAnalysis;
	eGeographicPosture m_eGeographicPosture;
	std::vector<int> m_vPatrolStations;
	std::vector<PendingTransit> m_vPendingTransits;
	std::vector<DetectedConvoy> m_vDetectedConvoys;

	// Internal computation
	void ClassifyAllCities();
	void ComputeGeographicPosture();
	void DetectSalients();          // Phase 2: salient + defensible salient detection
	void DetectChokepointCities();  // Phase 3: approach corridor analysis
	void BuildDependencyGraph();    // Phase 4: floodgate/dependency detection
	void AnalyzeApproachCorridors();// Phase 5: per-enemy approach analysis
	void DeriveRoadPriorities();    // Phase 5: road priority derivation
	void ComputePatrolStations();   // Patrol station placement for island civs
	int ComputeApproachDifficulty(CvCity* pOurCity, CvCity* pEnemyCity) const;
	int ScanCorridorWidth(CvPlot* pStart, DirectionTypes eDirection) const;
	int ComputeMinBorderDistance(CvCity* pCity) const;
	int CountAdjacentChokepoints(CvCity* pCity) const;
	int ComputeTerrainDefenseScore(CvCity* pCity) const;
	int CountHostileTilesInRing3(CvCity* pCity) const;
	int CountFriendlyTilesInRing3(CvCity* pCity) const;
	int CountAdjacentDefensiveTerrain(CvCity* pCity) const;
	bool DoesAnyEnemyHaveIndirectFire() const;

	// Naval Phase 1: Coastal exposure computation
	void AnalyzeCoastalExposure();

	// Naval Phase 2: Naval chokepoint detection
	void DetectNavalChokepoints();
	int ScanWaterCorridorWidth(CvPlot* pWaterPlot) const;

	// Naval Phase 3: Water area connectivity
	void BuildWaterConnectivityGraph();
	bool IsTransitOpenForPlayer(const WaterAreaEdge& edge) const;
	std::map<int, std::vector<WaterAreaEdge> > m_waterAreaGraph;
	int m_iLargestOceanAreaID;

	// Naval Phase 4: Amphibious threat assessment
	void AssessAmphibiousThreats();

	// Phase I-6: Enemy convoy detection
	void DetectInvasionConvoys();
};

#endif // CIV5_STRATEGIC_GEOGRAPHY_MAP_H
