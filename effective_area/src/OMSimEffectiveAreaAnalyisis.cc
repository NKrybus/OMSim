#include "OMSimEffectiveAreaAnalyisis.hh"
#include "OMSimCommandArgsTable.hh"
#include "OMSimHitManager.hh"
/**
 * @brief Writes a header to the output file.
 */
void OMSimEffectiveAreaAnalyisis::writeHeader()
{
	mDatafile.open(mOutputFileName.c_str(), std::ios::out | std::ios::app);
	mDatafile << "# Phi(deg)"
			  << "\t"
			  << "Theta(deg)"
			  << "\t"
			  << "hits[1perPMT]"
			  << "\t"
			  << "total_hits"		
			  << "\t"
			  << "EA_Total"
			  << "\t"
			  << "EA_Total_error"
			  << "\t" << G4endl;
	mDatafile.close();
}

effectiveAreaResult OMSimEffectiveAreaAnalyisis::calculateEffectiveArea(double pHits)
{
	OMSimCommandArgsTable &lArgs = OMSimCommandArgsTable::getInstance();
	G4double lBeamRadius = lArgs.get<G4double>("radius")/10.; //in cm
	G4double lNumberPhotons = lArgs.get<int>("numevents");
	G4double lBeamArea = CLHEP::pi * lBeamRadius * lBeamRadius;
	G4double lEA = pHits * lBeamArea / lNumberPhotons;
	G4double lEAError = sqrt(pHits) * lBeamArea / lNumberPhotons;
	return { lEA, lEAError };
}

/**
 * @brief Writes a scan result to the output file.
 * @param pPhi The phi angle used in the scan to be written to the output file.
 * @param pTheta The phi angle used in the scan to be written to the output file.
 */
void OMSimEffectiveAreaAnalyisis::writeScan(G4double pPhi, G4double pTheta)
{
	std::vector<double> lHits = OMSimHitManager::getInstance().countHits();

	mDatafile.open(mOutputFileName.c_str(), std::ios::out | std::ios::app);
	mDatafile << pPhi << "\t" << pTheta << "\t";
	G4double lTotalHits = 0;

	for (const auto &hit : lHits)
	{
		mDatafile << hit << "\t";
		lTotalHits = hit; //last element is total
	}

	effectiveAreaResult lEffectiveArea = calculateEffectiveArea(lTotalHits);

	mDatafile << lEffectiveArea.EA << "\t" << lEffectiveArea.EAError << "\t";
	mDatafile << G4endl;
	mDatafile.close();
}