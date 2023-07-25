/**
 * @file OMSim_effective_area.cc
 * @ingroup EffectiveArea
 * @brief Main for the calculation of effective areas.
 * @details The effective area of a module is calculated simulating a plane wave from a certain direction.
 * The photon generation is made with the module @link AngularScan @endlink, running the method runSingleAngularScan(phi, theta) once for each direction to be investigated.
 * Check command line arguments with --help.
 * @warning
 * There are a few material related arguments that are depracated as for example the glass and gel arguments. This were used to easily change materials during the OM development phase. Check @link InputDataManager::getMaterial @endlink and modify the respective OM class if you want to use these args.
 */
#include "OMSim.h"
#include "OMSimDetectorConstruction.hh"
#include "OMSimPhysicsList.hh"
#include "OMSimPrimaryGeneratorAction.hh"
#include "OMSimRunAction.hh"
#include "OMSimEventAction.hh"
#include "OMSimTrackingAction.hh"
#include "OMSimSteppingAction.hh"
// #include "OMSimSteppingVerbose.hh"
#include "OMSimAnalysisManager.hh"
#include "OMSimPMTResponse.hh"
#include "OMSimCommandArgsTable.hh"
#include "OMSimUIinterface.hh"
#include "OMSimSimpleGPSBeams.hh"

#include "G4RunManager.hh"
#include "G4UImanager.hh"
#include "G4UIterminal.hh"
#include "G4ThreeVector.hh"
#include "G4Navigator.hh"

#include "G4UItcsh.hh"
#include "G4VisExecutive.hh"
#include "G4UIExecutive.hh" //xxx

#include <ctime>
#include <sys/time.h>

#include <sstream>
#include <iostream>
#include <fstream>
#include <string>

#include <cmath> 
#include "G4SystemOfUnits.hh"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <TGraph.h>

#include <boost/program_options.hpp>
#include <filesystem>

namespace po = boost::program_options;

void effective_area_simulation()
{
	double startingtime = clock() / CLOCKS_PER_SEC;

	OMSimAnalysisManager &lAnalysisManager = OMSimAnalysisManager::getInstance();
	OMSimCommandArgsTable &lArgs = OMSimCommandArgsTable::getInstance();

	AngularScan *lScanner = new AngularScan(lArgs.get<G4double>("radius"), lArgs.get<G4double>("distance"), lArgs.get<G4double>("wavelength"));

	lAnalysisManager.mOutputFileName = lArgs.get<std::string>("output_file") + ".dat";
	lAnalysisManager.WriteHeader();

	// If file with angle pairs is not provided, use arg theta & phi
	if (!lArgs.keyExists("angles_file"))
	{
		// Use the angle pairs provided through command-line arguments
		lScanner->runSingleAngularScan(lArgs.get<G4double>("phi"), lArgs.get<G4double>("theta"));
		lAnalysisManager.WriteScan(lArgs.get<G4double>("phi"), lArgs.get<G4double>("theta"));
	}
	// File is provided, run over all angle pairs
	else
	{
		std::vector<G4PV2DDataVector> data = InputDataManager::loadtxt(lArgs.get<std::string>("angles_file"), true);
		std::vector<G4double> lThetas = data.at(0);
		std::vector<G4double> lPhis = data.at(1);

		for (std::vector<int>::size_type i = 0; i != lThetas.size(); i++)
		{
			lScanner->runSingleAngularScan(lPhis.at(i), lThetas.at(i));
			lAnalysisManager.WriteScan(lPhis.at(i), lThetas.at(i));
		}
	}
	double finishtime = clock() / CLOCKS_PER_SEC;
	G4cout << "Computation time: " << finishtime - startingtime << " seconds." << G4endl;
}

int main(int argc, char *argv[])
{
	try
	{
		OMSim lSimulation;
		// Do not use G4String as type here...
		po::options_description lSpecific("Effective area specific arguments");

		lSpecific.add_options()
		("world_radius,w", po::value<G4double>()->default_value(3.0), "radius of world sphere in m")
		("radius,r", po::value<G4double>()->default_value(300.0), "plane wave radius in mm")
		("distance,d", po::value<G4double>()->default_value(2000), "plane wave distance from origin, in mm")
		("theta,t", po::value<G4double>()->default_value(0.0), "theta (= zenith) in deg")
		("phi,f", po::value<G4double>()->default_value(0.0), "phi (= azimuth) in deg")
		("wavelength,l", po::value<G4double>()->default_value(400.0), "wavelength of incoming light in nm")
		("angles_file,i", po::value<std::string>(), "The input angle pairs file to be scanned. The file should contain two columns, the first column with the theta (zenith) and the second with phi (azimuth) in degrees.")
		("detector_type", po::value<G4int>()->default_value(2), "module type [custom = 0, Single PMT = 1, mDOM = 2, pDDOM = 3, LOM16 = 4]")
		("string_pos_angle", po::value<G4double>()->default_value(45), "Polar angle of main data cable (viewed from above)");


		po::options_description lAllargs("Allowed input arguments");
		lAllargs.add(lSimulation.mGeneralArgs).add(lSpecific);


		po::variables_map lVariablesMap;
		try {
			po::store(po::parse_command_line(argc, argv, lAllargs), lVariablesMap);
		} catch (std::invalid_argument& e) {
			std::cerr << "Invalid argument: " << e.what() << std::endl;
		} catch (std::exception& e) {
			std::cerr << "An exception occurred: " << e.what() << std::endl;
		} catch (...) {
			std::cerr << "An unknown exception occurred." << std::endl;
		}

		po::notify(lVariablesMap);

		if (lVariablesMap.count("help"))
		{
			std::cout << lAllargs << "\n";
			return 1;
		}

		OMSimCommandArgsTable &lArgs = OMSimCommandArgsTable::getInstance();

		// Now store the parsed parameters in the OMSimCommandArgsTable instance
		for (const auto &option : lVariablesMap)
		{
			lArgs.setParameter(option.first, option.second.value());
		}

		// Now that all parameters are set, "finalize" the OMSimCommandArgsTable instance so that the parameters cannot be modified anymore
		lArgs.finalize();
		lSimulation.initialiseSimulation();

		effective_area_simulation();

	}
	catch (std::exception &e)
	{
		std::cerr << "error: " << e.what() << "\n";
		return 1;
	}
	catch (...)
	{
		std::cerr << "Exception of unknown type!\n";
	}

	return 0;
}
