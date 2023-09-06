/**
 * @file OMSim_effective_area.cc
 * @ingroup EffectiveArea
 * @brief Main for the calculation of effective areas.
 * @details The effective area of a module is calculated simulating a plane wave from a certain direction.
 * The photon generation is made with the module AngularScan, running the method runSingleAngularScan(phi, theta) once for each direction to be investigated.
 * Check command line arguments with --help.
 */
#include "OMSim.hh"
#include "OMSimSNAnalysis.hh"
#include "OMSimPrimaryGeneratorAction.hh"
#include "OMSimHitManager.hh"

namespace po = boost::program_options;

void SupernovaNeutrinoSimulation()
{
	OMSimCommandArgsTable &lArgs = OMSimCommandArgsTable::getInstance();
	OMSimHitManager &lHitManager = OMSimHitManager::getInstance();

	//TODO: Check whether this is right, since runmanager and primarygenerators are being
	//called also in OMSim.cc
	OMSimUIinterface &lUIinterface = OMSimUIinterface::getInstance();
	lUIinterface.applyCommand("/selectGun", lArgs.getInstance().get<G4double>("SNgun"));


	OMSimSNAnalysis lAnalysisManager;

	// #TODO 
	G4String ldataoutputname = lArgs.get<std::string>("output_file") + ".dat";
    G4String linfooutputname = lArgs.get<std::string>("output_file") + "_info.dat";
    if (lArgs.get<bool>("SNfixEnergy")) {
    	lAnalysisManager.maininfofile.open(linfooutputname, std::ios::out| std::ios::trunc); 
      }
    lAnalysisManager.datafile.open(ldataoutputname, std::ios::out| std::ios::trunc); 
    lAnalysisManager.WriteHeader();
	// #TODO

}


int main(int argc, char *argv[])
{
	try
	{
		OMSim lSimulation;
		// Do not use G4String as type here...
		po::options_description lSpecific("SN simulation specific arguments");

		lSpecific.add_options()
		("wheight,wh", po::value<G4double>()->default_value(20), "Height of cylindrical world volume, in m")
		("wradius,wr", po::value<G4double>()->default_value(20), "Radius of cylindrical world volume, in m")
		("SNtype", po::value<G4int>()->default_value(0), "0=27 solar mass type II (ls220), 1=9.6 solar mass type II (ls220)")
		("SNgun", po::value<G4int>()->default_value(0), "Select interaction to simulate: 0=ENES, 1=IBD (no neutron capture included)")
		("SNfixEnergy", po::bool_switch(), "Instead of using the energy distribution of the model, it generates all neutrinos from an energy distribution with fixed mean energy and alpha")
		("SNmeanE", po::value<G4double>()->default_value(10.0), "If --SNfixEnergy, use this mean energy to generate the neutrinos ")
		("SNalpha", po::value<G4double>()->default_value(2.5), "If --SNfixEnergy, pinching (alpha) parameter of nu/nubar energy spectrum");


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

		SupernovaNeutrinoSimulation();

		if(lArgs.get<bool>("visual")) lSimulation.startVisualisation();
	
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