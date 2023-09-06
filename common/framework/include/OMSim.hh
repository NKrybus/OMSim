/**
 * @file OMSim.hh
 * @brief Main simulation class for the Optical Module (OM) simulation.
 * This file defines the OMSim class, which controls the entire simulation process in all studies and includes the general program options.
 * @warning
 * There are a few material related arguments that are depracated as for example the glass and gel arguments. This were used to easily change materials during the OM development phase. Check @link InputDataManager::getMaterial @endlink and modify the respective OM class if you want to use these args.
 * @ingroup common
 */

#ifndef OMSIM_H
#define OMSIM_H

#include "OMSimDetectorConstruction.hh"
#include "OMSimPhysicsList.hh"
#include "OMSimPrimaryGeneratorAction.hh"
#include "OMSimRunAction.hh"
#include "OMSimEventAction.hh"
#include "OMSimTrackingAction.hh"
#include "OMSimSteppingAction.hh"
#include "OMSimUIinterface.hh"

#include <G4RunManager.hh>
#include <G4VisExecutive.hh>
#include <G4UIExecutive.hh>

#include <filesystem>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

/**
 * @class OMSim
 * @brief Controls the main simulation process.
 *
 * The OMSim class is the top-level controller for the simulation. It sets up and
 * manages the individual components of the Geant4 simulation, such as the run manager,
 * the visualization manager, and the navigator. It also enables user
 * interaction with the simulation through a command line interface and visualisation
 * tools.
 * This class is used in the main function of all studies as provides the basic flow of control of any Geant4 simulation.
 * @ingroup common
 */
class OMSim
{
public:
    /**
     * @brief Construct a new OMSim object.
     *
     * This constructor initializes the Geant4 run manager, visualization manager,
     * and navigator. It also sets up the command line arguments for the simulation.
     */
    OMSim();

    ~OMSim();

    /**
     * @brief Ensure that the output directory for the simulation results exists.
     *
     * If the output directory does not exist, this function will create it.
     *
     * @param pFilePath The path to the output directory.
     */
    void ensureOutputDirectoryExists(const std::string &filepath);

    /**
     * @brief Initialize the simulation.
     *
     * This function sets up the necessary Geant4 components.
     */
    void initialiseSimulation();

    /**
     * @brief UIEx session is started for visualisation.
     */
    void startVisualisation();
    OMSimDetectorConstruction* getDetectorConstruction();
    po::options_description mGeneralArgs;

private:
    G4RunManager *mRunManager;
    G4VisExecutive *mVisManager;
    G4Navigator *mNavigator;

    OMSimDetectorConstruction *mDetector = nullptr;
    G4VUserPhysicsList *mPhysics = nullptr;
    G4VUserPrimaryGeneratorAction *mGenAction = nullptr;
    G4UserRunAction *mRunAction = nullptr;
    G4UserEventAction *mEventAction = nullptr;
    G4UserTrackingAction *mTracking = nullptr;
    G4UserSteppingAction *mStepping = nullptr;
    G4TouchableHistory *mHistory = nullptr;
    G4double mStartingTime;
};

#endif // OMSIM_H
