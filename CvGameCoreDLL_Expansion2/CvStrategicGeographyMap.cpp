/*	-------------------------------------------------------------------------------------------------------
	Sid Meier's Civilization V — Vox Populi
	Strategic Geography Map: persistent terrain-aware layer for AI defense allocation.
	Phase 1: Defensive Layer Classification + Capital Protection.
	Phase 2: Salient Detection + Defensible Salient Exception.
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

	return iModifier;
}

// ---------------------------------------------------------------------------
//  CvStrategicGeographyMap
// ---------------------------------------------------------------------------

CvStrategicGeographyMap::CvStrategicGeographyMap()
	: m_ePlayer(NO_PLAYER)
	, m_iLastFullUpdate(-1)
{
}

void CvStrategicGeographyMap::Init(PlayerTypes ePlayer)
{
	m_ePlayer = ePlayer;
	m_iLastFullUpdate = -1;
	m_cityAnalysis.clear();
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
	if (pAnalysis->iChokePointCount >= 3)
		return false;
	// Defensible salients pre-Indirect Fire are not expendable
	if (pAnalysis->bIsDefensibleSalient && !pAnalysis->bEnemyHasIndirectFire)
		return false;
	return true;
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
