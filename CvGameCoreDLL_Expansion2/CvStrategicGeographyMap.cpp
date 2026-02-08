/*	-------------------------------------------------------------------------------------------------------
	Sid Meier's Civilization V — Vox Populi
	Strategic Geography Map: persistent terrain-aware layer for AI defense allocation.
	Phase 1: Defensive Layer Classification + Capital Protection.
	Phase 2: Salient Detection + Defensible Salient Exception.
	Phase 3: Chokepoint City Detection + Approach Corridor Analysis.
	Phase 4: Floodgate/Dependency Detection — cities whose loss exposes multiple others.
	Phase 5: Approach Corridor Analysis + Road Priority derivation.
	Phase 6: Full Integration + Tuning (Polish) — logging, settler/production/diplo integration.
	See docs/military-ai/STRATEGIC_GEOGRAPHY_MAP_PLAN.md for design rationale.
	------------------------------------------------------------------------------------------------------- */

#include "CvGameCoreDLLPCH.h"
#include "CvStrategicGeographyMap.h"
#include "CvPlayer.h"
#include "CvCity.h"
#include "CvPlot.h"
#include "CvMap.h"
#include "CvGlobals.h"
#include "CvTeam.h"
#include "CvDiplomacyAI.h"
#include "CvUnit.h"
#include "CvCityCitizens.h"
#include "CvGameCoreUtils.h"
#include "LintFree.h"

// ---------------------------------------------------------------------------
//  StrategicCityAnalysis
// ---------------------------------------------------------------------------

/// Returns a priority modifier for use in UpdateCityThreatCriteria and PrioritizeZones.
/// Positive = more important to defend. Negative = expendable.
int StrategicCityAnalysis::GetDefensePriorityModifier() const
{
	int iModifier = 0;

	// Capital always gets a large boost
	if (bIsCapital)
		iModifier += 120;

	// Layer-based graduated bonuses (replaces flat +40/+15 from commit 6d5f9df81)
	switch (eLayer)
	{
	case STRATEGIC_LAYER_FRONT_LINE:
		// Front-line cities need defense but aren't inherently more valuable
		iModifier += 20;
		break;
	case STRATEGIC_LAYER_SECOND_LINE:
		// Second-line cities are the fallback — important to hold
		iModifier += 35;
		break;
	case STRATEGIC_LAYER_REAR_AREA:
		iModifier += 10;
		break;
	case STRATEGIC_LAYER_CORE:
		// Deep interior — low threat but important if somehow reached
		iModifier += 45;
		break;
	default:
		break;
	}

	// Chokepoint bonus: cities near chokepoints are disproportionately valuable
	// because losing them opens wide approach corridors
	if (iChokePointCount >= 3)
		iModifier += 60;   // city is effectively a chokepoint city
	else if (iChokePointCount >= 1)
		iModifier += 20;   // some chokepoint influence

	// Phase 3: Formal chokepoint city bonus (approach corridor analysis)
	// This is on top of the raw chokepoint count — it captures the "few approach corridors" property
	// that makes a city strategically critical as a bottleneck.
	if (bIsChokepointCity)
	{
		iModifier += 80;  // massive bonus — losing a chokepoint city is catastrophic

		// Extra bonus for extreme chokepoints (only 1 open approach corridor)
		if (iApproachCorridors <= 1)
			iModifier += 40;  // Thermopylae-level chokepoint
	}

	// Phase 4: Floodgate bonus — cities whose loss exposes multiple other cities.
	// Priority second only to capital. Scales with the number of dependent cities.
	if (bIsFloodgate)
	{
		iModifier += 100;  // base floodgate bonus — near-capital importance

		// Extra bonus for each additional dependent city beyond the minimum 2
		if (iDependentCityCount > 2)
			iModifier += (iDependentCityCount - 2) * 15;
	}

	// Phase 2: Salient modifiers
	if (bIsSalient)
	{
		if (bIsDefensibleSalient && !bEnemyHasIndirectFire)
		{
			// Defensible salient pre-Indirect Fire: forest/jungle hedgehog is viable.
			// Treat roughly like a second-line city — worth holding.
			iModifier += 60;
		}
		else if (bIsDefensibleSalient && bEnemyHasIndirectFire)
		{
			// Defensible salient but enemy has Indirect Fire — hedgehog is degraded.
			// Still slightly better than a naked salient but not worth heavy investment.
			iModifier -= 15;
		}
		else
		{
			// Expendable salient: protruding into enemy territory with no terrain advantage.
			// Heavy penalty — don't waste defense resources here, triage toward core.
			iModifier -= 40;
		}
	}

	// Terrain defense bonus (scaled): rough terrain around city makes it more defensible
	// and therefore more worth holding (hills, forests, rivers)
	iModifier += iTerrainDefenseScore / 4;

	// Naval Phase 1: Coastal exposure modifier.
	// Exposed coastal cities face additional naval threat vectors (amphibious assault,
	// naval bombardment) that pure land analysis doesn't capture.
	switch (eExposure)
	{
	case COASTAL_EXPOSURE_EXPOSED:
		// Peninsula tip or island city: 5-6 water tiles in RING1.
		// Very hard to defend — threats from multiple sea directions.
		iModifier += 30;
		// Extra bonus if deep ocean access exists (blue-water threats, not just galleys)
		if (iDeepWaterTilesRing2 > 0)
			iModifier += 15;
		break;
	case COASTAL_EXPOSURE_MODERATE:
		// Standard coastal city: 3-4 water tiles. Needs some naval defense.
		iModifier += 15;
		break;
	case COASTAL_EXPOSURE_SHELTERED:
		// Sheltered harbor: 1-2 water tiles. Minimal naval exposure.
		iModifier += 5;
		break;
	default:
		break;
	}

	// Naval Phase 2: Naval chokepoint modifier.
	// Cities that control naval chokepoints are strategically invaluable —
	// they gate fleet movement between water bodies (canals, straits).
	switch (eNavalChoke)
	{
	case NAVAL_CHOKE_CANAL_CITY:
		// This city IS the canal — losing it severs fleet transit entirely.
		// Massive bonus: worth defending at nearly any cost.
		iModifier += 80;
		break;
	case NAVAL_CHOKE_NEAR_STRAIT:
		// City overlooks a narrow strait — can project power to blockade.
		// Significant bonus but less than a canal city (strait works without the city).
		iModifier += 35;
		// Extra if the strait is very narrow (width 1) — almost a canal equivalent
		if (iNavalChokeWidth == 1)
			iModifier += 20;
		break;
	default:
		break;
	}

	// Naval Phase 3: Connectivity modifier.
	// Canal cities that bridge to the ocean are strategically critical —
	// losing them severs fleet access to the main water body.
	if (bIsNavalCanalCity && bFleetCanReachOcean)
		iModifier += 25;
	// If enemy blocks our route to the ocean, we need stronger local defenses
	// since naval reinforcement/evacuation is impossible.
	if (bEnemyBlocksNavalRoute)
		iModifier += 15;

	return iModifier;
}

// ---------------------------------------------------------------------------
//  CvStrategicGeographyMap
// ---------------------------------------------------------------------------

CvStrategicGeographyMap::CvStrategicGeographyMap()
	: m_ePlayer(NO_PLAYER)
	, m_iLastFullUpdate(-1)
	, m_iLargestOceanAreaID(-1)
{
}

void CvStrategicGeographyMap::Init(PlayerTypes ePlayer)
{
	m_ePlayer = ePlayer;
	m_iLastFullUpdate = -1;
	m_cityAnalysis.clear();
	m_waterAreaGraph.clear();
	m_iLargestOceanAreaID = -1;
}

/// Should we recompute? Every 5 turns peacetime, every 3 turns wartime, or if never computed.
bool CvStrategicGeographyMap::NeedsUpdate() const
{
	if (m_ePlayer == NO_PLAYER)
		return false;

	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	if (kPlayer.getNumCities() == 0)
		return false;

	if (m_iLastFullUpdate < 0)
		return true;

	int iCurrentTurn = GC.getGame().getGameTurn();
	int iTurnsSinceUpdate = iCurrentTurn - m_iLastFullUpdate;

	// At war? Update more frequently.
	bool bAtWar = (kPlayer.CountNumDangerousMajorsAtWarWith(true, false) > 0);
	int iUpdateInterval = bAtWar ? 3 : 5;

	return (iTurnsSinceUpdate >= iUpdateInterval);
}

/// Full recalculation of strategic geography for all owned cities.
void CvStrategicGeographyMap::Update()
{
	if (m_ePlayer == NO_PLAYER)
		return;

	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	if (kPlayer.getNumCities() == 0)
	{
		m_cityAnalysis.clear();
		return;
	}

	m_iLastFullUpdate = GC.getGame().getGameTurn();
	ClassifyAllCities();
	DetectSalients();
	DetectChokepointCities();
	BuildDependencyGraph();
	AnalyzeApproachCorridors();
	DeriveRoadPriorities();
	AnalyzeCoastalExposure();
	DetectNavalChokepoints();
	BuildWaterConnectivityGraph();
	LogStrategicGeography();
}

// ---------------------------------------------------------------------------
//  Query functions
// ---------------------------------------------------------------------------

const StrategicCityAnalysis* CvStrategicGeographyMap::GetCityAnalysis(int iCityID) const
{
	std::map<int, StrategicCityAnalysis>::const_iterator it = m_cityAnalysis.find(iCityID);
	if (it != m_cityAnalysis.end())
		return &(it->second);
	return NULL;
}

eStrategicLayer CvStrategicGeographyMap::GetCityLayer(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->eLayer : STRATEGIC_LAYER_UNKNOWN;
}

int CvStrategicGeographyMap::GetDefensiveDepth(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iDefensiveDepth : 99;
}

int CvStrategicGeographyMap::GetDefensePriorityModifier(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->GetDefensePriorityModifier() : 0;
}

int CvStrategicGeographyMap::GetChokePointCount(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iChokePointCount : 0;
}

