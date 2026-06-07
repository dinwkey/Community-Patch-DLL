#include "CvGameCoreDLLPCH.h"
#include "CvSerialize.h"

const std::string EmptyString = "";

#ifdef FAUTOARCHIVE_DEBUG
// FireWorksWin32.lib omits this debug-only helper, but FAutoArchive.h calls it in non-FINAL_RELEASE builds.
void FAutoArchive::debugHelp(FAutoVariableBase&)
{
}
#endif

void CvSyncArchiveCollectDeltas(FAutoArchive& syncArchive, const std::vector<FAutoVariableBase*>& vars)
{
	for (std::vector<FAutoVariableBase*>::const_iterator it = vars.begin(); it != vars.end(); ++it)
	{
		ICvSyncVar& syncVar = static_cast<ICvSyncVar&>(*(*it));
		if (syncVar.hasDelta())
		{
			syncArchive.touch(syncVar);
			syncVar.clearDelta();
		}
	}
}