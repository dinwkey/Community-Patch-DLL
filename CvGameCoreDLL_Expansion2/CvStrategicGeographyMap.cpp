/*	-------------------------------------------------------------------------------------------------------
	Sid Meier's Civilization V — Vox Populi
	Strategic Geography Map: persistent terrain-aware layer for AI defense allocation.
	Phase 1: Defensive Layer Classification + Capital Protection.
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