bool CvStrategicGeographyMap::IsCitySalient(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->bIsSalient : false;
}

bool CvStrategicGeographyMap::IsDefensibleSalient(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	if (!pAnalysis)
		return false;
	return pAnalysis->bIsDefensibleSalient && !pAnalysis->bEnemyHasIndirectFire;
}

bool CvStrategicGeographyMap::IsExpendableSalient(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	if (!pAnalysis || !pAnalysis->bIsSalient)
		return false;
	// Capital is never expendable, even if it's technically a salient
	if (pAnalysis->bIsCapital)
		return false;
	// Chokepoint salients are not expendable (losing them opens corridors)
	if (pAnalysis->iChokePointCount >= 3 || pAnalysis->bIsChokepointCity)
		return false;
	// Floodgate salients are not expendable (losing them exposes multiple cities)
	if (pAnalysis->bIsFloodgate)
		return false;
	// Defensible salients pre-Indirect Fire are not expendable
	if (pAnalysis->bIsDefensibleSalient && !pAnalysis->bEnemyHasIndirectFire)
		return false;
	return true;
}

bool CvStrategicGeographyMap::IsCityChokepoint(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->bIsChokepointCity : false;
}

int CvStrategicGeographyMap::GetApproachCorridors(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iApproachCorridors : 6;
}

bool CvStrategicGeographyMap::IsCityFloodgate(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->bIsFloodgate : false;
}

int CvStrategicGeographyMap::GetDependentCityCount(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iDependentCityCount : 0;
}

int CvStrategicGeographyMap::GetRoadPriority(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iRoadPriority : 0;
}

bool CvStrategicGeographyMap::CityNeedsStrategicRoad(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->bNeedsStrategicRoad : false;
}

const std::vector<EnemyApproach>* CvStrategicGeographyMap::GetEnemyApproaches(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? &pAnalysis->vEnemyApproaches : NULL;
}

/// Returns the approach direction of the closest/most-dangerous enemy, or NO_DIRECTION.
DirectionTypes CvStrategicGeographyMap::GetPrimaryThreatDirection(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	if (!pAnalysis || pAnalysis->vEnemyApproaches.empty())
		return NO_DIRECTION;

	// Return the direction of the closest enemy approach
	int iMinDist = 99;
	DirectionTypes eBest = NO_DIRECTION;
	for (size_t i = 0; i < pAnalysis->vEnemyApproaches.size(); i++)
	{
		if (pAnalysis->vEnemyApproaches[i].iDistanceFromEnemy < iMinDist)
		{
			iMinDist = pAnalysis->vEnemyApproaches[i].iDistanceFromEnemy;
			eBest = pAnalysis->vEnemyApproaches[i].eLikelyApproachDirection;
		}
	}
	return eBest;
}

// ---------------------------------------------------------------------------
//  Naval Phase 1: Coastal exposure queries
// ---------------------------------------------------------------------------

eCoastalExposure CvStrategicGeographyMap::GetCoastalExposure(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->eExposure : COASTAL_EXPOSURE_NONE;
}

bool CvStrategicGeographyMap::IsCityCoastal(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis && pAnalysis->eExposure != COASTAL_EXPOSURE_NONE;
}

bool CvStrategicGeographyMap::IsCityExposedCoast(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis && pAnalysis->eExposure == COASTAL_EXPOSURE_EXPOSED;
}

int CvStrategicGeographyMap::GetLandingZoneCount(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iLandingZonesRing2 : 0;
}

bool CvStrategicGeographyMap::IsCityConnectedToOcean(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis && pAnalysis->bConnectedToOcean;
}

// ---------------------------------------------------------------------------
//  Naval Phase 2: Naval chokepoint queries
// ---------------------------------------------------------------------------

eNavalChokeType CvStrategicGeographyMap::GetNavalChokeType(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->eNavalChoke : NAVAL_CHOKE_NONE;
}

bool CvStrategicGeographyMap::CityControlsNavalChokepoint(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis && pAnalysis->eNavalChoke != NAVAL_CHOKE_NONE;
}

bool CvStrategicGeographyMap::IsCityNavalCanal(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis && pAnalysis->bIsNavalCanalCity;
}

int CvStrategicGeographyMap::GetNearbyStraitTileCount(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iStraitTilesNearby : 0;
}

int CvStrategicGeographyMap::GetNavalChokeWidth(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iNavalChokeWidth : 0;
}

// ---------------------------------------------------------------------------
//  Naval Phase 3: Water connectivity queries
// ---------------------------------------------------------------------------

int CvStrategicGeographyMap::GetPrimaryWaterArea(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iPrimaryWaterAreaID : -1;
}

int CvStrategicGeographyMap::GetConnectedWaterAreaCount(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->iConnectedWaterAreaCount : 0;
}

bool CvStrategicGeographyMap::CanFleetReachOcean(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->bFleetCanReachOcean : false;
}

bool CvStrategicGeographyMap::IsNavalRouteBlocked(int iCityID) const
{
	const StrategicCityAnalysis* pAnalysis = GetCityAnalysis(iCityID);
	return pAnalysis ? pAnalysis->bEnemyBlocksNavalRoute : false;
}

