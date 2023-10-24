#include "OMSimEffectiveAreaDetector.hh"
#include "OMSimPDOM.hh"
#include "OMSimLOM16.hh"
#include "OMSimLOM18.hh"
#include "OMSimDEGG.hh"

#include "G4Box.hh"
#include "G4LogicalVolume.hh"

#include "OMSimMDOM.hh"
#include "OMSimCommandArgsTable.hh"
#include "OMSimSensitiveDetector.hh"
#include "OMSimHitManager.hh"
#include "G4SDManager.hh"
#include <G4Orb.hh>


/**
 * @brief Constructs the world volume (sphere).
 */
void OMSimEffectiveAreaDetector::constructWorld()
{
    mWorldSolid = new G4Orb("World", OMSimCommandArgsTable::getInstance().get<G4double>("world_radius") * m);
    mWorldLogical = new G4LogicalVolume(mWorldSolid, mData->getMaterial("argWorld"), "World_log", 0, 0, 0);
    mWorldPhysical = new G4PVPlacement(0, G4ThreeVector(0., 0., 0.), mWorldLogical, "World_phys", 0, false, 0);
    G4VisAttributes *World_vis = new G4VisAttributes(G4Colour(0.45, 0.5, 0.35, 0.));
    mWorldLogical->SetVisAttributes(World_vis);
}

/**
 * @brief Constructs the selected detector from the command line argument and returns the physical world volume.
 * @return Pointer to the physical world volume
 */
void OMSimEffectiveAreaDetector::constructDetector()
{
    OMSimHitManager &lHitManager = OMSimHitManager::getInstance();

    bool lPlaceHarness = OMSimCommandArgsTable::getInstance().get<bool>("place_harness");


/*
    mDOM* mOpticalModule = new mDOM(mData, lPlaceHarness);
    mOpticalModule->placeIt(G4ThreeVector(0, 0, 0), G4RotationMatrix(), mWorldLogical, "");
    mOpticalModule->configureSensitiveVolume(this);

    lHitManager.setNumberOfPMTs(mOpticalModule->getNumberOfPMTs(), mOpticalModule->mIndex);

    mDOM* mOpticalModule2 = new mDOM(mData, lPlaceHarness, 1);
    mOpticalModule2->placeIt(G4ThreeVector(0, 0.5*m, -1.5*m), G4RotationMatrix(), mWorldLogical, "");
    mOpticalModule2->configureSensitiveVolume(this);
    lHitManager.setNumberOfPMTs(mOpticalModule2->getNumberOfPMTs(), mOpticalModule2->mIndex);
*/

    G4double lPhotocathodeThickness = 10*nm;

    G4Box* Photocathode = new G4Box("Photocathode", 20*cm, 20*cm, lPhotocathodeThickness);
    G4Box* Glass = new G4Box("Glass", 20*cm, 20*cm, 1*mm);
    G4Box* Vacuum = new G4Box("Vacuum", 20*cm, 20*cm, 1*cm);
    //G4Box* VacBorder = new G4Box("Photocathode", 20*cm, 20*cm, 1*cm + 20*nm);

/*  VacBorder ->    two sensitive objects (photocathodes) on top and under the vacuum
                    to count reflected and transmitted photons */ 
 
    G4LogicalVolume* PhotocathodeLog = new G4LogicalVolume(Photocathode, mData->getMaterial("NoOptic_Absorber"), "Photocathode");
    G4LogicalVolume* GlassLog = new G4LogicalVolume(Glass, mData->getMaterial("RiAbs_Glass_Tube"), "PMTGlass");
    G4LogicalVolume* VacuumLog = new G4LogicalVolume(Vacuum, mData->getMaterial("Ri_Vacuum"), "PMTvacuum");
    //G4LogicalVolume* VacBorderLog = new G4LogicalVolume(VacBorder, mData->getMaterial("RiAbs_Photocathode"), "VacBorder");

    const G4VisAttributes *lGlassVis = new G4VisAttributes(G4Colour(0.7, 0.7, 0.8, 0.25));
    const G4VisAttributes *lVacuumColor = new G4VisAttributes(G4Colour(1, 0, 0, 0.25));
    GlassLog->SetVisAttributes(lGlassVis);
    VacuumLog->SetVisAttributes(lVacuumColor);
    
    OMSimSensitiveDetector* lSensitiveDetector = new OMSimSensitiveDetector("/Phot/0");
    lSensitiveDetector->setPMTResponse(&NoResponse::getInstance());
    setSensitiveDetector(PhotocathodeLog, lSensitiveDetector);
    lHitManager.setNumberOfPMTs(1, 0);

    new G4PVPlacement(0, G4ThreeVector(0, 0, 0), GlassLog, "PMTGlass", mWorldLogical, false, 0, true);
    new G4PVPlacement(0, G4ThreeVector(0, 0, -(1*mm+lPhotocathodeThickness)), PhotocathodeLog, "Photocathode", mWorldLogical, false, 0, true);
    new G4PVPlacement(0, G4ThreeVector(0, 0, -(1*cm+1*mm)), VacuumLog, "PMTvacuum", mWorldLogical, false, 0, true);
    //new G4PVPlacement(0, G4ThreeVector(0, 0, -(1*cm+1*mm)), VacBorderLog, "VacBorder", mWorldLogical, false, 0, true);





 
}
