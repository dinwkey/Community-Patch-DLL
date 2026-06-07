// Shared inline helpers for strategic and tactical city-target evaluation.

#pragma once

namespace CityTargetingAIHelpers
{
enum CoalitionLiberationCase
{
	COALITION_LIBERATION_NONE,
	COALITION_LIBERATION_CITY_STATE,
	COALITION_LIBERATION_DEAD_MAJOR,
	COALITION_LIBERATION_ORIGINAL_CAPITAL,
	COALITION_LIBERATION_OTHER_MAJOR,
};

inline bool IsCoalitionRelevantCityTargetOwner(PlayerTypes eTargetOwner)
{
	if (eTargetOwner == NO_PLAYER || eTargetOwner == BARBARIAN_PLAYER)
		return false;

	const CvPlayer& kTargetOwner = GET_PLAYER(eTargetOwner);
	return kTargetOwner.isAlive() && (kTargetOwner.isMajorCiv() || kTargetOwner.isMinorCiv());
}

inline CoalitionLiberationCase GetCoalitionLiberationCase(const CvCity* pTargetCity, PlayerTypes eLiberationTarget)
{
	if (!pTargetCity || eLiberationTarget == NO_PLAYER)
		return COALITION_LIBERATION_NONE;

	const CvPlayer& kLiberationTarget = GET_PLAYER(eLiberationTarget);
	if (kLiberationTarget.isMinorCiv())
		return COALITION_LIBERATION_CITY_STATE;

	if (!kLiberationTarget.isMajorCiv())
		return COALITION_LIBERATION_NONE;

	if (!kLiberationTarget.isAlive())
		return COALITION_LIBERATION_DEAD_MAJOR;

	if (pTargetCity->IsOriginalCapitalForPlayer(eLiberationTarget) || (pTargetCity->IsOriginalMajorCapital() && pTargetCity->getOriginalOwner() == eLiberationTarget))
		return COALITION_LIBERATION_ORIGINAL_CAPITAL;

	return COALITION_LIBERATION_OTHER_MAJOR;
}

inline bool ShouldPreferSelfLiberationCapture(const CvPlayer* pPlayer, const CvCity* pTargetCity)
{
	if (!pPlayer || !pTargetCity)
		return false;

	CvDiplomacyAI* pDiploAI = pPlayer->GetDiplomacyAI();
	if (!pDiploAI)
		return false;

	CvPlayer* pMutablePlayer = const_cast<CvPlayer*>(pPlayer);
	CvCity* pMutableCity = const_cast<CvCity*>(pTargetCity);
	PlayerTypes eLiberationTarget = pMutablePlayer->GetPlayerToLiberate(pMutableCity);
	return (eLiberationTarget != NO_PLAYER) && pDiploAI->IsTryingToLiberate(pMutableCity);
}

inline bool IsStrategicallyImportantCapture(const CvPlayer* pPlayer, const CvCity* pTargetCity)
{
	if (!pPlayer || !pTargetCity)
		return false;

	if (pTargetCity->HasAnyWonder() || pTargetCity->GetCityReligions()->IsHolyCityAnyReligion())
		return true;

	if (pTargetCity->IsOriginalMajorCapital() || pTargetCity->getOriginalOwner() == pPlayer->GetID())
		return true;

	CvLandmass* pLandmass = GC.getMap().getLandmassById(pTargetCity->plot()->getLandmass());
	if (pLandmass != NULL && pLandmass->getCitiesPerPlayer(pPlayer->GetID()) <= 1)
		return true;

	return false;
}

inline int GetCaptureCityValuePercent(const CvPlayer* pPlayer, const CvCity* pTargetCity)
{
	if (!pPlayer || !pTargetCity)
		return 0;

	CvCity* pMutableCity = const_cast<CvCity*>(pTargetCity);
	int iMedianEconomicPower = GC.getGame().getMedianEconomicValue();
	int iLocalEconomicPower = pMutableCity->getEconomicValue(pPlayer->GetID());
	int iCityValue = (iLocalEconomicPower * 100) / max(1, iMedianEconomicPower);

	iCityValue *= GD_INT_GET(AI_CITY_VALUE_MULTIPLIER);
	iCityValue /= 100;

	if (pTargetCity->IsOriginalMajorCapital())
	{
		iCityValue *= GD_INT_GET(AI_CAPITAL_VALUE_MULTIPLIER);
		iCityValue /= 100;
	}

	return iCityValue;
}

inline bool IsSelfCaptureBurdensome(const CvPlayer* pPlayer, const CvCity* pTargetCity)
{
	if (!pPlayer || !pTargetCity)
		return false;

	if (ShouldPreferSelfLiberationCapture(pPlayer, pTargetCity) || IsStrategicallyImportantCapture(pPlayer, pTargetCity))
		return false;

	if (GetCaptureCityValuePercent(pPlayer, pTargetCity) >= GD_INT_GET(AI_CITY_SOME_VALUE_THRESHOLD))
		return false;

	int iBurdenScore = 0;
	if (pPlayer->IsEmpireVeryUnhappy())
		iBurdenScore += 3;
	else if (pPlayer->IsEmpireUnhappy())
		iBurdenScore += 2;

	if (pPlayer->GetExcessHappiness() < 0)
		iBurdenScore += 1;

	if (pPlayer->GetPlayerTraits()->IsNoAnnexing())
		iBurdenScore += 1;

	return iBurdenScore >= 3;
}
}