/// BFS on the water area graph to check if two non-lake water areas are connected
/// through transit points (canal cities / passable forts) that are open to our player.
bool CvStrategicGeographyMap::AreWaterAreasConnected(int iAreaA, int iAreaB) const
{
	if (iAreaA == iAreaB)
		return true;
	if (iAreaA < 0 || iAreaB < 0)
		return false;

	std::set<int> visited;
	std::vector<int> frontier;
	visited.insert(iAreaA);
	frontier.push_back(iAreaA);

	size_t head = 0;
	while (head < frontier.size())
	{
		int iCurrent = frontier[head++];
		std::map<int, std::vector<WaterAreaEdge> >::const_iterator it = m_waterAreaGraph.find(iCurrent);
		if (it == m_waterAreaGraph.end())
			continue;

		const std::vector<WaterAreaEdge>& edges = it->second;
		for (size_t e = 0; e < edges.size(); e++)
		{
			if (visited.find(edges[e].iOtherAreaID) != visited.end())
				continue;
			if (!IsTransitOpenForPlayer(edges[e]))
				continue;
			if (edges[e].iOtherAreaID == iAreaB)
				return true;
			visited.insert(edges[e].iOtherAreaID);
			frontier.push_back(edges[e].iOtherAreaID);
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
//  Internal computation
// ---------------------------------------------------------------------------

/// Classify all cities by defensive layer, terrain features, and chokepoint proximity.
void CvStrategicGeographyMap::ClassifyAllCities()
{
	m_cityAnalysis.clear();

	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	CvCity* pCapital = kPlayer.getCapitalCity();

	int iCityLoop = 0;
	for (CvCity* pCity = kPlayer.firstCity(&iCityLoop); pCity != NULL; pCity = kPlayer.nextCity(&iCityLoop))
	{
		StrategicCityAnalysis analysis;
		analysis.iCityID = pCity->GetID();
		analysis.bIsCapital = pCity->isCapital();

		// Compute minimum border distance to hostile/neutral territory
		analysis.iDefensiveDepth = ComputeMinBorderDistance(pCity);

		// Classify into layer based on defensive depth
		if (analysis.iDefensiveDepth <= 4)
		{
			analysis.eLayer = STRATEGIC_LAYER_FRONT_LINE;
			analysis.bIsFrontLine = true;
		}
		else if (analysis.iDefensiveDepth <= 8)
		{
			analysis.eLayer = STRATEGIC_LAYER_SECOND_LINE;
			analysis.bIsSecondLine = true;
		}
		else if (analysis.iDefensiveDepth <= 12)
		{
			analysis.eLayer = STRATEGIC_LAYER_REAR_AREA;
		}
		else
		{
			analysis.eLayer = STRATEGIC_LAYER_CORE;
			analysis.bIsCore = true;
		}

		// Capital always gets CORE treatment for defense priority, regardless of actual distance.
		// The layer remains accurate (it may be FRONT_LINE), but the capital flag ensures
		// GetDefensePriorityModifier() gives it top priority.
		if (analysis.bIsCapital)
			analysis.bIsCore = true;

		// Count adjacent chokepoint tiles
		analysis.iChokePointCount = CountAdjacentChokepoints(pCity);

		// Compute aggregate terrain defense score
		analysis.iTerrainDefenseScore = ComputeTerrainDefenseScore(pCity);

		m_cityAnalysis[pCity->GetID()] = analysis;
	}
}

/// Compute minimum distance from a city to the nearest tile owned by a hostile or neutral major civ.
/// Returns 99 if no such tile is found within scan range.
int CvStrategicGeographyMap::ComputeMinBorderDistance(CvCity* pCity) const
{
	if (!pCity)
		return 99;

	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	TeamTypes eOurTeam = kPlayer.getTeam();
	CvPlot* pCityPlot = pCity->plot();
	int iMinDist = 99;

	// Scan outward from the city in rings up to range 15.
	// We use RING_PLOTS iteration for efficiency.
	// Start at ring 1 (adjacent) up to ring 15.
	for (int iRing = 1; iRing <= 15; iRing++)
	{
		// Early exit: if we already found a border tile closer than this ring, stop.
		if (iRing > iMinDist)
			break;

		// Iterate plots at exactly this ring distance
		int iStart = (iRing <= 1) ? 0 : RING_PLOTS[iRing - 1];
		int iEnd = RING_PLOTS[iRing];
		for (int i = iStart; i < iEnd; i++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
			if (!pLoopPlot)
				continue;

			// Only consider revealed tiles
			if (!pLoopPlot->isRevealed(eOurTeam))
				continue;

			PlayerTypes ePlotOwner = pLoopPlot->getOwner();
			if (ePlotOwner == NO_PLAYER || ePlotOwner == m_ePlayer)
				continue;

			// Skip minor civs — we care about major civ borders
			if (GET_PLAYER(ePlotOwner).isMinorCiv())
				continue;

			// Is this player hostile or neutral? Check approach/opinion.
			// At war → always counts as hostile border
			if (GET_TEAM(eOurTeam).isAtWar(GET_PLAYER(ePlotOwner).getTeam()))
			{
				int iDist = plotDistance(pCityPlot->getX(), pCityPlot->getY(), pLoopPlot->getX(), pLoopPlot->getY());
				if (iDist < iMinDist)
					iMinDist = iDist;
				continue;
			}

			// Not at war but hostile approach or bad opinion
			CivOpinionTypes eOpinion = kPlayer.GetDiplomacyAI()->GetCivOpinion(ePlotOwner);
			if (eOpinion <= CIV_OPINION_COMPETITOR)
			{
				int iDist = plotDistance(pCityPlot->getX(), pCityPlot->getY(), pLoopPlot->getX(), pLoopPlot->getY());
				if (iDist < iMinDist)
					iMinDist = iDist;
			}
		}
	}

	return iMinDist;
}

/// Count the number of plots adjacent to the city (RING1) that are chokepoints.
int CvStrategicGeographyMap::CountAdjacentChokepoints(CvCity* pCity) const
{
	if (!pCity)
		return 0;

	CvPlot* pCityPlot = pCity->plot();
	int iCount = 0;

	// Check RING1 (6 adjacent tiles) and RING2 (12 more) for chokepoint flags
	for (int i = 0; i < RING2_PLOTS; i++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
		if (pLoopPlot && pLoopPlot->IsChokePoint())
			iCount++;
	}

	return iCount;
}

/// Compute aggregate terrain defense score for plots surrounding the city.
/// Checks RING1 and RING2 for hills, forests, jungles, rivers, mountains.
int CvStrategicGeographyMap::ComputeTerrainDefenseScore(CvCity* pCity) const
{
	if (!pCity)
		return 0;

	CvPlot* pCityPlot = pCity->plot();
	int iScore = 0;

	for (int i = 0; i < RING2_PLOTS; i++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
		if (!pLoopPlot)
			continue;

		// Hills provide defense bonus
		if (pLoopPlot->isHills())
			iScore += 5;

		// Mountains are impassable — block approach vectors
		if (pLoopPlot->isMountain())
			iScore += 8;

		// Forest and jungle block line-of-sight and slow movement
		if (pLoopPlot->HasFeature(FEATURE_FOREST))
			iScore += 3;
		if (pLoopPlot->HasFeature(FEATURE_JUNGLE))
			iScore += 3;

		// River on the plot edge provides defense bonus to defenders
		if (pLoopPlot->isRiver())
			iScore += 4;
	}

	return iScore;
}

// ========================================================================
// Phase 2: Salient Detection + Defensible Salient Exception
// ========================================================================

/// Count hostile-owned tiles within RING3 (37 plots) of a city.
/// "Hostile" means: at war with us, or CIV_OPINION_COMPETITOR or worse.
int CvStrategicGeographyMap::CountHostileTilesInRing3(CvCity* pCity) const
{
	if (!pCity)
		return 0;

	CvPlot* pCityPlot = pCity->plot();
	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	TeamTypes eOurTeam = kPlayer.getTeam();
	int iCount = 0;

	for (int i = 0; i < RING3_PLOTS; i++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
		if (!pLoopPlot)
			continue;

		PlayerTypes ePlotOwner = pLoopPlot->getOwner();
		if (ePlotOwner == NO_PLAYER || ePlotOwner == m_ePlayer)
			continue;

		// Skip minor civs
		if (GET_PLAYER(ePlotOwner).isMinorCiv())
			continue;

		// At war → hostile
		if (GET_TEAM(eOurTeam).isAtWar(GET_PLAYER(ePlotOwner).getTeam()))
		{
			iCount++;
			continue;
		}

		// Competitor or worse → hostile
		CivOpinionTypes eOpinion = kPlayer.GetDiplomacyAI()->GetCivOpinion(ePlotOwner);
		if (eOpinion <= CIV_OPINION_COMPETITOR)
			iCount++;
	}

	return iCount;
}

/// Count friendly-owned tiles within RING3 (37 plots) of a city.
int CvStrategicGeographyMap::CountFriendlyTilesInRing3(CvCity* pCity) const
{
	if (!pCity)
		return 0;

	CvPlot* pCityPlot = pCity->plot();
	int iCount = 0;

	for (int i = 0; i < RING3_PLOTS; i++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
		if (!pLoopPlot)
			continue;

		if (pLoopPlot->getOwner() == m_ePlayer)
			iCount++;
	}

	return iCount;
}

/// Count RING1 (6 adjacent) tiles that have defensive terrain (forest, jungle, hills+forest, hills+jungle).
int CvStrategicGeographyMap::CountAdjacentDefensiveTerrain(CvCity* pCity) const
{
	if (!pCity)
		return 0;

	CvPlot* pCityPlot = pCity->plot();
	int iCount = 0;

	// RING1 is plots 0..RING1_PLOTS-1 but plot 0 is the center; adjacent tiles are 1..6
	// Actually iterateRingPlots index 0 = center, 1-6 = ring1 adjacent tiles
	for (int i = 1; i < RING1_PLOTS; i++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
		if (!pLoopPlot)
			continue;

		bool bDefensive = false;

		// Forest or jungle provide LOS blocking and movement penalty
		if (pLoopPlot->HasFeature(FEATURE_FOREST) || pLoopPlot->HasFeature(FEATURE_JUNGLE))
			bDefensive = true;

		// Hills provide elevation defense; hills + forest/jungle is even better
		if (pLoopPlot->isHills())
			bDefensive = true;

		// Mountain is impassable — counts as a natural wall
		if (pLoopPlot->isMountain())
			bDefensive = true;

		if (bDefensive)
			iCount++;
	}

	return iCount;
}

/// Check if any enemy we are at war with has units with Indirect Fire (RangeAttackIgnoreLOS).
/// This is the era-aware degradation check: once Indirect Fire exists, forest/jungle hedgehog loses value.
bool CvStrategicGeographyMap::DoesAnyEnemyHaveIndirectFire() const
{
	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	TeamTypes eOurTeam = kPlayer.getTeam();

	// Iterate all major players
	for (int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
	{
		PlayerTypes eEnemy = (PlayerTypes)iPlayerLoop;
		if (eEnemy == m_ePlayer)
			continue;

		CvPlayer& kEnemy = GET_PLAYER(eEnemy);
		if (!kEnemy.isAlive() || kEnemy.isMinorCiv())
			continue;

		if (!GET_TEAM(eOurTeam).isAtWar(kEnemy.getTeam()))
			continue;

		// Check this enemy's units for Indirect Fire capability
		int iLoop = 0;
		for (CvUnit* pUnit = kEnemy.firstUnit(&iLoop); pUnit != NULL; pUnit = kEnemy.nextUnit(&iLoop))
		{
			if (pUnit->IsRangeAttackIgnoreLOS())
				return true;
		}
	}

	return false;
}

/// Phase 2 main routine: scan all classified cities and detect salients.
/// A salient is a FRONT_LINE city that protrudes into hostile territory (hostile tiles >> friendly tiles in RING3).
/// A defensible salient has enough adjacent defensive terrain to make a hedgehog viable.
void CvStrategicGeographyMap::DetectSalients()
{
	// First, check once if any enemy has Indirect Fire (expensive, do it once per update)
	bool bEnemyHasIndirectFire = DoesAnyEnemyHaveIndirectFire();

	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	int iLoop = 0;

	for (CvCity* pCity = kPlayer.firstCity(&iLoop); pCity != NULL; pCity = kPlayer.nextCity(&iLoop))
	{
		int iCityID = pCity->GetID();

		// Find the mutable analysis entry for this city
		std::map<int, StrategicCityAnalysis>::iterator it = m_cityAnalysis.find(iCityID);
		if (it == m_cityAnalysis.end())
			continue;

		StrategicCityAnalysis& analysis = it->second;

		// Reset Phase 2 fields
		analysis.bIsSalient = false;
		analysis.bIsDefensibleSalient = false;
		analysis.bEnemyHasIndirectFire = bEnemyHasIndirectFire;
		analysis.iHostileTilesRing3 = 0;
		analysis.iFriendlyTilesRing3 = 0;
		analysis.iAdjacentDefensiveTerrain = 0;

		// Only FRONT_LINE cities can be salients — second-line/rear cities are by definition not protruding
		if (analysis.eLayer != STRATEGIC_LAYER_FRONT_LINE)
			continue;

		// Count hostile and friendly tiles in RING3
		analysis.iHostileTilesRing3 = CountHostileTilesInRing3(pCity);
		analysis.iFriendlyTilesRing3 = CountFriendlyTilesInRing3(pCity);

		// Salient detection: hostile/friendly ratio > 2.0
		// Avoid division by zero: if no friendly tiles, any hostile presence makes it a salient
		int iFriendlyForRatio = max(analysis.iFriendlyTilesRing3, 1);
		if (analysis.iHostileTilesRing3 > iFriendlyForRatio * 2)
		{
			analysis.bIsSalient = true;

			// Check defensibility: >=4 of 6 adjacent tiles have defensive terrain
			analysis.iAdjacentDefensiveTerrain = CountAdjacentDefensiveTerrain(pCity);
			if (analysis.iAdjacentDefensiveTerrain >= 4)
			{
				analysis.bIsDefensibleSalient = true;
			}
		}
	}
}

// ========================================================================
// Phase 3: Chokepoint City Detection + Approach Corridor Analysis
// ========================================================================

/// Scan a corridor in a single hex direction from pStart.
/// Walk up to 3 tiles in eDirection, measuring cross-section width at each step.
/// Cross-section = center tile + one tile to each side (perpendicular).
/// Returns minimum corridor width along the 3-step path (0-3).
/// 0 = completely blocked, 1 = extreme choke, 2 = narrow, 3 = wide open.
int CvStrategicGeographyMap::ScanCorridorWidth(CvPlot* pStart, DirectionTypes eDirection) const
{
	if (!pStart)
		return 0;

	// The two "side" directions perpendicular-ish to eDirection in hex geometry.
	// Using ±1 step (60° offset) gives us the nearest neighbors on either side of the corridor.
	DirectionTypes eLeftDir = (DirectionTypes)(((int)eDirection + NUM_DIRECTION_TYPES - 1) % NUM_DIRECTION_TYPES);
	DirectionTypes eRightDir = (DirectionTypes)(((int)eDirection + 1) % NUM_DIRECTION_TYPES);

	int iMinWidth = 3;
	CvPlot* pCurrent = pStart;

	for (int iStep = 0; iStep < 3; iStep++)
	{
		// Step one tile in the main direction
		pCurrent = plotDirection(pCurrent->getX(), pCurrent->getY(), eDirection);
		if (!pCurrent)
			return 0; // off map edge — direction is blocked

		// If center tile is impassable, this direction is completely blocked
		if (pCurrent->isImpassable(BARBARIAN_TEAM) || pCurrent->isWater())
			return 0;

		// Count passable land tiles in the 3-tile cross-section at this step
		int iWidth = 1; // center tile is passable (checked above)

		CvPlot* pLeft = plotDirection(pCurrent->getX(), pCurrent->getY(), eLeftDir);
		if (pLeft && !pLeft->isImpassable(BARBARIAN_TEAM) && !pLeft->isWater())
			iWidth++;

		CvPlot* pRight = plotDirection(pCurrent->getX(), pCurrent->getY(), eRightDir);
		if (pRight && !pRight->isImpassable(BARBARIAN_TEAM) && !pRight->isWater())
			iWidth++;

		if (iWidth < iMinWidth)
			iMinWidth = iWidth;
	}

	return iMinWidth;
}

/// Phase 3 main routine: analyze approach corridors for all cities and detect chokepoint cities.
/// A chokepoint city has few open approach directions, making it a terrain bottleneck.
/// Losing a chokepoint city is catastrophic because it opens a wide front behind it.
void CvStrategicGeographyMap::DetectChokepointCities()
{
	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	int iLoop = 0;

	for (CvCity* pCity = kPlayer.firstCity(&iLoop); pCity != NULL; pCity = kPlayer.nextCity(&iLoop))
	{
		std::map<int, StrategicCityAnalysis>::iterator it = m_cityAnalysis.find(pCity->GetID());
		if (it == m_cityAnalysis.end())
			continue;

		StrategicCityAnalysis& analysis = it->second;
		CvPlot* pCityPlot = pCity->plot();
		if (!pCityPlot)
			continue;

		// Scan all 6 hex directions for approach corridor width
		int iOpenCorridors = 0;  // Corridors with width >= 2 (wide enough for flanking)
		int iNarrow = 0;         // Corridors with width == 1 (single-file, extreme choke)
		int iBlocked = 0;        // Completely blocked directions (mountains/water)

		for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
		{
			int iWidth = ScanCorridorWidth(pCityPlot, (DirectionTypes)iDir);
			if (iWidth >= 2)
				iOpenCorridors++;
			else if (iWidth == 1)
				iNarrow++;
			else
				iBlocked++;
		}

		analysis.iApproachCorridors = iOpenCorridors;
		analysis.iNarrowCorridors = iNarrow;

		// Chokepoint city criteria (any one is sufficient):
		// 1. Few wide-open approach corridors (≤2 out of 6) — most directions are choked or blocked
		// 2. Many adjacent IsChokePoint() plots (≥3) from Phase 1 — plot-level chokepoint density
		// 3. City's own plot is a chokepoint — sitting directly on a terrain bottleneck
		bool bFewApproaches = (iOpenCorridors <= 2);
		bool bManyChokePlots = (analysis.iChokePointCount >= 3);
		bool bSelfIsChokepoint = pCityPlot->IsChokePoint();

		analysis.bIsChokepointCity = bFewApproaches || bManyChokePlots || bSelfIsChokepoint;
	}
}

// ========================================================================
// Phase 4: Floodgate/Dependency Detection
// ========================================================================

/// Build dependency graph: for each city, determine if losing it would directly
/// expose 2+ other friendly cities to hostile territory.
///
/// Algorithm: For each city C that is currently front-line or second-line:
///   1. Collect the set of tiles owned by C.
///   2. For each other city D that is NOT currently front-line:
///      Check if any of D's tiles are adjacent to tiles owned by C.
///      If C's territory is all that separates D from hostile/unowned territory,
///      then D depends on C for protection.
///   3. If removing C would expose 2+ cities → C is a floodgate.
///
/// The "exposure" check: for each of C's owned tiles that are adjacent to D's
/// owned tiles, check whether the opposite side of C's tile (away from D) is
/// hostile or unowned. If so, C's territory is the buffer between D and danger.
void CvStrategicGeographyMap::BuildDependencyGraph()
{
	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	TeamTypes eOurTeam = kPlayer.getTeam();

	// Reset Phase 4 fields for all cities
	for (std::map<int, StrategicCityAnalysis>::iterator it = m_cityAnalysis.begin(); it != m_cityAnalysis.end(); ++it)
	{
		it->second.bIsFloodgate = false;
		it->second.iDependentCityCount = 0;
		it->second.vDependentCities.clear();
	}

	// Build a set of city IDs for quick lookup
	// Also pre-classify which cities are "interior" (not front-line) — these are potential dependents
	std::vector<int> vFrontLineCityIDs;    // Candidate floodgates (front-line or second-line)
	std::vector<int> vInteriorCityIDs;     // Potential dependents (second-line, rear, core)

	for (std::map<int, StrategicCityAnalysis>::iterator it = m_cityAnalysis.begin(); it != m_cityAnalysis.end(); ++it)
	{
		eStrategicLayer eLayer = it->second.eLayer;
		if (eLayer == STRATEGIC_LAYER_FRONT_LINE)
			vFrontLineCityIDs.push_back(it->first);

		// Interior cities can depend on front-line cities for protection
		if (eLayer == STRATEGIC_LAYER_SECOND_LINE || eLayer == STRATEGIC_LAYER_REAR_AREA || eLayer == STRATEGIC_LAYER_CORE)
			vInteriorCityIDs.push_back(it->first);
	}

	// No front-line cities or no interior cities → no floodgate relationships possible
	if (vFrontLineCityIDs.empty() || vInteriorCityIDs.empty())
		return;

	// For each front-line city C, check if its territory buffers any interior city D
	for (size_t iC = 0; iC < vFrontLineCityIDs.size(); iC++)
	{
		int iCandidateID = vFrontLineCityIDs[iC];
		CvCity* pCandidateCity = kPlayer.getCity(iCandidateID);
		if (!pCandidateCity)
			continue;

		CvPlot* pCandidatePlot = pCandidateCity->plot();
		if (!pCandidatePlot)
			continue;

		std::map<int, StrategicCityAnalysis>::iterator itCandidate = m_cityAnalysis.find(iCandidateID);
		if (itCandidate == m_cityAnalysis.end())
			continue;

		// Collect tiles owned by C within its working radius
		// For each such tile, check if it borders hostile/unowned territory on one side
		// AND borders a different friendly city's territory on the other side.
		// This means C's territory is the buffer.

		// Track which interior cities are shielded by this candidate
		std::set<int> shieldedCities;

		int iNumPlots = pCandidateCity->GetNumWorkablePlots();
		for (int iPlot = 0; iPlot < iNumPlots; iPlot++)
		{
			CvPlot* pTile = pCandidateCity->GetCityCitizens()->GetCityPlotFromIndex(iPlot);
			if (!pTile)
				continue;

			// Must be owned by us
			if (pTile->getOwner() != m_ePlayer)
				continue;

			// Must belong to the candidate city (not a neighboring city that happens to be in range)
			CvCity* pOwningCity = pTile->getOwningCity();
			if (!pOwningCity || pOwningCity->GetID() != iCandidateID)
				continue;

			// Check the 6 neighbors of this tile
			bool bBordersHostile = false;
			bool bBordersFriendlyCity = false;
			int iFriendlyCityID = -1;

			for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
			{
				CvPlot* pNeighbor = plotDirection(pTile->getX(), pTile->getY(), (DirectionTypes)iDir);
				if (!pNeighbor)
					continue;

				PlayerTypes eNeighborOwner = pNeighbor->getOwner();

				// Hostile neighbor: at war, or competitor/unforgivable opinion, or unowned
				if (eNeighborOwner == NO_PLAYER)
				{
					bBordersHostile = true;
				}
				else if (eNeighborOwner != m_ePlayer)
				{
					if (!GET_PLAYER(eNeighborOwner).isMinorCiv())
					{
						if (GET_TEAM(eOurTeam).isAtWar(GET_PLAYER(eNeighborOwner).getTeam()))
						{
							bBordersHostile = true;
						}
						else
						{
							CivOpinionTypes eOpinion = kPlayer.GetDiplomacyAI()->GetCivOpinion(eNeighborOwner);
							if (eOpinion <= CIV_OPINION_COMPETITOR)
								bBordersHostile = true;
						}
					}
				}
				else
				{
					// Neighbor is owned by us — check if it belongs to a DIFFERENT city
					CvCity* pNeighborCity = pNeighbor->getOwningCity();
					if (pNeighborCity && pNeighborCity->GetID() != iCandidateID)
					{
						// Check if that city is an interior city (potential dependent)
						for (size_t iD = 0; iD < vInteriorCityIDs.size(); iD++)
						{
							if (vInteriorCityIDs[iD] == pNeighborCity->GetID())
							{
								bBordersFriendlyCity = true;
								iFriendlyCityID = pNeighborCity->GetID();
								break;
							}
						}
					}
				}
			}

			// If this tile of C borders hostile territory AND a friendly interior city,
			// then C is shielding that city
			if (bBordersHostile && bBordersFriendlyCity && iFriendlyCityID != -1)
			{
				shieldedCities.insert(iFriendlyCityID);
			}
		}

		// If C shields 2+ interior cities, it's a floodgate
		if ((int)shieldedCities.size() >= 2)
		{
			StrategicCityAnalysis& analysis = itCandidate->second;
			analysis.bIsFloodgate = true;
			analysis.iDependentCityCount = (int)shieldedCities.size();
			analysis.vDependentCities.clear();
			for (std::set<int>::iterator sit = shieldedCities.begin(); sit != shieldedCities.end(); ++sit)
			{
				analysis.vDependentCities.push_back(*sit);
			}
		}
	}
}

// ========================================================================
// Phase 5: Approach Corridor Analysis + Road Priority
// ========================================================================

/// Compute approach difficulty for a single enemy city attacking our city.
/// Higher = harder for the enemy (better for us). 0 = trivial approach, 100 = very difficult.
/// Scans RING2 tiles around our city in the approach direction for defensive terrain.
int CvStrategicGeographyMap::ComputeApproachDifficulty(CvCity* pOurCity, CvCity* pEnemyCity) const
{
	if (!pOurCity || !pEnemyCity)
		return 0;

	CvPlot* pOurPlot = pOurCity->plot();
	CvPlot* pEnemyPlot = pEnemyCity->plot();
	if (!pOurPlot || !pEnemyPlot)
		return 0;

	// Estimate approach direction from enemy to us
	DirectionTypes eApproachDir = estimateDirection(pEnemyPlot->getX(), pEnemyPlot->getY(),
		pOurPlot->getX(), pOurPlot->getY());

	if (eApproachDir == NO_DIRECTION)
		return 0;

	// Perpendicular directions for cross-section scanning
	DirectionTypes eLeftDir = (DirectionTypes)(((int)eApproachDir + NUM_DIRECTION_TYPES - 1) % NUM_DIRECTION_TYPES);
	DirectionTypes eRightDir = (DirectionTypes)(((int)eApproachDir + 1) % NUM_DIRECTION_TYPES);

	int iDifficulty = 0;

	// Walk 4 tiles outward from our city in the approach direction
	// checking center + left + right at each step for defensive terrain
	CvPlot* pCurrent = pOurPlot;
	for (int iStep = 0; iStep < 4; iStep++)
	{
		pCurrent = plotDirection(pCurrent->getX(), pCurrent->getY(), eApproachDir);
		if (!pCurrent)
			break;

		// Check center, left, and right tiles at this step
		CvPlot* pScan[3];
		pScan[0] = pCurrent;
		pScan[1] = plotDirection(pCurrent->getX(), pCurrent->getY(), eLeftDir);
		pScan[2] = plotDirection(pCurrent->getX(), pCurrent->getY(), eRightDir);

		for (int j = 0; j < 3; j++)
		{
			if (!pScan[j])
				continue;

			// Mountains completely block this approach tile
			if (pScan[j]->isMountain())
				iDifficulty += 8;
			// Hills slow attackers and give defenders advantage
			else if (pScan[j]->isHills())
				iDifficulty += 4;

			// Forest/jungle slows movement and blocks LOS
			if (pScan[j]->HasFeature(FEATURE_FOREST) || pScan[j]->HasFeature(FEATURE_JUNGLE))
				iDifficulty += 3;

			// River crossings penalize attackers
			if (pScan[j]->isRiver())
				iDifficulty += 3;

			// Chokepoint tiles constrict the approach corridor
			if (pScan[j]->IsChokePoint())
				iDifficulty += 5;

			// Water is impassable for land armies
			if (pScan[j]->isWater())
				iDifficulty += 6;
		}
	}

	// Cap at 100
	return min(iDifficulty, 100);
}

/// Phase 5 main routine: for each city and each hostile/at-war enemy, compute
/// approach distance, direction, and terrain difficulty.
void CvStrategicGeographyMap::AnalyzeApproachCorridors()
{
	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	TeamTypes eOurTeam = kPlayer.getTeam();

	// Collect enemies at war or hostile (competitor+ opinion)
	std::vector<PlayerTypes> vEnemies;
	for (int iP = 0; iP < MAX_MAJOR_CIVS; iP++)
	{
		PlayerTypes eEnemy = (PlayerTypes)iP;
		if (eEnemy == m_ePlayer)
			continue;
		CvPlayer& kEnemy = GET_PLAYER(eEnemy);
		if (!kEnemy.isAlive() || kEnemy.isMinorCiv())
			continue;
		if (kEnemy.getNumCities() == 0)
			continue;

		bool bHostile = false;
		if (GET_TEAM(eOurTeam).isAtWar(kEnemy.getTeam()))
			bHostile = true;
		else
		{
			CivOpinionTypes eOpinion = kPlayer.GetDiplomacyAI()->GetCivOpinion(eEnemy);
			if (eOpinion <= CIV_OPINION_COMPETITOR)
				bHostile = true;
		}

		if (bHostile)
			vEnemies.push_back(eEnemy);
	}

	// For each of our cities, compute approach data for each enemy
	int iCityLoop = 0;
	for (CvCity* pCity = kPlayer.firstCity(&iCityLoop); pCity != NULL; pCity = kPlayer.nextCity(&iCityLoop))
	{
		std::map<int, StrategicCityAnalysis>::iterator it = m_cityAnalysis.find(pCity->GetID());
		if (it == m_cityAnalysis.end())
			continue;

		StrategicCityAnalysis& analysis = it->second;
		analysis.vEnemyApproaches.clear();

		for (size_t iE = 0; iE < vEnemies.size(); iE++)
		{
			PlayerTypes eEnemy = vEnemies[iE];

			// Find the nearest enemy city to this city
			CvCity* pNearestEnemyCity = NULL;
			int iMinDist = 99;
			int iEnemyCityLoop = 0;
			for (CvCity* pEnemyCity = GET_PLAYER(eEnemy).firstCity(&iEnemyCityLoop);
				pEnemyCity != NULL;
				pEnemyCity = GET_PLAYER(eEnemy).nextCity(&iEnemyCityLoop))
			{
				int iDist = plotDistance(pCity->getX(), pCity->getY(), pEnemyCity->getX(), pEnemyCity->getY());
				if (iDist < iMinDist)
				{
					iMinDist = iDist;
					pNearestEnemyCity = pEnemyCity;
				}
			}

			if (!pNearestEnemyCity || iMinDist > 30)
				continue; // Too far away to be a realistic threat

			EnemyApproach approach;
			approach.eEnemy = eEnemy;
			approach.iDistanceFromEnemy = iMinDist;

			// Approach direction: from enemy toward our city
			approach.eLikelyApproachDirection = estimateDirection(
				pNearestEnemyCity->getX(), pNearestEnemyCity->getY(),
				pCity->getX(), pCity->getY());

			// Terrain difficulty along the approach corridor
			approach.iApproachDifficulty = ComputeApproachDifficulty(pCity, pNearestEnemyCity);

			analysis.vEnemyApproaches.push_back(approach);
		}
	}
}

/// Phase 5 road priority: compute a strategic road priority for each city.
/// Higher priority cities should get roads/rails built to them first.
void CvStrategicGeographyMap::DeriveRoadPriorities()
{
	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	CvCity* pCapital = kPlayer.getCapitalCity();

	for (std::map<int, StrategicCityAnalysis>::iterator it = m_cityAnalysis.begin(); it != m_cityAnalysis.end(); ++it)
	{
		StrategicCityAnalysis& analysis = it->second;
		int iPriority = 0;

		// Floodgate cities: high road priority — losing connectivity to them is devastating
		if (analysis.bIsFloodgate)
			iPriority += 50 + analysis.iDependentCityCount * 10;

		// Chokepoint cities: need rapid reinforcement capability
		if (analysis.bIsChokepointCity)
			iPriority += 40;

		// Front-line cities: need supply lines for defense
		if (analysis.bIsFrontLine)
			iPriority += 30;
		else if (analysis.bIsSecondLine)
			iPriority += 15;

		// Capital always gets top road priority
		if (analysis.bIsCapital)
			iPriority += 80;

		// Expendable salients: de-prioritize road building — don't invest in them
		if (analysis.bIsSalient && !analysis.bIsDefensibleSalient && !analysis.bIsFloodgate && !analysis.bIsChokepointCity && !analysis.bIsCapital)
			iPriority -= 20;

		// Check if city has a road connection to capital
		analysis.bNeedsStrategicRoad = false;
		if (pCapital && !analysis.bIsCapital)
		{
			CvCity* pCity = kPlayer.getCity(analysis.iCityID);
			if (pCity)
			{
				// If not connected to capital by road, massive priority boost
				if (!kPlayer.IsCityConnectedToCity(pCapital, pCity, ROUTE_ROAD, true))
				{
					analysis.bNeedsStrategicRoad = true;
					iPriority += 100;
				}
			}
		}

		// Nearby enemy threats increase road priority (need fast reinforcement)
		for (size_t iA = 0; iA < analysis.vEnemyApproaches.size(); iA++)
		{
			if (analysis.vEnemyApproaches[iA].iDistanceFromEnemy <= 10)
				iPriority += 15;
			else if (analysis.vEnemyApproaches[iA].iDistanceFromEnemy <= 20)
				iPriority += 5;
		}

		analysis.iRoadPriority = max(iPriority, 0);
	}
}

// ---------------------------------------------------------------------------
//  Naval Phase 1: Coastal Exposure Analysis
// ---------------------------------------------------------------------------

/// For each coastal city, classify how exposed it is to naval threats by counting
/// water tiles, deep ocean access, and potential amphibious landing zones.
/// This is city-centric (not a full-map water scan) — scales with number of coastal cities.
void CvStrategicGeographyMap::AnalyzeCoastalExposure()
{
	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	int iMinOceanSize = GD_INT_GET(MIN_WATER_SIZE_FOR_OCEAN);

	for (std::map<int, StrategicCityAnalysis>::iterator it = m_cityAnalysis.begin(); it != m_cityAnalysis.end(); ++it)
	{
		StrategicCityAnalysis& analysis = it->second;
		CvCity* pCity = kPlayer.getCity(analysis.iCityID);
		if (!pCity)
			continue;

		CvPlot* pCityPlot = pCity->plot();
		if (!pCityPlot)
			continue;

		// Quick check: skip non-coastal cities entirely.
		// Use a small min-water-size (1) to catch even cities next to very small seas,
		// then filter lakes below with the proper threshold.
		if (!pCity->isCoastal(1))
		{
			analysis.eExposure = COASTAL_EXPOSURE_NONE;
			continue;
		}

		// --- Pass 1: scan RING1 for water tile count + ocean connectivity ---
		int iWaterRing1 = 0;
		bool bOceanConnected = false;

		for (int i = 1; i < RING1_PLOTS; i++)  // skip center (index 0)
		{
			CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
			if (!pLoopPlot)
				continue;

			if (pLoopPlot->isWater())
			{
				// Check if this water body is an ocean (not a lake)
				CvLandmass* pWaterBody = pLoopPlot->landmass();
				if (pWaterBody && !pWaterBody->isLake())
				{
					iWaterRing1++;
					bOceanConnected = true;
				}
				// Lake tiles don't count for coastal exposure — no naval threat from lakes
			}
		}

		// If no ocean-connected water in RING1, this is a lake-only city → not coastally exposed
		if (!bOceanConnected)
		{
			analysis.eExposure = COASTAL_EXPOSURE_NONE;
			continue;
		}

		analysis.iWaterTilesRing1 = iWaterRing1;
		analysis.bConnectedToOcean = true;

		// --- Pass 2: scan RING2 for deeper analysis ---
		int iWaterRing2 = 0;
		int iDeepWater = 0;
		int iLandingZones = 0;

		// Count deep water in RING1 too (for iDeepWaterTilesRing2 which covers RING1+RING2)
		for (int i = 1; i < RING1_PLOTS; i++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
			if (pLoopPlot && pLoopPlot->isDeepWater())
			{
				CvLandmass* pWaterBody = pLoopPlot->landmass();
				if (pWaterBody && !pWaterBody->isLake())
					iDeepWater++;
			}
		}

		// RING2 only: indices RING1_PLOTS..RING2_PLOTS-1
		for (int i = RING1_PLOTS; i < RING2_PLOTS; i++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
			if (!pLoopPlot)
				continue;

			if (pLoopPlot->isWater())
			{
				CvLandmass* pWaterBody = pLoopPlot->landmass();
				if (pWaterBody && !pWaterBody->isLake())
				{
					iWaterRing2++;
					if (pLoopPlot->isDeepWater())
						iDeepWater++;
				}
			}
			else if (pLoopPlot->isFlatlands() && pLoopPlot->isAdjacentToShallowWater())
			{
				// Flat land adjacent to coast water = potential amphibious landing zone
				// Must be adjacent to non-lake water. isAdjacentToShallowWater checks for
				// TERRAIN_COAST adjacency which includes lake coast, but flat land in RING2
				// of a coastal city is overwhelmingly next to real ocean coast.
				// Hills/mountains are not landing zones (units disembark onto flat land).
				iLandingZones++;
			}
		}

		analysis.iWaterTilesRing2 = iWaterRing2;
		analysis.iDeepWaterTilesRing2 = iDeepWater;
		analysis.iLandingZonesRing2 = iLandingZones;

		// --- Classify exposure level based on RING1 water count ---
		if (iWaterRing1 >= 5)
			analysis.eExposure = COASTAL_EXPOSURE_EXPOSED;
		else if (iWaterRing1 >= 3)
			analysis.eExposure = COASTAL_EXPOSURE_MODERATE;
		else
			analysis.eExposure = COASTAL_EXPOSURE_SHELTERED;
	}
}

// ---------------------------------------------------------------------------
//  Naval Phase 2: Naval Chokepoint Detection
// ---------------------------------------------------------------------------

/// Measure the navigable water width at a given water plot.
/// Uses the "brute force axis" method: test all 3 hex axis pairs.
/// For each axis, count consecutive water tiles in both opposite directions.
/// The axis with the longest reach is the "flow" direction; the perpendicular
/// axis gives the "width" of the passage.
/// Returns the minimum perpendicular width found (1 = extreme strait, 2-3 = narrow, 0 = not a strait).
int CvStrategicGeographyMap::ScanWaterCorridorWidth(CvPlot* pWaterPlot) const
{
	if (!pWaterPlot || !pWaterPlot->isWater())
		return 0;

	// Skip lakes — they don't have strategic naval chokepoints
	CvLandmass* pBody = pWaterPlot->landmass();
	if (pBody && pBody->isLake())
		return 0;

	// Hex has 3 axis pairs: (NE/SW=0/3), (E/W=1/4), (SE/NW=2/5)
	// For each axis pair, measure:
	//   flowReach = how far water extends along that axis (both directions)
	//   perpWidth = how many water tiles in the perpendicular cross-section
	int iBestFlowReach = 0;
	int iBestPerpWidth = 99;

	for (int iAxis = 0; iAxis < 3; iAxis++)
	{
		DirectionTypes eDirA = (DirectionTypes)iAxis;              // e.g., NE (0)
		DirectionTypes eDirB = (DirectionTypes)(iAxis + 3);        // e.g., SW (3) — opposite

		// Perpendicular directions: rotate 90° (±2 steps on hex = ±120°... actually ±1 step = ±60°)
		// For hex, "perpendicular" to axis pair {A,B} is the OTHER two axis pairs.
		// We use ±1 step and ±2 steps from eDirA as the perpendicular probes.
		DirectionTypes ePerpL1 = (DirectionTypes)(((int)eDirA + NUM_DIRECTION_TYPES - 1) % NUM_DIRECTION_TYPES);
		DirectionTypes ePerpR1 = (DirectionTypes)(((int)eDirA + 1) % NUM_DIRECTION_TYPES);
		DirectionTypes ePerpL2 = (DirectionTypes)(((int)eDirA + NUM_DIRECTION_TYPES - 2) % NUM_DIRECTION_TYPES);
		DirectionTypes ePerpR2 = (DirectionTypes)(((int)eDirA + 2) % NUM_DIRECTION_TYPES);

		// Measure flow reach along this axis
		int iReachA = 0;
		CvPlot* pProbe = pWaterPlot;
		for (int iStep = 0; iStep < 6; iStep++)
		{
			pProbe = plotDirection(pProbe->getX(), pProbe->getY(), eDirA);
			if (!pProbe || !pProbe->isWater())
				break;
			CvLandmass* pProbeBody = pProbe->landmass();
			if (pProbeBody && pProbeBody->isLake())
				break;
			iReachA++;
		}

		int iReachB = 0;
		pProbe = pWaterPlot;
		for (int iStep = 0; iStep < 6; iStep++)
		{
			pProbe = plotDirection(pProbe->getX(), pProbe->getY(), eDirB);
			if (!pProbe || !pProbe->isWater())
				break;
			CvLandmass* pProbeBody = pProbe->landmass();
			if (pProbeBody && pProbeBody->isLake())
				break;
			iReachB++;
		}

		int iFlowReach = iReachA + iReachB;

		// Only consider this a "flow" axis if water extends at least 3 tiles total along it
		if (iFlowReach < 3)
			continue;

		// Measure perpendicular width: count water tiles at the center in the perpendicular direction.
		// Width = 1 (just this tile) + extend left + extend right.
		int iPerpWidth = 1; // the center tile itself

		// Check 4 perpendicular neighbors (±60° and ±120° from the flow axis)
		// For a narrow strait, at least one pair should be land/blocked.
		CvPlot* pPerpL1 = plotDirection(pWaterPlot->getX(), pWaterPlot->getY(), ePerpL1);
		if (pPerpL1 && pPerpL1->isWater())
		{
			CvLandmass* pPBody = pPerpL1->landmass();
			if (!pPBody || !pPBody->isLake())
				iPerpWidth++;
		}

		CvPlot* pPerpR1 = plotDirection(pWaterPlot->getX(), pWaterPlot->getY(), ePerpR1);
		if (pPerpR1 && pPerpR1->isWater())
		{
			CvLandmass* pPBody = pPerpR1->landmass();
			if (!pPBody || !pPBody->isLake())
				iPerpWidth++;
		}

		CvPlot* pPerpL2 = plotDirection(pWaterPlot->getX(), pWaterPlot->getY(), ePerpL2);
		if (pPerpL2 && pPerpL2->isWater())
		{
			CvLandmass* pPBody = pPerpL2->landmass();
			if (!pPBody || !pPBody->isLake())
				iPerpWidth++;
		}

		CvPlot* pPerpR2 = plotDirection(pWaterPlot->getX(), pWaterPlot->getY(), ePerpR2);
		if (pPerpR2 && pPerpR2->isWater())
		{
			CvLandmass* pPBody = pPerpR2->landmass();
			if (!pPBody || !pPBody->isLake())
				iPerpWidth++;
		}

		// Track the axis with the best flow reach, and use its perpendicular width
		if (iFlowReach > iBestFlowReach || (iFlowReach == iBestFlowReach && iPerpWidth < iBestPerpWidth))
		{
			iBestFlowReach = iFlowReach;
			iBestPerpWidth = iPerpWidth;
		}
	}

	// A strait tile has: good flow (>=3) AND narrow perpendicular width (<=3)
	if (iBestFlowReach >= 3 && iBestPerpWidth <= 3)
		return iBestPerpWidth;

	return 0; // Not a strait
}

/// Detect naval chokepoints near each coastal city.
/// City-centric: for each coastal city, scan RING1-RING3 for:
///   1. Water tiles that are narrow straits (ScanWaterCorridorWidth <= 3)
///   2. Land tiles that are water area separators (IsWaterAreaSeparator — land between 2 water bodies)
///   3. Whether the city itself is a canal city (coastal city connecting 2 different water Areas)
void CvStrategicGeographyMap::DetectNavalChokepoints()
{
	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);

	for (std::map<int, StrategicCityAnalysis>::iterator it = m_cityAnalysis.begin(); it != m_cityAnalysis.end(); ++it)
	{
		StrategicCityAnalysis& analysis = it->second;
		CvCity* pCity = kPlayer.getCity(analysis.iCityID);
		if (!pCity)
			continue;

		// Skip non-coastal cities — no naval chokepoints relevant
		if (analysis.eExposure == COASTAL_EXPOSURE_NONE)
			continue;

		CvPlot* pCityPlot = pCity->plot();
		if (!pCityPlot)
			continue;

		// --- Check 1: Is this city itself a canal city? ---
		// A canal city is a coastal city adjacent to 2+ different non-lake water Areas.
		int iFirstWaterAreaID = -1;
		bool bIsCanalCity = false;

		for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
		{
			CvPlot* pAdj = plotDirection(pCityPlot->getX(), pCityPlot->getY(), (DirectionTypes)iDir);
			if (!pAdj || !pAdj->isWater())
				continue;

			CvLandmass* pWaterBody = pAdj->landmass();
			if (pWaterBody && pWaterBody->isLake())
				continue;

			int iAreaID = pAdj->getArea();
			if (iFirstWaterAreaID == -1)
			{
				iFirstWaterAreaID = iAreaID;
			}
			else if (iAreaID != iFirstWaterAreaID)
			{
				bIsCanalCity = true;
				break;
			}
		}

		analysis.bIsNavalCanalCity = bIsCanalCity;

		// --- Check 2: Scan RING1-RING3 for strait indicators ---
		int iStraitTiles = 0;
		int iSeparatorLand = 0;
		int iNarrowestWidth = 99;

		for (int i = 1; i < RING3_PLOTS; i++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(pCityPlot, i);
			if (!pLoopPlot)
				continue;

			if (pLoopPlot->isWater())
			{
				// Check if this water tile is a narrow strait
				int iWidth = ScanWaterCorridorWidth(pLoopPlot);
				if (iWidth > 0) // iWidth <= 3 means it's a strait
				{
					iStraitTiles++;
					if (iWidth < iNarrowestWidth)
						iNarrowestWidth = iWidth;
				}
			}
			else
			{
				// Check if this is a land bridge between water areas (within RING2 only — closer relevance)
				if (i < RING2_PLOTS && pLoopPlot->IsWaterAreaSeparator())
					iSeparatorLand++;
			}
		}

		analysis.iStraitTilesNearby = iStraitTiles;
		analysis.iWaterSeparatorLandNearby = iSeparatorLand;
		analysis.iNavalChokeWidth = (iNarrowestWidth < 99) ? iNarrowestWidth : 0;

		// --- Classify naval chokepoint type ---
		if (bIsCanalCity)
		{
			// Highest priority: city IS the chokepoint (controllable — owner-only transit)
			analysis.eNavalChoke = NAVAL_CHOKE_CANAL_CITY;
		}
		else if (iStraitTiles >= 2 || iSeparatorLand >= 1)
		{
			// City is near a natural strait or land bridge between water areas
			analysis.eNavalChoke = NAVAL_CHOKE_NEAR_STRAIT;
		}
		else
		{
			analysis.eNavalChoke = NAVAL_CHOKE_NONE;
		}
	}
}

// ---------------------------------------------------------------------------
//  Naval Phase 3: Water Area Connectivity Graph
// ---------------------------------------------------------------------------

/// Check if a water-area transit edge is usable by our military units.
/// Canal cities are owner-only; passable forts require friendly territory (open borders).
bool CvStrategicGeographyMap::IsTransitOpenForPlayer(const WaterAreaEdge& edge) const
{
	// No controller — open (shouldn't happen for inter-area edges, but handle gracefully)
	if (edge.eController == NO_PLAYER)
		return true;

	// We own the transit point — always open
	if (edge.eController == m_ePlayer)
		return true;

	// Canal city — owner-only for military transit
	if (edge.bViaCanalCity)
		return false;

	// Fort — open if we have open borders with the controller
	if (edge.bViaFort)
	{
		TeamTypes eOurTeam = GET_PLAYER(m_ePlayer).getTeam();
		TeamTypes eControllerTeam = GET_PLAYER(edge.eController).getTeam();
		if (eOurTeam == eControllerTeam)
			return true;
		return GET_TEAM(eControllerTeam).IsAllowsOpenBordersToTeam(eOurTeam);
	}

	return false;
}

/// Build the water area connectivity graph and analyze per-city reachability.
///
/// Phase 1: Enumerate non-lake water areas and find the largest ocean.
/// Phase 2: Scan all cities on the map — canal cities create edges between water areas.
/// Phase 3: Scan all map tiles for passable forts on water-area-separator land (fort-canals).
/// Phase 4: BFS from each city's primary water area to determine connectivity.
void CvStrategicGeographyMap::BuildWaterConnectivityGraph()
{
	CvMap& kMap = GC.getMap();
	TeamTypes eOurTeam = GET_PLAYER(m_ePlayer).getTeam();
	int iMinOceanTiles = GD_INT_GET(MIN_WATER_SIZE_FOR_OCEAN);

	// --- Phase 1: Enumerate non-lake water areas, find the largest ocean ---
	m_waterAreaGraph.clear();
	m_iLargestOceanAreaID = -1;
	int iLargestSize = 0;

	int iAreaLoop;
	for (CvArea* pArea = kMap.firstArea(&iAreaLoop); pArea != NULL; pArea = kMap.nextArea(&iAreaLoop))
	{
		if (!pArea->isWater())
			continue;
		if (pArea->getNumTiles() < iMinOceanTiles)
			continue; // skip lakes

		m_waterAreaGraph[pArea->GetID()] = std::vector<WaterAreaEdge>();
		if (pArea->getNumTiles() > iLargestSize)
		{
			iLargestSize = pArea->getNumTiles();
			m_iLargestOceanAreaID = pArea->GetID();
		}
	}

	// No non-lake water areas — nothing to connect
	if (m_waterAreaGraph.empty())
		return;

	// --- Phase 2: Find canal cities (all players) → graph edges ---
	for (int iPlayerLoop = 0; iPlayerLoop < MAX_CIV_PLAYERS; iPlayerLoop++)
	{
		CvPlayer& kLoopPlayer = GET_PLAYER((PlayerTypes)iPlayerLoop);
		if (!kLoopPlayer.isAlive())
			continue;

		int iCityLoop;
		for (CvCity* pCity = kLoopPlayer.firstCity(&iCityLoop); pCity != NULL; pCity = kLoopPlayer.nextCity(&iCityLoop))
		{
			CvPlot* pCityPlot = pCity->plot();
			if (!pCityPlot || pCityPlot->isWater())
				continue;

			// Collect distinct non-lake water areas adjacent to this city
			int aAdjacentAreas[NUM_DIRECTION_TYPES];
			int iUniqueAreas = 0;

			CvPlot** aNeighbors = kMap.getNeighborsUnchecked(pCityPlot);
			for (int d = 0; d < NUM_DIRECTION_TYPES; d++)
			{
				CvPlot* pNeighbor = aNeighbors[d];
				if (!pNeighbor || !pNeighbor->isWater())
					continue;

				int iWaterAreaID = pNeighbor->getArea();
				CvArea* pWaterArea = kMap.getAreaById(iWaterAreaID);
				if (!pWaterArea || pWaterArea->getNumTiles() < iMinOceanTiles)
					continue;

				// Dedup within this city's neighbors
				bool bDup = false;
				for (int u = 0; u < iUniqueAreas; u++)
				{
					if (aAdjacentAreas[u] == iWaterAreaID)
					{
						bDup = true;
						break;
					}
				}
				if (!bDup && iUniqueAreas < NUM_DIRECTION_TYPES)
					aAdjacentAreas[iUniqueAreas++] = iWaterAreaID;
			}

			if (iUniqueAreas < 2)
				continue; // not a canal city

			// Add bidirectional edges for each pair of adjacent water areas
			PlayerTypes eController = pCity->getOwner();
			for (int a = 0; a < iUniqueAreas; a++)
			{
				for (int b = a + 1; b < iUniqueAreas; b++)
				{
					WaterAreaEdge edge;
					edge.iOtherAreaID = aAdjacentAreas[b];
					edge.eController = eController;
					edge.bViaCanalCity = true;
					m_waterAreaGraph[aAdjacentAreas[a]].push_back(edge);

					WaterAreaEdge revEdge;
					revEdge.iOtherAreaID = aAdjacentAreas[a];
					revEdge.eController = eController;
					revEdge.bViaCanalCity = true;
					m_waterAreaGraph[aAdjacentAreas[b]].push_back(revEdge);
				}
			}
		}
	}

	// --- Phase 3: Find fort-canals on water area separator tiles ---
	if (MOD_GLOBAL_PASSABLE_FORTS)
	{
		for (int iPlot = 0; iPlot < kMap.numPlots(); iPlot++)
		{
			CvPlot* pPlot = kMap.plotByIndexUnchecked(iPlot);
			if (pPlot->isWater())
				continue;
			if (!pPlot->IsWaterAreaSeparator())
				continue;
			if (pPlot->isCity())
				continue; // handled in Phase 2
			if (!pPlot->isRevealed(eOurTeam))
				continue;
			if (!pPlot->isOwned())
				continue;
			if (!pPlot->IsImprovementPassable())
				continue;
			if (pPlot->IsImprovementPillaged())
				continue;

			// Passable fort on separator land — find connected water areas
			int aFortAreas[NUM_DIRECTION_TYPES];
			int iFortUniqueAreas = 0;

			CvPlot** aNeighbors = kMap.getNeighborsUnchecked(pPlot);
			for (int d = 0; d < NUM_DIRECTION_TYPES; d++)
			{
				CvPlot* pNeighbor = aNeighbors[d];
				if (!pNeighbor || !pNeighbor->isWater())
					continue;

				int iWaterAreaID = pNeighbor->getArea();
				CvArea* pWaterArea = kMap.getAreaById(iWaterAreaID);
				if (!pWaterArea || pWaterArea->getNumTiles() < iMinOceanTiles)
					continue;

				bool bDup = false;
				for (int u = 0; u < iFortUniqueAreas; u++)
				{
					if (aFortAreas[u] == iWaterAreaID)
					{
						bDup = true;
						break;
					}
				}
				if (!bDup && iFortUniqueAreas < NUM_DIRECTION_TYPES)
					aFortAreas[iFortUniqueAreas++] = iWaterAreaID;
			}

			if (iFortUniqueAreas < 2)
				continue;

			// Add bidirectional edges
			PlayerTypes eFortOwner = pPlot->getOwner();
			for (int a = 0; a < iFortUniqueAreas; a++)
			{
				for (int b = a + 1; b < iFortUniqueAreas; b++)
				{
					WaterAreaEdge edge;
					edge.iOtherAreaID = aFortAreas[b];
					edge.eController = eFortOwner;
					edge.bViaFort = true;
					m_waterAreaGraph[aFortAreas[a]].push_back(edge);

					WaterAreaEdge revEdge;
					revEdge.iOtherAreaID = aFortAreas[a];
					revEdge.eController = eFortOwner;
					revEdge.bViaFort = true;
					m_waterAreaGraph[aFortAreas[b]].push_back(revEdge);
				}
			}
		}
	}

	// --- Phase 4: Per-city connectivity analysis ---
	for (std::map<int, StrategicCityAnalysis>::iterator it = m_cityAnalysis.begin(); it != m_cityAnalysis.end(); ++it)
	{
		StrategicCityAnalysis& analysis = it->second;

		// Default: inland / not connected
		analysis.iPrimaryWaterAreaID = -1;
		analysis.iConnectedWaterAreaCount = 0;
		analysis.bFleetCanReachOcean = false;
		analysis.bEnemyBlocksNavalRoute = false;

		if (analysis.eExposure == COASTAL_EXPOSURE_NONE)
			continue;

		CvCity* pCity = GET_PLAYER(m_ePlayer).getCity(analysis.iCityID);
		if (!pCity)
			continue;

		// Find primary water area (largest non-lake water area adjacent to city)
		int iBestSize = 0;
		CvPlot** aNeighbors = kMap.getNeighborsUnchecked(pCity->plot());
		for (int d = 0; d < NUM_DIRECTION_TYPES; d++)
		{
			CvPlot* pNeighbor = aNeighbors[d];
			if (!pNeighbor || !pNeighbor->isWater())
				continue;
			CvArea* pWaterArea = pNeighbor->area();
			if (!pWaterArea || pWaterArea->getNumTiles() < iMinOceanTiles)
				continue;
			if (pWaterArea->getNumTiles() > iBestSize)
			{
				iBestSize = pWaterArea->getNumTiles();
				analysis.iPrimaryWaterAreaID = pWaterArea->GetID();
			}
		}

		if (analysis.iPrimaryWaterAreaID < 0)
			continue;

		// BFS pass 1: traverse only edges open to our player
		std::set<int> reachableOpen;
		{
			std::vector<int> frontier;
			reachableOpen.insert(analysis.iPrimaryWaterAreaID);
			frontier.push_back(analysis.iPrimaryWaterAreaID);

			size_t head = 0;
			while (head < frontier.size())
			{
				int iCurrent = frontier[head++];
				std::map<int, std::vector<WaterAreaEdge> >::const_iterator graphIt = m_waterAreaGraph.find(iCurrent);
				if (graphIt == m_waterAreaGraph.end())
					continue;

				const std::vector<WaterAreaEdge>& edges = graphIt->second;
				for (size_t e = 0; e < edges.size(); e++)
				{
					if (reachableOpen.find(edges[e].iOtherAreaID) != reachableOpen.end())
						continue;
					if (!IsTransitOpenForPlayer(edges[e]))
						continue;
					reachableOpen.insert(edges[e].iOtherAreaID);
					frontier.push_back(edges[e].iOtherAreaID);
				}
			}
		}

		analysis.iConnectedWaterAreaCount = (int)reachableOpen.size();
		analysis.bFleetCanReachOcean = (reachableOpen.find(m_iLargestOceanAreaID) != reachableOpen.end());

		// BFS pass 2 (only if ocean not reachable): check if enemy blocks the route
		if (!analysis.bFleetCanReachOcean && m_iLargestOceanAreaID >= 0)
		{
			// Traverse ALL edges regardless of openness — if ocean IS reachable
			// ignoring transit control, then an enemy is blocking us.
			std::set<int> reachableAll;
			std::vector<int> frontier;
			reachableAll.insert(analysis.iPrimaryWaterAreaID);
			frontier.push_back(analysis.iPrimaryWaterAreaID);

			size_t head = 0;
			while (head < frontier.size())
			{
				int iCurrent = frontier[head++];
				std::map<int, std::vector<WaterAreaEdge> >::const_iterator graphIt = m_waterAreaGraph.find(iCurrent);
				if (graphIt == m_waterAreaGraph.end())
					continue;

				const std::vector<WaterAreaEdge>& edges = graphIt->second;
				for (size_t e = 0; e < edges.size(); e++)
				{
					if (reachableAll.find(edges[e].iOtherAreaID) != reachableAll.end())
						continue;
					reachableAll.insert(edges[e].iOtherAreaID);
					frontier.push_back(edges[e].iOtherAreaID);
				}
			}

			// Enemy blocks if ocean is reachable via all edges but not via open edges
			analysis.bEnemyBlocksNavalRoute = (reachableAll.find(m_iLargestOceanAreaID) != reachableAll.end());
		}
	}
}

// ---------------------------------------------------------------------------
//  Phase 6: Logging
// ---------------------------------------------------------------------------

/// Output StrategicGeographyLog CSV for debugging.
/// Format: Turn, CityName, Layer, IsSalient, IsChokepoint, IsFloodgate, DefDepth, ApproachCorridors, RoadPriority, PriorityMod
void CvStrategicGeographyMap::LogStrategicGeography() const
{
	if (!GC.getLogging() || !GC.getAILogging())
		return;

	if (m_ePlayer == NO_PLAYER)
		return;

	CvPlayer& kPlayer = GET_PLAYER(m_ePlayer);
	CvString strPlayerName(kPlayer.getCivilizationShortDescription());
	strPlayerName.Replace(' ', '_');

	CvString strLogName;
	if (GC.getPlayerAndCityAILogSplit())
		strLogName = "StrategicGeographyLog_" + strPlayerName + ".csv";
	else
		strLogName = "StrategicGeographyLog.csv";

	FILogFile* pLog = LOGFILEMGR.GetLog(strLogName, FILogFile::kDontTimeStamp);
	if (!pLog)
		return;

	CvString strBaseString;
	strBaseString.Format("%03d, %s, ", GC.getGame().getElapsedGameTurns(), strPlayerName.c_str());

	static const char* szLayerNames[] = { "UNKNOWN", "FRONT_LINE", "SECOND_LINE", "REAR_AREA", "CORE" };
	static const char* szExposureNames[] = { "NONE", "SHELTERED", "MODERATE", "EXPOSED" };
	static const char* szNavalChokeNames[] = { "NONE", "NEAR_STRAIT", "CANAL_CITY" };

	for (std::map<int, StrategicCityAnalysis>::const_iterator it = m_cityAnalysis.begin(); it != m_cityAnalysis.end(); ++it)
	{
		const StrategicCityAnalysis& analysis = it->second;
		CvCity* pCity = kPlayer.getCity(analysis.iCityID);
		if (!pCity)
			continue;

		CvString strCityName(pCity->getName());
		strCityName.Replace(' ', '_');

		CvString strMsg;
		strMsg.Format("%s%s, %s, salient=%d, chokepoint=%d, floodgate=%d, defDepth=%d, corridors=%d, roadPrio=%d, prioMod=%d, deps=%d, coast=%s, waterR1=%d, waterR2=%d, deepW=%d, landing=%d, ocean=%d, navalChoke=%s, canalCity=%d, straitTiles=%d, separators=%d, chokeW=%d, waterArea=%d, connAreas=%d, reachOcean=%d, blocked=%d",
			strBaseString.c_str(),
			strCityName.c_str(),
			(analysis.eLayer >= 0 && analysis.eLayer <= 4) ? szLayerNames[analysis.eLayer] : "???",
			analysis.bIsSalient ? 1 : 0,
			analysis.bIsChokepointCity ? 1 : 0,
			analysis.bIsFloodgate ? 1 : 0,
			analysis.iDefensiveDepth,
			analysis.iApproachCorridors,
			analysis.iRoadPriority,
			analysis.GetDefensePriorityModifier(),
			analysis.iDependentCityCount,
			(analysis.eExposure >= 0 && analysis.eExposure <= 3) ? szExposureNames[analysis.eExposure] : "???",
			analysis.iWaterTilesRing1,
			analysis.iWaterTilesRing2,
			analysis.iDeepWaterTilesRing2,
			analysis.iLandingZonesRing2,
			analysis.bConnectedToOcean ? 1 : 0,
			(analysis.eNavalChoke >= 0 && analysis.eNavalChoke <= 2) ? szNavalChokeNames[analysis.eNavalChoke] : "???",
			analysis.bIsNavalCanalCity ? 1 : 0,
			analysis.iStraitTilesNearby,
			analysis.iWaterSeparatorLandNearby,
			analysis.iNavalChokeWidth,
			analysis.iPrimaryWaterAreaID,
			analysis.iConnectedWaterAreaCount,
			analysis.bFleetCanReachOcean ? 1 : 0,
			analysis.bEnemyBlocksNavalRoute ? 1 : 0);

		pLog->Msg(strMsg.c_str());
	}
}
