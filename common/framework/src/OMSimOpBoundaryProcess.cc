//
// ********************************************************************
// * License and Disclaimer                                           *
// *                                                                  *
// * The  Geant4 software  is  copyright of the Copyright Holders  of *
// * the Geant4 Collaboration.  It is provided  under  the terms  and *
// * conditions of the Geant4 Software License,  included in the file *
// * LICENSE and available at  http://cern.ch/geant4/license .  These *
// * include a list of copyright holders.                             *
// *                                                                  *
// * Neither the authors of this software system, nor their employing *
// * institutes,nor the agencies providing financial support for this *
// * work  make  any representation or  warranty, express or implied, *
// * regarding  this  software system or assume any liability for its *
// * use.  Please see the license in the file  LICENSE  and URL above *
// * for the full disclaimer and the limitation of liability.         *
// *                                                                  *
// * This  code  implementation is the result of  the  scientific and *
// * technical work of the GEANT4 collaboration.                      *
// * By using,  copying,  modifying or  distributing the software (or *
// * any work based  on the software)  you  agree  to acknowledge its *
// * use  in  resulting  scientific  publications,  and indicate your *
// * acceptance of all terms of the Geant4 Software license.          *
// ********************************************************************
//
////////////////////////////////////////////////////////////////////////
// Optical Photon Boundary Process Class Implementation
////////////////////////////////////////////////////////////////////////
//
// File:        G4OpBoundaryProcess.cc
// Description: Discrete Process -- reflection/refraction at
//                                  optical interfaces
// Version:     1.1
// Created:     1997-06-18
// Modified:    1998-05-25 - Correct parallel component of polarization
//                           (thanks to: Stefano Magni + Giovanni Pieri)
//              1998-05-28 - NULL Rindex pointer before reuse
//                           (thanks to: Stefano Magni)
//              1998-06-11 - delete *sint1 in oblique reflection
//                           (thanks to: Giovanni Pieri)
//              1998-06-19 - move from GetLocalExitNormal() to the new
//                           method: GetLocalExitNormal(&valid) to get
//                           the surface normal in all cases
//              1998-11-07 - NULL OpticalSurface pointer before use
//                           comparison not sharp for: std::abs(cost1) < 1.0
//                           remove sin1, sin2 in lines 556,567
//                           (thanks to Stefano Magni)
//              1999-10-10 - Accommodate changes done in DoAbsorption by
//                           changing logic in DielectricMetal
//              2001-10-18 - avoid Linux (gcc-2.95.2) warning about variables
//                           might be used uninitialized in this function
//                           moved E2_perp, E2_parl and E2_total out of 'if'
//              2003-11-27 - Modified line 168-9 to reflect changes made to
//                           G4OpticalSurface class ( by Fan Lei)
//              2004-02-02 - Set theStatus = Undefined at start of DoIt
//              2005-07-28 - add G4ProcessType to constructor
//              2006-11-04 - add capability of calculating the reflectivity
//                           off a metal surface by way of a complex index
//                           of refraction - Thanks to Sehwook Lee and John
//                           Hauptman (Dept. of Physics - Iowa State Univ.)
//              2009-11-10 - add capability of simulating surface reflections
//                           with Look-Up-Tables (LUT) containing measured
//                           optical reflectance for a variety of surface
//                           treatments - Thanks to Martin Janecek and
//                           William Moses (Lawrence Berkeley National Lab.)
//              2013-06-01 - add the capability of simulating the transmission
//                           of a dichronic filter
//              2017-02-24 - add capability of simulating surface reflections
//                           with Look-Up-Tables (LUT) developed in DAVIS
//
// Author:      Peter Gumplinger
// 		adopted from work by Werner Keil - April 2/96
//
////////////////////////////////////////////////////////////////////////

#include "OMSimOpBoundaryProcess.hh"
#include "OMSimLogger.hh"

#include "G4ios.hh"
#include "G4GeometryTolerance.hh"
#include "G4LogicalBorderSurface.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4OpProcessSubType.hh"
#include "G4OpticalParameters.hh"
#include "G4ParallelWorldProcess.hh"
#include "G4PhysicalConstants.hh"
#include "G4SystemOfUnits.hh"
#include "G4TransportationManager.hh"
#include "G4VSensitiveDetector.hh"
#include <tuple>

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
G4OpBoundaryProcess::G4OpBoundaryProcess(const G4String &processName,
                                         G4ProcessType ptype)
    : G4VDiscreteProcess(processName, ptype)
{
  Initialise();
  if (verboseLevel > 0)
  {
    G4cout << GetProcessName() << " is created " << G4endl;
  }
  SetProcessSubType(fOpBoundary);

  fStatus = Undefined;
  fModel = glisur;
  fFinish = polished;
  fReflectivity = 1.;
  fEfficiency = 0.;
  fTransmittance = 0.;
  fSurfaceRoughness = 0.;
  fProb_sl = 0.;
  fProb_ss = 0.;
  fProb_bs = 0.;

  fRealRIndexMPV = nullptr;
  fImagRIndexMPV = nullptr;
  fMaterial1 = nullptr;
  fMaterial2 = nullptr;
  fOpticalSurface = nullptr;
  fCarTolerance = G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();

  f_iTE = f_iTM = 0;
  fPhotonMomentum = 0.;
  fRindex1 = fRindex2 = 1.;
  fSint1 = 0.;
  fDichroicVector = nullptr;

  fNumWarnings = 0;
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
G4OpBoundaryProcess::~G4OpBoundaryProcess() = default;

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::PreparePhysicsTable(const G4ParticleDefinition &)
{
  Initialise();
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::Initialise()
{
  G4OpticalParameters *params = G4OpticalParameters::Instance();
  SetInvokeSD(params->GetBoundaryInvokeSD());
  SetVerboseLevel(params->GetBoundaryVerboseLevel());
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
G4VParticleChange *G4OpBoundaryProcess::PostStepDoIt(const G4Track &aTrack,
                                                     const G4Step &aStep)
{
  fStatus = Undefined;
  aParticleChange.Initialize(aTrack);
  aParticleChange.ProposeVelocity(aTrack.GetVelocity());
  
  // Get hyperStep from  G4ParallelWorldProcess
  //  NOTE: PostSetpDoIt of this process to be invoked after
  //  G4ParallelWorldProcess!
  const G4Step *pStep = &aStep;
  const G4Step *hStep = G4ParallelWorldProcess::GetHyperStep();
  if (hStep != nullptr) // checks for a hyper step
    pStep = hStep;

  if (pStep->GetPostStepPoint()->GetStepStatus() == fGeomBoundary)
  {
    fMaterial1 = pStep->GetPreStepPoint()->GetMaterial();
    fMaterial2 = pStep->GetPostStepPoint()->GetMaterial();

    // Checks if the photon is at the geometry boundary. If true, it retrieves the materials of the two volumes on either side of the boundary.
  }
  else
  {
    fStatus = NotAtBoundary;
    if (verboseLevel > 1)
      BoundaryProcessVerbose();
    return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
  }

  G4VPhysicalVolume *thePrePV = pStep->GetPreStepPoint()->GetPhysicalVolume();
  G4VPhysicalVolume *thePostPV = pStep->GetPostStepPoint()->GetPhysicalVolume();
  log_critical("New");
  if (verboseLevel >= 2)
  {
    G4cout << " Photon at Boundary! " << G4endl;
    if (thePrePV != nullptr)
      G4cout << " thePrePV:  " << thePrePV->GetName() << G4endl;
    if (thePostPV != nullptr)
      G4cout << " thePostPV: " << thePostPV->GetName() << G4endl;
  }

  G4double stepLength = aTrack.GetStepLength();
  if (stepLength <= fCarTolerance) // If the step length is too small, it proposes a new velocity for the photon and returns.
  {
    fStatus = StepTooSmall;
    if (verboseLevel > 1)
      BoundaryProcessVerbose();

    G4MaterialPropertyVector *groupvel = nullptr;
    G4MaterialPropertiesTable *aMPT = fMaterial2->GetMaterialPropertiesTable();
    if (aMPT != nullptr)
    {
      groupvel = aMPT->GetProperty(kGROUPVEL);
    }

    if (groupvel != nullptr)
    {
      aParticleChange.ProposeVelocity(
          groupvel->Value(fPhotonMomentum, idx_groupvel));
    }
    return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
  }
  else if (stepLength <= 10. * fCarTolerance && fNumWarnings < 10)
  { // see bug 2510
    ++fNumWarnings;
    {
      G4ExceptionDescription ed;
      ed << "G4OpBoundaryProcess: "
         << "Opticalphoton step length: " << stepLength / mm << " mm." << G4endl
         << "This is larger than the threshold " << fCarTolerance / mm << " mm "
                                                                          "to set status StepTooSmall."
         << G4endl
         << "Boundary scattering may be incorrect. ";
      if (fNumWarnings == 10)
      {
        ed << G4endl << "*** Step size warnings stopped.";
      }
      G4Exception("G4OpBoundaryProcess", "OpBoun06", JustWarning, ed, "");
    }
  }

  const G4DynamicParticle *aParticle = aTrack.GetDynamicParticle(); // Getting Photon Information

  fPhotonMomentum = aParticle->GetTotalMomentum();
  fOldMomentum = aParticle->GetMomentumDirection();
  fOldPolarization = aParticle->GetPolarization();

  if (verboseLevel > 1)
  {
    G4cout << " Old Momentum Direction: " << fOldMomentum << G4endl
           << " Old Polarization:       " << fOldPolarization << G4endl;
  }

  G4ThreeVector theGlobalPoint = pStep->GetPostStepPoint()->GetPosition();
  G4bool valid;

  // ID of Navigator which limits step
  G4int hNavId = G4ParallelWorldProcess::GetHypNavigatorID();
  auto iNav = G4TransportationManager::GetTransportationManager()
                  ->GetActiveNavigatorsIterator();
  fGlobalNormal = (iNav[hNavId])->GetGlobalExitNormal(theGlobalPoint, &valid);

  if (valid)
  {
    fGlobalNormal = -fGlobalNormal;
  }
  else
  {
    G4ExceptionDescription ed;
    ed << " G4OpBoundaryProcess/PostStepDoIt(): "
       << " The Navigator reports that it returned an invalid normal" << G4endl;
    G4Exception(
        "G4OpBoundaryProcess::PostStepDoIt", "OpBoun01", EventMustBeAborted, ed,
        "Invalid Surface Normal - Geometry must return valid surface normal");
  }

  if (fOldMomentum * fGlobalNormal > 0.0)
  {
#ifdef G4OPTICAL_DEBUG
    G4ExceptionDescription ed;
    ed << " G4OpBoundaryProcess/PostStepDoIt(): fGlobalNormal points in a "
          "wrong direction. "
       << G4endl
       << "   The momentum of the photon arriving at interface (oldMomentum)"
       << "   must exit the volume cross in the step. " << G4endl
       << "   So it MUST have dot < 0 with the normal that Exits the new "
          "volume (globalNormal)."
       << G4endl << "   >> The dot product of oldMomentum and global Normal is "
       << fOldMomentum * fGlobalNormal << G4endl
       << "     Old Momentum  (during step)     = " << fOldMomentum << G4endl
       << "     Global Normal (Exiting New Vol) = " << fGlobalNormal << G4endl
       << G4endl;
    G4Exception("G4OpBoundaryProcess::PostStepDoIt", "OpBoun02",
                EventMustBeAborted, // Or JustWarning to see if it happens
                                    // repeatedly on one ray
                ed,
                "Invalid Surface Normal - Geometry must return valid surface "
                "normal pointing in the right direction");
#else
    fGlobalNormal = -fGlobalNormal;
#endif
  }

  G4MaterialPropertyVector *rIndexMPV = nullptr;
  G4MaterialPropertyVector *absLengthMPV = nullptr;
  G4MaterialPropertiesTable *MPT = fMaterial1->GetMaterialPropertiesTable();

  if (MPT != nullptr)
  {
    rIndexMPV = MPT->GetProperty(kRINDEX);
  }
  if (rIndexMPV != nullptr)
  {
    fRindex1 = rIndexMPV->Value(fPhotonMomentum, idx_rindex1);
  }
  // newly added, Nico
  if (MPT != nullptr)
  {
    absLengthMPV = MPT->GetProperty(kABSLENGTH);
    if (absLengthMPV != nullptr)
    { // Checking if "kABSLENGTH" property is defined for this material.

      fAbsorptionLength1 = absLengthMPV->Value(fPhotonMomentum, idx_abslength1);
    }
  }
  // end of newly added
  else
  {
    G4cout << "::::::::::returncase" << G4endl;
    fStatus = NoRINDEX; // Handling no refractive index
    if (verboseLevel > 1)
      BoundaryProcessVerbose();
    aParticleChange.ProposeLocalEnergyDeposit(fPhotonMomentum);
    aParticleChange.ProposeTrackStatus(fStopAndKill);
    return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
  }

  fReflectivity = 1.;
  fEfficiency = 0.;
  fTransmittance = 0.;
  fSurfaceRoughness = 0.;
  fModel = glisur;
  fFinish = polished;
  G4SurfaceType type = dielectric_dielectric; // before, coated for Nico

  rIndexMPV = nullptr;
  fOpticalSurface = nullptr;

  // Handling Optical Surface Information
  G4LogicalSurface *surface =
      G4LogicalBorderSurface::GetSurface(thePrePV, thePostPV);
  if (surface == nullptr)
  {
    if (thePostPV->GetMotherLogical() == thePrePV->GetLogicalVolume())
    {
      surface = G4LogicalSkinSurface::GetSurface(thePostPV->GetLogicalVolume());
      if (surface == nullptr)
      {
        surface =
            G4LogicalSkinSurface::GetSurface(thePrePV->GetLogicalVolume());
      }
    }
    else
    {
      surface = G4LogicalSkinSurface::GetSurface(thePrePV->GetLogicalVolume());
      if (surface == nullptr)
      {
        surface =
            G4LogicalSkinSurface::GetSurface(thePostPV->GetLogicalVolume());
      }
    }
  }

  if (surface != nullptr)
  {
    fOpticalSurface =
        dynamic_cast<G4OpticalSurface *>(surface->GetSurfaceProperty());
  }
  if (fOpticalSurface != nullptr)
  {
    type = fOpticalSurface->GetType();
    fModel = fOpticalSurface->GetModel();
    fFinish = fOpticalSurface->GetFinish();

    G4MaterialPropertiesTable *sMPT = fOpticalSurface->GetMaterialPropertiesTable();
    if (sMPT != nullptr)
    {
      if (fFinish == polishedbackpainted || fFinish == groundbackpainted)
      {
        rIndexMPV = sMPT->GetProperty(kRINDEX);
        absLengthMPV = sMPT->GetProperty(kABSLENGTH);
        if (rIndexMPV != nullptr)
        {
          fRindex2 = rIndexMPV->Value(fPhotonMomentum, idx_rindex_surface);
        }

        else
        {
          fStatus = NoRINDEX;
          if (verboseLevel > 1)
            BoundaryProcessVerbose();
          aParticleChange.ProposeLocalEnergyDeposit(fPhotonMomentum);
          aParticleChange.ProposeTrackStatus(fStopAndKill);
          return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
        }
      }

      fRealRIndexMPV = sMPT->GetProperty(kREALRINDEX);
      fImagRIndexMPV = sMPT->GetProperty(kIMAGINARYRINDEX);
      f_iTE = f_iTM = 1;

      G4MaterialPropertyVector *pp;
      if ((pp = sMPT->GetProperty(kREFLECTIVITY)))
      {
        fReflectivity = pp->Value(fPhotonMomentum, idx_reflect);
      }
      else if (fRealRIndexMPV && fImagRIndexMPV)
      {
        CalculateReflectivity();
      }

      if ((pp = sMPT->GetProperty(kEFFICIENCY)))
      {
        fEfficiency = pp->Value(fPhotonMomentum, idx_eff);
      }
      if ((pp = sMPT->GetProperty(kTRANSMITTANCE)))
      {
        fTransmittance = pp->Value(fPhotonMomentum, idx_trans);
      }
      if (sMPT->ConstPropertyExists(kSURFACEROUGHNESS))
      {
        fSurfaceRoughness = sMPT->GetConstProperty(kSURFACEROUGHNESS);
      }

      if (fModel == unified)
      {
        fProb_sl = (pp = sMPT->GetProperty(kSPECULARLOBECONSTANT))
                       ? pp->Value(fPhotonMomentum, idx_lobe)
                       : 0.;
        fProb_ss = (pp = sMPT->GetProperty(kSPECULARSPIKECONSTANT))
                       ? pp->Value(fPhotonMomentum, idx_spike)
                       : 0.;
        fProb_bs = (pp = sMPT->GetProperty(kBACKSCATTERCONSTANT))
                       ? pp->Value(fPhotonMomentum, idx_back)
                       : 0.;
      }
    } // end of if(sMPT)
    else if (fFinish == polishedbackpainted || fFinish == groundbackpainted)
    {
      aParticleChange.ProposeLocalEnergyDeposit(fPhotonMomentum);
      aParticleChange.ProposeTrackStatus(fStopAndKill);
      return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
    }
  } // end of if(fOpticalSurface)

  //  DIELECTRIC-DIELECTRIC
  if (type == dielectric_dielectric)
  {

    if (fFinish == polished || fFinish == ground) // If the finish is polished or ground, it retrieves the refractive index of the second material.
    {
      if (fMaterial1 == fMaterial2)
      {
        fStatus = SameMaterial;
        if (verboseLevel > 1)
          BoundaryProcessVerbose();
        return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
      }
      MPT = fMaterial2->GetMaterialPropertiesTable();
      rIndexMPV = nullptr;
      if (MPT != nullptr)
      {
        rIndexMPV = MPT->GetProperty(kRINDEX);
      }
      if (rIndexMPV != nullptr)
      {
        fRindex2 = rIndexMPV->Value(fPhotonMomentum, idx_rindex2);
      }
      else
      {
        fStatus = NoRINDEX;
        if (verboseLevel > 1)
          BoundaryProcessVerbose();
        aParticleChange.ProposeLocalEnergyDeposit(fPhotonMomentum);
        aParticleChange.ProposeTrackStatus(fStopAndKill);
        return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
      }
    }
    if (fFinish == polishedbackpainted || fFinish == groundbackpainted)
    {
      DielectricDielectric();
    }
    else
    {
      G4double rand = G4UniformRand(); // Handles random processes, such as absorption, transmission, and reflection, based on random numbers
      if (rand > fReflectivity + fTransmittance)
      {
        DoAbsorption();
      }
      else if (rand > fReflectivity)
      {
        fStatus = Transmission;
        fNewMomentum = fOldMomentum;
        fNewPolarization = fOldPolarization;
      }
      else
      {
        if (fFinish == polishedfrontpainted) // Determines the reflection process based on the finish type
        {
          DoReflection();
        }
        else if (fFinish == groundfrontpainted)
        {
          fStatus = LambertianReflection;
          DoReflection();
        }
        else
        {
          DielectricDielectric();
        }
      }
    }
  }
  else if (type == dielectric_metal) // Calls functions corresponding to different boundary types
  {
    DielectricMetal();
  }
  else if (type == dielectric_LUT)
  {
    DielectricLUT();
  }
  else if (type == dielectric_LUTDAVIS)
  {
    DielectricLUTDAVIS();
  }
  else if (type == dielectric_dichroic)
  {
    DielectricDichroic();
  }
  else if (type == coated)
  {
    // CoatedDielectricDielectric(); //<- old type
    PhotocathodeCoated(); //<- Nico type
  }
  else
  {
    G4ExceptionDescription ed;
    ed << " PostStepDoIt(): Illegal boundary type." << G4endl;
    G4Exception("G4OpBoundaryProcess", "OpBoun04", JustWarning, ed);
    return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
  }

  // Normalization and Verbose Output
  fNewMomentum = fNewMomentum.unit();
  fNewPolarization = fNewPolarization.unit();

  if (verboseLevel > 1)
  {
    G4cout << " New Momentum Direction: " << fNewMomentum << G4endl
           << " New Polarization:       " << fNewPolarization << G4endl;
    BoundaryProcessVerbose();
  }

  // Proposes the new momentum and polarization directions to the G4VParticleChange object
  aParticleChange.ProposeMomentumDirection(fNewMomentum);
  aParticleChange.ProposePolarization(fNewPolarization);

  if (fStatus == FresnelRefraction || fStatus == Transmission)
  {
    // not all surface types check that fMaterial2 has an MPT
    G4MaterialPropertiesTable *aMPT = fMaterial2->GetMaterialPropertiesTable();
    G4MaterialPropertyVector *groupvel = nullptr;
    if (aMPT != nullptr)
    {
      groupvel = aMPT->GetProperty(kGROUPVEL);
    }
    if (groupvel != nullptr)
    {
      aParticleChange.ProposeVelocity(
          groupvel->Value(fPhotonMomentum, idx_groupvel));
    }
  }

  if (fStatus == Detection && fInvokeSD) // Invokes the sensitive detector if the status is "Detection" and the detector is configured to be invoked
    InvokeSD(pStep);
  return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::BoundaryProcessVerbose() const // Outputs verbose information about the current status of the boundary process
{
  G4cout << " *** ";
  if (fStatus == Undefined)
    G4cout << "Undefined";
  else if (fStatus == Transmission)
    G4cout << "Transmission";
  else if (fStatus == FresnelRefraction)
    G4cout << "FresnelRefraction";
  else if (fStatus == FresnelReflection)
    G4cout << "FresnelReflection";
  else if (fStatus == TotalInternalReflection)
    G4cout << "TotalInternalReflection";
  else if (fStatus == LambertianReflection)
    G4cout << "LambertianReflection";
  else if (fStatus == LobeReflection)
    G4cout << "LobeReflection";
  else if (fStatus == SpikeReflection)
    G4cout << "SpikeReflection";
  else if (fStatus == BackScattering)
    G4cout << "BackScattering";
  else if (fStatus == PolishedLumirrorAirReflection)
    G4cout << "PolishedLumirrorAirReflection";
  else if (fStatus == PolishedLumirrorGlueReflection)
    G4cout << "PolishedLumirrorGlueReflection";
  else if (fStatus == PolishedAirReflection)
    G4cout << "PolishedAirReflection";
  else if (fStatus == PolishedTeflonAirReflection)
    G4cout << "PolishedTeflonAirReflection";
  else if (fStatus == PolishedTiOAirReflection)
    G4cout << "PolishedTiOAirReflection";
  else if (fStatus == PolishedTyvekAirReflection)
    G4cout << "PolishedTyvekAirReflection";
  else if (fStatus == PolishedVM2000AirReflection)
    G4cout << "PolishedVM2000AirReflection";
  else if (fStatus == PolishedVM2000GlueReflection)
    G4cout << "PolishedVM2000GlueReflection";
  else if (fStatus == EtchedLumirrorAirReflection)
    G4cout << "EtchedLumirrorAirReflection";
  else if (fStatus == EtchedLumirrorGlueReflection)
    G4cout << "EtchedLumirrorGlueReflection";
  else if (fStatus == EtchedAirReflection)
    G4cout << "EtchedAirReflection";
  else if (fStatus == EtchedTeflonAirReflection)
    G4cout << "EtchedTeflonAirReflection";
  else if (fStatus == EtchedTiOAirReflection)
    G4cout << "EtchedTiOAirReflection";
  else if (fStatus == EtchedTyvekAirReflection)
    G4cout << "EtchedTyvekAirReflection";
  else if (fStatus == EtchedVM2000AirReflection)
    G4cout << "EtchedVM2000AirReflection";
  else if (fStatus == EtchedVM2000GlueReflection)
    G4cout << "EtchedVM2000GlueReflection";
  else if (fStatus == GroundLumirrorAirReflection)
    G4cout << "GroundLumirrorAirReflection";
  else if (fStatus == GroundLumirrorGlueReflection)
    G4cout << "GroundLumirrorGlueReflection";
  else if (fStatus == GroundAirReflection)
    G4cout << "GroundAirReflection";
  else if (fStatus == GroundTeflonAirReflection)
    G4cout << "GroundTeflonAirReflection";
  else if (fStatus == GroundTiOAirReflection)
    G4cout << "GroundTiOAirReflection";
  else if (fStatus == GroundTyvekAirReflection)
    G4cout << "GroundTyvekAirReflection";
  else if (fStatus == GroundVM2000AirReflection)
    G4cout << "GroundVM2000AirReflection";
  else if (fStatus == GroundVM2000GlueReflection)
    G4cout << "GroundVM2000GlueReflection";
  else if (fStatus == Absorption)
    G4cout << "Absorption";
  else if (fStatus == Detection)
    G4cout << "Detection";
  else if (fStatus == NotAtBoundary)
    G4cout << "NotAtBoundary";
  else if (fStatus == SameMaterial)
    G4cout << "SameMaterial";
  else if (fStatus == StepTooSmall)
    G4cout << "StepTooSmall";
  else if (fStatus == NoRINDEX)
    G4cout << "NoRINDEX";
  else if (fStatus == Dichroic)
    G4cout << "Dichroic Transmission";
  else if (fStatus == CoatedDielectricReflection)
    G4cout << "Coated Dielectric Reflection";
  else if (fStatus == CoatedDielectricRefraction)
    G4cout << "Coated Dielectric Refraction";
  else if (fStatus == CoatedDielectricFrustratedTransmission)
    G4cout << "Coated Dielectric Frustrated Transmission";

  G4cout << " ***" << G4endl;
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
/*
this function is responsible for generating a facet normal vector based on the specified surface model and properties.
The generated normal is used in subsequent calculations related to the reflection or transmission of photons at the optical boundary
*/
G4ThreeVector G4OpBoundaryProcess::GetFacetNormal(
    const G4ThreeVector &momentum, const G4ThreeVector &normal) const
{
  G4ThreeVector facetNormal;
  if (fModel == unified || fModel == LUT || fModel == DAVIS)
  {
    /* This function codes alpha to a random value taken from the
    distribution p(alpha) = g(alpha; 0, sigma_alpha)*std::sin(alpha),
    for alpha > 0 and alpha < 90, where g(alpha; 0, sigma_alpha) is a
    gaussian distribution with mean 0 and standard deviation sigma_alpha.  */

    G4double sigma_alpha = 0.0;
    if (fOpticalSurface)
      sigma_alpha = fOpticalSurface->GetSigmaAlpha();
    if (sigma_alpha == 0.0)
    {
      return normal;
    }

    G4double f_max = std::min(1.0, 4. * sigma_alpha);
    G4double alpha, phi, sinAlpha;

    do
    { // Loop checking, 13-Aug-2015, Peter Gumplinger
      do
      { // Loop checking, 13-Aug-2015, Peter Gumplinger
        alpha = G4RandGauss::shoot(0.0, sigma_alpha);
        sinAlpha = std::sin(alpha);
      } while (G4UniformRand() * f_max > sinAlpha || alpha >= halfpi);

      phi = G4UniformRand() * twopi;
      facetNormal.set(sinAlpha * std::cos(phi), sinAlpha * std::sin(phi),
                      std::cos(alpha));
      facetNormal.rotateUz(normal);
    } while (momentum * facetNormal >= 0.0);
  }
  else // For models other than unified, LUT, or DAVIS, it generates a facet normal
  {
    G4double polish = 1.0;
    if (fOpticalSurface)
      polish = fOpticalSurface->GetPolish();
    if (polish < 1.0) // If the surface has polish less than 1.0, it applies a smoothing operation by perturbing the normal vector
    {
      do
      { // Loop checking, 13-Aug-2015, Peter Gumplinger
        G4ThreeVector smear;
        do
        { // Loop checking, 13-Aug-2015, Peter Gumplinger
          smear.setX(2. * G4UniformRand() - 1.);
          smear.setY(2. * G4UniformRand() - 1.);
          smear.setZ(2. * G4UniformRand() - 1.);
        } while (smear.mag2() > 1.0);
        facetNormal = normal + (1. - polish) * smear;
      } while (momentum * facetNormal >= 0.0);
      facetNormal = facetNormal.unit(); // Ensures the generated facet normal is not in the same direction as the incident momentum
    }
    else
    {
      facetNormal = normal;
    }
  }
  return facetNormal;
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::DielectricMetal()
{
  G4int n = 0;
  G4double rand;
  G4ThreeVector A_trans;

  do
  {
    ++n;
    rand = G4UniformRand();
    if (rand > fReflectivity && n == 1)
    {
      if (rand > fReflectivity + fTransmittance)
      {
        DoAbsorption();
      }
      else
      {
        fStatus = Transmission;
        fNewMomentum = fOldMomentum;
        fNewPolarization = fOldPolarization;
      }
      break;
    }
    else
    {
      if (fRealRIndexMPV && fImagRIndexMPV)
      {
        if (n > 1)
        {
          CalculateReflectivity();
          if (!G4BooleanRand(fReflectivity))
          {
            DoAbsorption();
            break;
          }
        }
      }
      if (fModel == glisur || fFinish == polished)
      {
        DoReflection();
      }
      else
      {
        if (n == 1)
          ChooseReflection();
        if (fStatus == LambertianReflection)
        {
          DoReflection();
        }
        else if (fStatus == BackScattering)
        {
          fNewMomentum = -fOldMomentum;
          fNewPolarization = -fOldPolarization;
        }
        else
        {
          if (fStatus == LobeReflection)
          {
            if (!fRealRIndexMPV || !fImagRIndexMPV)
            {
              fFacetNormal = GetFacetNormal(fOldMomentum, fGlobalNormal);
            }
            // else
            //  case of complex rindex needs to be implemented
          }
          fNewMomentum =
              fOldMomentum - 2. * fOldMomentum * fFacetNormal * fFacetNormal;

          if (f_iTE > 0 && f_iTM > 0)
          {
            fNewPolarization =
                -fOldPolarization +
                (2. * fOldPolarization * fFacetNormal * fFacetNormal);
          }
          else if (f_iTE > 0)
          {
            A_trans = (fSint1 > 0.0) ? fOldMomentum.cross(fFacetNormal).unit()
                                     : fOldPolarization;
            fNewPolarization = -A_trans;
          }
          else if (f_iTM > 0)
          {
            fNewPolarization =
                -fNewMomentum.cross(A_trans).unit(); // = -A_paral
          }
        }
      }
      fOldMomentum = fNewMomentum;
      fOldPolarization = fNewPolarization;
    }
    // Loop checking, 13-Aug-2015, Peter Gumplinger
  } while (fNewMomentum * fGlobalNormal < 0.0);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::DielectricLUT()
{
  G4int thetaIndex, phiIndex;
  G4double angularDistVal, thetaRad, phiRad;
  G4ThreeVector perpVectorTheta, perpVectorPhi;

  fStatus = G4OpBoundaryProcessStatus(
      G4int(fFinish) + (G4int(NoRINDEX) - G4int(groundbackpainted)));

  G4int thetaIndexMax = fOpticalSurface->GetThetaIndexMax();
  G4int phiIndexMax = fOpticalSurface->GetPhiIndexMax();

  G4double rand;

  do
  {
    rand = G4UniformRand();
    if (rand > fReflectivity)
    {
      if (rand > fReflectivity + fTransmittance)
      {
        DoAbsorption();
      }
      else
      {
        fStatus = Transmission;
        fNewMomentum = fOldMomentum;
        fNewPolarization = fOldPolarization;
      }
      break;
    }
    else
    {
      // Calculate Angle between Normal and Photon Momentum
      G4double anglePhotonToNormal = fOldMomentum.angle(-fGlobalNormal);
      // Round to closest integer: LBNL model array has 91 values
      G4int angleIncident = (G4int)std::lrint(anglePhotonToNormal / CLHEP::deg);

      // Take random angles THETA and PHI,
      // and see if below Probability - if not - Redo
      do
      {
        thetaIndex = (G4int)G4RandFlat::shootInt(thetaIndexMax - 1);
        phiIndex = (G4int)G4RandFlat::shootInt(phiIndexMax - 1);
        // Find probability with the new indeces from LUT
        angularDistVal = fOpticalSurface->GetAngularDistributionValue(
            angleIncident, thetaIndex, phiIndex);
        // Loop checking, 13-Aug-2015, Peter Gumplinger
      } while (!G4BooleanRand(angularDistVal));

      thetaRad = G4double(-90 + 4 * thetaIndex) * pi / 180.;
      phiRad = G4double(-90 + 5 * phiIndex) * pi / 180.;
      // Rotate Photon Momentum in Theta, then in Phi
      fNewMomentum = -fOldMomentum;

      perpVectorTheta = fNewMomentum.cross(fGlobalNormal);
      if (perpVectorTheta.mag() < fCarTolerance)
      {
        perpVectorTheta = fNewMomentum.orthogonal();
      }
      fNewMomentum =
          fNewMomentum.rotate(anglePhotonToNormal - thetaRad, perpVectorTheta);
      perpVectorPhi = perpVectorTheta.cross(fNewMomentum);
      fNewMomentum = fNewMomentum.rotate(-phiRad, perpVectorPhi);

      // Rotate Polarization too:
      fFacetNormal = (fNewMomentum - fOldMomentum).unit();
      fNewPolarization = -fOldPolarization +
                         (2. * fOldPolarization * fFacetNormal * fFacetNormal);
    }
    // Loop checking, 13-Aug-2015, Peter Gumplinger
  } while (fNewMomentum * fGlobalNormal <= 0.0);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::DielectricLUTDAVIS()
{
  G4int angindex, random, angleIncident;
  G4double reflectivityValue, elevation, azimuth;
  G4double anglePhotonToNormal;

  G4int lutbin = fOpticalSurface->GetLUTbins();
  G4double rand = G4UniformRand();

  G4double sinEl;
  G4ThreeVector u, vNorm, w;

  do
  {
    anglePhotonToNormal = fOldMomentum.angle(-fGlobalNormal);

    // Davis model has 90 reflection bins: round down
    // don't allow angleIncident to be 90 for anglePhotonToNormal close to 90
    angleIncident = std::min(
        static_cast<G4int>(std::floor(anglePhotonToNormal / CLHEP::deg)), 89);
    reflectivityValue = fOpticalSurface->GetReflectivityLUTValue(angleIncident);

    if (rand > reflectivityValue)
    {
      if (fEfficiency > 0.)
      {
        DoAbsorption();
        break;
      }
      else
      {
        fStatus = Transmission;

        if (angleIncident <= 0.01)
        {
          fNewMomentum = fOldMomentum;
          break;
        }

        do
        {
          random = (G4int)G4RandFlat::shootInt(1, lutbin + 1);
          angindex =
              (((random * 2) - 1)) + angleIncident * lutbin * 2 + 3640000;

          azimuth =
              fOpticalSurface->GetAngularDistributionValueLUT(angindex - 1);
          elevation = fOpticalSurface->GetAngularDistributionValueLUT(angindex);
        } while (elevation == 0. && azimuth == 0.);

        sinEl = std::sin(elevation);
        vNorm = (fGlobalNormal.cross(fOldMomentum)).unit();
        u = vNorm.cross(fGlobalNormal) * (sinEl * std::cos(azimuth));
        vNorm *= (sinEl * std::sin(azimuth));
        // fGlobalNormal shouldn't be modified here
        w = (fGlobalNormal *= std::cos(elevation));
        fNewMomentum = u + vNorm + w;

        // Rotate Polarization too:
        fFacetNormal = (fNewMomentum - fOldMomentum).unit();
        fNewPolarization = -fOldPolarization + (2. * fOldPolarization *
                                                fFacetNormal * fFacetNormal);
      }
    }
    else
    {
      fStatus = LobeReflection;

      if (angleIncident == 0)
      {
        fNewMomentum = -fOldMomentum;
        break;
      }

      do
      {
        random = (G4int)G4RandFlat::shootInt(1, lutbin + 1);
        angindex = (((random * 2) - 1)) + (angleIncident - 1) * lutbin * 2;

        azimuth = fOpticalSurface->GetAngularDistributionValueLUT(angindex - 1);
        elevation = fOpticalSurface->GetAngularDistributionValueLUT(angindex);
      } while (elevation == 0. && azimuth == 0.);

      sinEl = std::sin(elevation);
      vNorm = (fGlobalNormal.cross(fOldMomentum)).unit();
      u = vNorm.cross(fGlobalNormal) * (sinEl * std::cos(azimuth));
      vNorm *= (sinEl * std::sin(azimuth));
      // fGlobalNormal shouldn't be modified here
      w = (fGlobalNormal *= std::cos(elevation));

      fNewMomentum = u + vNorm + w;

      // Rotate Polarization too: (needs revision)
      fNewPolarization = fOldPolarization;
    }
  } while (fNewMomentum * fGlobalNormal <= 0.0);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::DielectricDichroic()
{
  // Calculate Angle between Normal and Photon Momentum
  G4double anglePhotonToNormal = fOldMomentum.angle(-fGlobalNormal);

  // Round it to closest integer
  G4double angleIncident = std::floor(180. / pi * anglePhotonToNormal + 0.5);

  if (!fDichroicVector)
  {
    if (fOpticalSurface)
      fDichroicVector = fOpticalSurface->GetDichroicVector();
  }

  if (fDichroicVector)
  {
    G4double wavelength = h_Planck * c_light / fPhotonMomentum;
    fTransmittance = fDichroicVector->Value(wavelength / nm, angleIncident,
                                            idx_dichroicX, idx_dichroicY) *
                     perCent;
    //   G4cout << "wavelength: " << std::floor(wavelength/nm)
    //                            << "nm" << G4endl;
    //   G4cout << "Incident angle: " << angleIncident << "deg" << G4endl;
    //   G4cout << "Transmittance: "
    //          << std::floor(fTransmittance/perCent) << "%" << G4endl;
  }
  else
  {
    G4ExceptionDescription ed;
    ed << " G4OpBoundaryProcess/DielectricDichroic(): "
       << " The dichroic surface has no G4Physics2DVector" << G4endl;
    G4Exception("G4OpBoundaryProcess::DielectricDichroic", "OpBoun03",
                FatalException, ed,
                "A dichroic surface must have an associated G4Physics2DVector");
  }

  if (!G4BooleanRand(fTransmittance))
  { // Not transmitted, so reflect
    if (fModel == glisur || fFinish == polished)
    {
      DoReflection();
    }
    else
    {
      ChooseReflection();
      if (fStatus == LambertianReflection)
      {
        DoReflection();
      }
      else if (fStatus == BackScattering)
      {
        fNewMomentum = -fOldMomentum;
        fNewPolarization = -fOldPolarization;
      }
      else
      {
        G4double PdotN, EdotN;
        do
        {
          if (fStatus == LobeReflection)
          {
            fFacetNormal = GetFacetNormal(fOldMomentum, fGlobalNormal);
          }
          PdotN = fOldMomentum * fFacetNormal;
          fNewMomentum = fOldMomentum - (2. * PdotN) * fFacetNormal;
          // Loop checking, 13-Aug-2015, Peter Gumplinger
        } while (fNewMomentum * fGlobalNormal <= 0.0);

        EdotN = fOldPolarization * fFacetNormal;
        fNewPolarization = -fOldPolarization + (2. * EdotN) * fFacetNormal;
      }
    }
  }
  else
  {
    fStatus = Dichroic;
    fNewMomentum = fOldMomentum;
    fNewPolarization = fOldPolarization;
  }
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::DielectricDielectric()
{
  G4bool inside = false;
  G4bool swap = false;

  if (fFinish == polished)
  {
    fFacetNormal = fGlobalNormal;
  }
  else
  {
    fFacetNormal = GetFacetNormal(fOldMomentum, fGlobalNormal);
  }
  G4double cost1 = -fOldMomentum * fFacetNormal;
  G4double cost2 = 0.;
  G4double sint2 = 0.;

  G4bool surfaceRoughnessCriterionPass = true;
  if (fSurfaceRoughness != 0. && fRindex1 > fRindex2)
  {
    G4double wavelength = h_Planck * c_light / fPhotonMomentum;
    G4double surfaceRoughnessCriterion = std::exp(-std::pow(
        (4. * pi * fSurfaceRoughness * fRindex1 * cost1 / wavelength), 2));
    surfaceRoughnessCriterionPass = G4BooleanRand(surfaceRoughnessCriterion);
    // Calculates a surface roughness criterion based on the wavelength and compares it using a random number to determine if it passes
  }

leap:

  G4bool through = false;
  G4bool done = false;

  G4ThreeVector A_trans, A_paral, E1pp, E1pl;
  G4double E1_perp, E1_parl;
  G4double s1, s2, E2_perp, E2_parl, E2_total, transCoeff;
  G4double E2_abs, C_parl, C_perp;
  G4double alpha;

  do
  {
    if (through)
    {
      swap = !swap;
      through = false;
      fGlobalNormal = -fGlobalNormal;
      G4SwapPtr(fMaterial1, fMaterial2);
      G4SwapObj(&fRindex1, &fRindex2);
    }

    if (fFinish == polished)
    {
      fFacetNormal = fGlobalNormal;
    }
    else
    {
      fFacetNormal = GetFacetNormal(fOldMomentum, fGlobalNormal);
    }

    cost1 = -fOldMomentum * fFacetNormal;
    if (std::abs(cost1) < 1.0 - fCarTolerance)
    {
      fSint1 = std::sqrt(1. - cost1 * cost1);
      sint2 = fSint1 * fRindex1 / fRindex2; // *** Snell's Law ***
      // this isn't a sine as we might expect from the name; can be > 1 !!!
    }
    else
    {
      fSint1 = 0.0;
      sint2 = 0.0;
    }

    // TOTAL INTERNAL REFLECTION
    if (sint2 >= 1.0)
    {
      swap = false;

      fStatus = TotalInternalReflection;
      if (!surfaceRoughnessCriterionPass)
        fStatus = LambertianReflection;
      if (fModel == unified && fFinish != polished)
        ChooseReflection();
      if (fStatus == LambertianReflection)
      {
        DoReflection();
      }
      else if (fStatus == BackScattering)
      {
        fNewMomentum = -fOldMomentum;
        fNewPolarization = -fOldPolarization;
      }
      else
      {
        fNewMomentum =
            fOldMomentum - 2. * fOldMomentum * fFacetNormal * fFacetNormal;
        fNewPolarization = -fOldPolarization + (2. * fOldPolarization *
                                                fFacetNormal * fFacetNormal);
      }
    }
    // NOT TIR
    else if (sint2 < 1.0)
    {
      // Calculate amplitude for transmission (Q = P x N)
      if (cost1 > 0.0)
      {
        cost2 = std::sqrt(1. - sint2 * sint2);
      }
      else
      {
        cost2 = -std::sqrt(1. - sint2 * sint2);
      }

      if (fSint1 > 0.0)
      {
        A_trans = (fOldMomentum.cross(fFacetNormal)).unit();
        E1_perp = fOldPolarization * A_trans;
        E1pp = E1_perp * A_trans;
        E1pl = fOldPolarization - E1pp;
        E1_parl = E1pl.mag();
      }
      else
      {
        A_trans = fOldPolarization;
        // Here we Follow Jackson's conventions and set the parallel
        // component = 1 in case of a ray perpendicular to the surface
        E1_perp = 0.0;
        E1_parl = 1.0;
      }

      s1 = fRindex1 * cost1;
      E2_perp = 2. * s1 * E1_perp / (fRindex1 * cost1 + fRindex2 * cost2);
      E2_parl = 2. * s1 * E1_parl / (fRindex2 * cost1 + fRindex1 * cost2);
      E2_total = E2_perp * E2_perp + E2_parl * E2_parl;
      s2 = fRindex2 * cost2 * E2_total;

      if (fTransmittance > 0.)
        transCoeff = fTransmittance; // Determines whether the photon reflects or transmits based on a transmission coefficient
      else if (cost1 != 0.0)
        transCoeff = s2 / s1;
      else
        transCoeff = 0.0;

      // NOT TIR: REFLECTION
      if (!G4BooleanRand(transCoeff))
      {
        swap = false;
        fStatus = FresnelReflection;

        if (!surfaceRoughnessCriterionPass)
          fStatus = LambertianReflection;
        if (fModel == unified && fFinish != polished)
          ChooseReflection();
        if (fStatus == LambertianReflection)
        {
          DoReflection();
        }
        else if (fStatus == BackScattering)
        {
          fNewMomentum = -fOldMomentum;
          fNewPolarization = -fOldPolarization;
        }
        else
        {
          fNewMomentum =
              fOldMomentum - 2. * fOldMomentum * fFacetNormal * fFacetNormal;
          if (fSint1 > 0.0)
          { // incident ray oblique
            E2_parl = fRindex2 * E2_parl / fRindex1 - E1_parl;
            E2_perp = E2_perp - E1_perp;
            E2_total = E2_perp * E2_perp + E2_parl * E2_parl;
            A_paral = (fNewMomentum.cross(A_trans)).unit();
            E2_abs = std::sqrt(E2_total);
            C_parl = E2_parl / E2_abs;
            C_perp = E2_perp / E2_abs;

            fNewPolarization = C_parl * A_paral + C_perp * A_trans;
          }
          else
          { // incident ray perpendicular
            if (fRindex2 > fRindex1)
            {
              fNewPolarization = -fOldPolarization;
            }
            else
            {
              fNewPolarization = fOldPolarization;
            }
          }
        }
      }
      // NOT TIR: TRANSMISSION
      else
      {
        inside = !inside;
        through = true;
        fStatus = FresnelRefraction;

        if (fSint1 > 0.0)
        { // incident ray oblique
          alpha = cost1 - cost2 * (fRindex2 / fRindex1);
          fNewMomentum = (fOldMomentum + alpha * fFacetNormal).unit();
          A_paral = (fNewMomentum.cross(A_trans)).unit();
          E2_abs = std::sqrt(E2_total);
          C_parl = E2_parl / E2_abs;
          C_perp = E2_perp / E2_abs;

          fNewPolarization = C_parl * A_paral + C_perp * A_trans;
        }
        else
        { // incident ray perpendicular
          fNewMomentum = fOldMomentum;
          fNewPolarization = fOldPolarization; // Updates the momentum and polarization vectors
        }
      }
    }

    fOldMomentum = fNewMomentum.unit();
    fOldPolarization = fNewPolarization.unit();

    if (fStatus == FresnelRefraction)
    {
      done = (fNewMomentum * fGlobalNormal <= 0.0); // Checks whether to terminate the loop based on the direction of the new momentum vector
    }
    else
    {
      done = (fNewMomentum * fGlobalNormal >= -fCarTolerance);
    }
    // Loop checking, 13-Aug-2015, Peter Gumplinger
  } while (!done);

  if (inside && !swap)
  {
    if (fFinish == polishedbackpainted || fFinish == groundbackpainted)
    {
      G4double rand = G4UniformRand();
      if (rand > fReflectivity + fTransmittance)
      {
        DoAbsorption();
      }
      else if (rand > fReflectivity)
      {
        fStatus = Transmission;
        fNewMomentum = fOldMomentum;
        fNewPolarization = fOldPolarization;
      }
      else
      {
        if (fStatus != FresnelRefraction)
        {
          fGlobalNormal = -fGlobalNormal;
        }
        else
        {
          swap = !swap;
          G4SwapPtr(fMaterial1, fMaterial2);
          G4SwapObj(&fRindex1, &fRindex2);
        }
        if (fFinish == groundbackpainted)
          fStatus = LambertianReflection;

        DoReflection();

        fGlobalNormal = -fGlobalNormal;
        fOldMomentum = fNewMomentum;

        goto leap;
      }
    }
  }
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
G4double G4OpBoundaryProcess::GetMeanFreePath(const G4Track &, G4double,
                                              G4ForceCondition *condition)
{
  *condition = Forced;
  return DBL_MAX;
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
G4double G4OpBoundaryProcess::GetIncidentAngle()
{

  return pi - std::acos(fOldMomentum * fFacetNormal /
                        (fOldMomentum.mag() * fFacetNormal.mag()));
}
/*calculates the incident angle of the photon by taking the arccosine of the dot product of the old momentum and the surface normal,
 and then subtracting this angle from pi.
 The result is the incident angle in radians
*/

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
G4double G4OpBoundaryProcess::GetReflectivity(G4double E1_perp,
                                              G4double E1_parl,
                                              G4double incidentangle,
                                              G4double realRindex,
                                              G4double imaginaryRindex)
{
  G4complex reflectivity, reflectivity_TE, reflectivity_TM;
  G4complex N1(fRindex1, 0.), N2(realRindex, imaginaryRindex);
  G4complex cosPhi;

  G4complex u(1., 0.); // unit number 1

  G4complex numeratorTE; // E1_perp=1 E1_parl=0 -> TE polarization
  G4complex numeratorTM; // E1_parl=1 E1_perp=0 -> TM polarization
  G4complex denominatorTE, denominatorTM;
  G4complex rTM, rTE;

  G4MaterialPropertiesTable *MPT = fMaterial1->GetMaterialPropertiesTable();
  G4MaterialPropertyVector *ppR = MPT->GetProperty(kREALRINDEX);
  G4MaterialPropertyVector *ppI = MPT->GetProperty(kIMAGINARYRINDEX);

  if (ppR && ppI)
  {
    G4double rRindex = ppR->Value(fPhotonMomentum, idx_rrindex);
    G4double iRindex = ppI->Value(fPhotonMomentum, idx_irindex);
    N1 = G4complex(rRindex, iRindex);
  }

  // Following two equations, rTM and rTE, are from: "Introduction To Modern
  // Optics" written by Fowles
  cosPhi = std::sqrt(u - ((std::sin(incidentangle) * std::sin(incidentangle)) *
                          (N1 * N1) / (N2 * N2)));

  numeratorTE = N1 * std::cos(incidentangle) - N2 * cosPhi;
  denominatorTE = N1 * std::cos(incidentangle) + N2 * cosPhi;
  rTE = numeratorTE / denominatorTE;

  numeratorTM = N2 * std::cos(incidentangle) - N1 * cosPhi;
  denominatorTM = N2 * std::cos(incidentangle) + N1 * cosPhi;
  rTM = numeratorTM / denominatorTM;

  // This is my (PG) calculaton for reflectivity on a metallic surface
  // depending on the fraction of TE and TM polarization
  // when TE polarization, E1_parl=0 and E1_perp=1, R=abs(rTE)^2 and
  // when TM polarization, E1_parl=1 and E1_perp=0, R=abs(rTM)^2

  reflectivity_TE = (rTE * conj(rTE)) * (E1_perp * E1_perp) /
                    (E1_perp * E1_perp + E1_parl * E1_parl);
  reflectivity_TM = (rTM * conj(rTM)) * (E1_parl * E1_parl) /
                    (E1_perp * E1_perp + E1_parl * E1_parl);
  reflectivity = reflectivity_TE + reflectivity_TM; // why not *0.5 here?!?

  do
  {
    if (G4UniformRand() * real(reflectivity) > real(reflectivity_TE))
    {
      f_iTE = -1;
    }
    else
    {
      f_iTE = 1;
    }
    if (G4UniformRand() * real(reflectivity) > real(reflectivity_TM))
    {
      f_iTM = -1;
    }
    else
    {
      f_iTM = 1;
    }
    // Loop checking, 13-Aug-2015, Peter Gumplinger
  } while (f_iTE < 0 && f_iTM < 0);

  return real(reflectivity);
}
/*
function models the reflection behavior of light at a surface,
considering both TE and TM polarizations and randomizing the polarization state
based on the calculated reflectivities
*/

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::CalculateReflectivity()
{
  G4double realRindex = fRealRIndexMPV->Value(fPhotonMomentum, idx_rrindex);
  G4double imaginaryRindex =
      fImagRIndexMPV->Value(fPhotonMomentum, idx_irindex);

  // calculate FacetNormal
  if (fFinish == ground)
  {
    fFacetNormal = GetFacetNormal(fOldMomentum, fGlobalNormal);
  }
  else
  {
    fFacetNormal = fGlobalNormal;
  }

  G4double cost1 = -fOldMomentum * fFacetNormal;
  if (std::abs(cost1) < 1.0 - fCarTolerance)
  {
    fSint1 = std::sqrt(1. - cost1 * cost1);
  }
  else
  {
    fSint1 = 0.0;
  }

  G4ThreeVector A_trans, A_paral, E1pp, E1pl;
  G4double E1_perp, E1_parl;

  if (fSint1 > 0.0)
  {
    A_trans = (fOldMomentum.cross(fFacetNormal)).unit();
    E1_perp = fOldPolarization * A_trans;
    E1pp = E1_perp * A_trans;
    E1pl = fOldPolarization - E1pp;
    E1_parl = E1pl.mag();
  }
  else
  {
    A_trans = fOldPolarization;
    // Here we Follow Jackson's conventions and we set the parallel
    // component = 1 in case of a ray perpendicular to the surface
    E1_perp = 0.0;
    E1_parl = 1.0;
  }

  G4double incidentangle = GetIncidentAngle();

  // calculate the reflectivity using the precious function depending on incident angle,
  // polarization and complex refractive
  fReflectivity = GetReflectivity(E1_perp, E1_parl, incidentangle, realRindex,
                                  imaginaryRindex);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
G4bool G4OpBoundaryProcess::InvokeSD(const G4Step *pStep)
{

  G4Step aStep = *pStep;
  aStep.AddTotalEnergyDeposit(fPhotonMomentum); // relevant for tracking the energy deposited by the photon in a sensitive detector

  G4VSensitiveDetector *sd = aStep.GetPostStepPoint()->GetSensitiveDetector();
  if (sd != nullptr)
    return sd->Hit(&aStep); // notifying the sensitive detector that a hit has occurred
  else
    return false;
}
// The function returns true if a sensitive detector was found and the Hit method was invoked successfully. Otherwise, it returns false

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
inline void G4OpBoundaryProcess::SetInvokeSD(G4bool flag)
{
  fInvokeSD = flag;
  G4OpticalParameters::Instance()->SetBoundaryInvokeSD(fInvokeSD);
}
// provides a way to toggle the behavior of invoking or not invoking the sensitive detector based on the value passed as a parameter

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::SetVerboseLevel(G4int verbose)
{
  verboseLevel = verbose; // The verboseLevel variable determines the level of detail for diagnostic and debug output during the simulation
  G4OpticalParameters::Instance()->SetBoundaryVerboseLevel(verboseLevel);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::CoatedDielectricDielectric()
{
  G4MaterialPropertyVector *pp = nullptr;

  G4MaterialPropertiesTable *MPT = fMaterial2->GetMaterialPropertiesTable();
  if ((pp = MPT->GetProperty(kRINDEX)))
  {
    fRindex2 = pp->Value(fPhotonMomentum, idx_rindex2);
  }

  MPT = fOpticalSurface->GetMaterialPropertiesTable();
  if ((pp = MPT->GetProperty(kRINDEX)))
  {
    fCoatedRindex = pp->Value(fPhotonMomentum, idx_coatedrindex);
  }
  if (MPT->ConstPropertyExists(kCOATEDTHICKNESS))
  {
    fCoatedThickness = MPT->GetConstProperty(kCOATEDTHICKNESS);
  }
  if (MPT->ConstPropertyExists(kCOATEDFRUSTRATEDTRANSMISSION))
  {
    fCoatedFrustratedTransmission =
        (G4bool)MPT->GetConstProperty(kCOATEDFRUSTRATEDTRANSMISSION);
  }

  G4double sintTL;
  G4double wavelength = h_Planck * c_light / fPhotonMomentum;
  G4double PdotN;
  G4double E1_perp, E1_parl;
  G4double s1, E2_perp, E2_parl, E2_total, transCoeff;
  G4double E2_abs, C_parl, C_perp;
  G4double alpha;
  G4ThreeVector A_trans, A_paral, E1pp, E1pl;
  // G4bool Inside  = false;
  // G4bool Swap    = false;
  G4bool through = false;
  G4bool done = false;

  do
  {
    if (through)
    {
      // Swap = !Swap;
      through = false;
      fGlobalNormal = -fGlobalNormal;
      G4SwapPtr(fMaterial1, fMaterial2);
      G4SwapObj(&fRindex1, &fRindex2);
    }

    if (fFinish == polished)
    {
      fFacetNormal = fGlobalNormal;
    }
    else
    {
      fFacetNormal = GetFacetNormal(fOldMomentum, fGlobalNormal);
    }

    PdotN = fOldMomentum * fFacetNormal;
    G4double cost1 = -PdotN;
    G4double sint2, cost2 = 0.;

    if (std::abs(cost1) < 1.0 - fCarTolerance)
    {
      fSint1 = std::sqrt(1. - cost1 * cost1);
      sint2 = fSint1 * fRindex1 / fRindex2;
      sintTL = fSint1 * fRindex1 / fCoatedRindex;
    }
    else
    {
      fSint1 = 0.0;
      sint2 = 0.0;
      sintTL = 0.0;
    }

    if (fSint1 > 0.0)
    {
      A_trans = fOldMomentum.cross(fFacetNormal);
      A_trans = A_trans.unit();
      E1_perp = fOldPolarization * A_trans;
      E1pp = E1_perp * A_trans;
      E1pl = fOldPolarization - E1pp;
      E1_parl = E1pl.mag();
    }
    else
    {
      A_trans = fOldPolarization;
      E1_perp = 0.0;
      E1_parl = 1.0;
    }

    s1 = fRindex1 * cost1;

    if (cost1 > 0.0)
    {
      cost2 = std::sqrt(1. - sint2 * sint2);
    }
    else
    {
      cost2 = -std::sqrt(1. - sint2 * sint2);
    }

    transCoeff = 0.0;

    if (sintTL >= 1.0) // --> Angle > Angle Limit
    {
      // Swap = false;
    }
    E2_perp = 2. * s1 * E1_perp / (fRindex1 * cost1 + fRindex2 * cost2);
    E2_parl = 2. * s1 * E1_parl / (fRindex2 * cost1 + fRindex1 * cost2);
    E2_total = E2_perp * E2_perp + E2_parl * E2_parl;

    transCoeff = 1. - GetReflectivityThroughThinLayer(
                          sintTL, E1_perp, E1_parl, wavelength, cost1, cost2);

    if (!G4BooleanRand(transCoeff))
    {
      if (verboseLevel > 2)
        G4cout << "Reflection from " << fMaterial1->GetName() << " to "
               << fMaterial2->GetName() << G4endl;

      // Swap = false;

      if (sintTL >= 1.0)
      {
        fStatus = TotalInternalReflection;
      }
      else
      {
        fStatus = CoatedDielectricReflection;
      }

      PdotN = fOldMomentum * fFacetNormal;
      fNewMomentum = fOldMomentum - (2. * PdotN) * fFacetNormal;

      if (fSint1 > 0.0)
      { // incident ray oblique

        E2_parl = fRindex2 * E2_parl / fRindex1 - E1_parl;
        E2_perp = E2_perp - E1_perp;
        E2_total = E2_perp * E2_perp + E2_parl * E2_parl;
        A_paral = fNewMomentum.cross(A_trans);
        A_paral = A_paral.unit();
        E2_abs = std::sqrt(E2_total);
        C_parl = E2_parl / E2_abs;
        C_perp = E2_perp / E2_abs;

        fNewPolarization = C_parl * A_paral + C_perp * A_trans;
      }
      else
      { // incident ray perpendicular
        if (fRindex2 > fRindex1)
        {
          fNewPolarization = -fOldPolarization;
        }
        else
        {
          fNewPolarization = fOldPolarization;
        }
      }
    }
    else
    { // photon gets transmitted
      if (verboseLevel > 2)
        G4cout << "Transmission from " << fMaterial1->GetName() << " to "
               << fMaterial2->GetName() << G4endl;

      // Inside = !Inside;
      through = true;

      if (fEfficiency > 0.)
      {
        DoAbsorption();
        return;
      }
      else
      {
        if (sintTL >= 1.0)
        {
          fStatus = CoatedDielectricFrustratedTransmission;
        }
        else
        {
          fStatus = CoatedDielectricRefraction;
        }

        if (fSint1 > 0.0)
        { // incident ray oblique

          alpha = cost1 - cost2 * (fRindex2 / fRindex1);
          fNewMomentum = fOldMomentum + alpha * fFacetNormal;
          fNewMomentum = fNewMomentum.unit();
          A_paral = fNewMomentum.cross(A_trans);
          A_paral = A_paral.unit();
          E2_abs = std::sqrt(E2_total);
          C_parl = E2_parl / E2_abs;
          C_perp = E2_perp / E2_abs;

          fNewPolarization = C_parl * A_paral + C_perp * A_trans;
        }
        else
        { // incident ray perpendicular
          fNewMomentum = fOldMomentum;
          fNewPolarization = fOldPolarization;
        }
      }
    }

    fOldMomentum = fNewMomentum.unit();
    fOldPolarization = fNewPolarization.unit();
    if ((fStatus == CoatedDielectricFrustratedTransmission) ||
        (fStatus == CoatedDielectricRefraction))
    {
      done = (fNewMomentum * fGlobalNormal <= 0.0);
    }
    else
    {
      done = (fNewMomentum * fGlobalNormal >= -fCarTolerance);
    }

  } while (!done);
}

G4double G4OpBoundaryProcess::GetReflectivityThroughThinLayer(G4double sinTL,
                                                              G4double E1_perp,
                                                              G4double E1_parl,
                                                              G4double wavelength, G4double cost1, G4double cost2)
{
  G4complex Reflectivity, Reflectivity_TE, Reflectivity_TM;
  G4double gammaTL, costTL;

  G4complex i(0, 1);
  G4complex rTM, rTE;
  G4complex r1toTL, rTLto2;
  G4double k0 = 2 * pi / wavelength;

  // Angle > Angle limit
  if (sinTL >= 1.0)
  {
    if (fCoatedFrustratedTransmission)
    { // Frustrated transmission

      if (cost1 > 0.0)
      {
        gammaTL = std::sqrt(fRindex1 * fRindex1 * fSint1 * fSint1 -
                            fCoatedRindex * fCoatedRindex);
      }
      else
      {
        gammaTL = -std::sqrt(fRindex1 * fRindex1 * fSint1 * fSint1 -
                             fCoatedRindex * fCoatedRindex);
      }

      // TE
      r1toTL = (fRindex1 * cost1 - i * gammaTL) / (fRindex1 * cost1 + i * gammaTL);
      rTLto2 = (i * gammaTL - fRindex2 * cost2) / (i * gammaTL + fRindex2 * cost2);
      if (cost1 != 0.0)
      {
        rTE = (r1toTL + rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL)) /
              (1.0 + r1toTL * rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL));
      }
      // TM
      r1toTL = (fRindex1 * i * gammaTL - fCoatedRindex * fCoatedRindex * cost1) /
               (fRindex1 * i * gammaTL + fCoatedRindex * fCoatedRindex * cost1);
      rTLto2 = (fCoatedRindex * fCoatedRindex * cost2 - fRindex2 * i * gammaTL) /
               (fCoatedRindex * fCoatedRindex * cost2 + fRindex2 * i * gammaTL);
      if (cost1 != 0.0)
      {
        rTM = (r1toTL + rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL)) /
              (1.0 + r1toTL * rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL));
      }
    }
    else
    { // Total reflection
      return (1.);
    }
  }

  // Angle <= Angle limit
  else // if (sinTL < 1.0)
  {
    if (cost1 > 0.0)
    {
      costTL = std::sqrt(1. - sinTL * sinTL);
    }
    else
    {
      costTL = -std::sqrt(1. - sinTL * sinTL);
    }
    // TE
    r1toTL = (fRindex1 * cost1 - fCoatedRindex * costTL) / (fRindex1 * cost1 + fCoatedRindex * costTL);
    rTLto2 = (fCoatedRindex * costTL - fRindex2 * cost2) / (fCoatedRindex * costTL + fRindex2 * cost2);
    if (cost1 != 0.0)
    {
      rTE = (r1toTL + rTLto2 * std::exp(2.0 * i * k0 * fCoatedRindex * fCoatedThickness * costTL)) /
            (1.0 + r1toTL * rTLto2 * std::exp(2.0 * i * k0 * fCoatedRindex * fCoatedThickness * costTL));
    }
    // TM
    r1toTL = (fRindex1 * costTL - fCoatedRindex * cost1) / (fRindex1 * costTL + fCoatedRindex * cost1);
    rTLto2 = (fCoatedRindex * cost2 - fRindex2 * costTL) / (fCoatedRindex * cost2 + fRindex2 * costTL);
    if (cost1 != 0.0)
    {
      rTM = (r1toTL + rTLto2 * std::exp(2.0 * i * k0 * fCoatedRindex * fCoatedThickness * costTL)) /
            (1.0 + r1toTL * rTLto2 * std::exp(2.0 * i * k0 * fCoatedRindex * fCoatedThickness * costTL));
    }
  }

  Reflectivity_TE = (rTE * conj(rTE)) * (E1_perp * E1_perp) / (E1_perp * E1_perp + E1_parl * E1_parl);
  Reflectivity_TM = (rTM * conj(rTM)) * (E1_parl * E1_parl) / (E1_perp * E1_perp + E1_parl * E1_parl);
  Reflectivity = Reflectivity_TE + Reflectivity_TM;

  return real(Reflectivity);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void G4OpBoundaryProcess::PhotocathodeCoated()
{
  log_info("Material1 = {}, Material2 = {}", fMaterial1->GetName(), fMaterial2->GetName());

  // replace with "PhotocathodeLog"-Volume later
  G4Material *lPhotocathodeMaterial = G4Material::GetMaterial("RiAbs_NicoPalladium");

  G4MaterialPropertyVector *rIndexMPV2 = nullptr;
  G4MaterialPropertyVector *absLengthMPV2 = nullptr;
  G4MaterialPropertiesTable *lMPT_photocathode = lPhotocathodeMaterial->GetMaterialPropertiesTable();
  G4MaterialPropertiesTable *MPT2 = fMaterial2->GetMaterialPropertiesTable();

  if (MPT2 != nullptr)
  {
    rIndexMPV2 = MPT2->GetProperty(kRINDEX);
  }
  if (rIndexMPV2 != nullptr)
  {
    fRindex2 = rIndexMPV2->Value(fPhotonMomentum, idx_rindex2);
  }
  if (MPT2 != nullptr)
  {
    absLengthMPV2 = MPT2->GetProperty(kABSLENGTH);

    if (absLengthMPV2 != nullptr)
    { // Checking if "kABSLENGTH" property is defined for this material.
      fAbsorptionLength2 = absLengthMPV2->Value(fPhotonMomentum, idx_abslength2);
    }
  }

  G4MaterialPropertiesTable *MPTCoated = fOpticalSurface->GetMaterialPropertiesTable();
  if (MPTCoated)
  {
    G4MaterialPropertyVector *ppCoated = nullptr;

    if (ppCoated = MPTCoated->GetProperty(kRINDEX))
    {
      fCoatedRindex = ppCoated->Value(fPhotonMomentum, idx_coatedrindex);
    }

    if (ppCoated = MPTCoated->GetProperty(kABSLENGTH))
    {
      fCoatedAbsLength = ppCoated->Value(fPhotonMomentum, idx_coatedabslength);
    }

    if (MPTCoated->ConstPropertyExists(kCOATEDTHICKNESS))
    {
      fCoatedThickness = MPTCoated->GetConstProperty(kCOATEDTHICKNESS);
    }

    if (MPTCoated->ConstPropertyExists(kCOATEDFRUSTRATEDTRANSMISSION))
    {
      fCoatedFrustratedTransmission = MPTCoated->GetConstProperty(kCOATEDFRUSTRATEDTRANSMISSION);
    }
  }

  G4double sintTL;
  G4double wavelength = h_Planck * c_light / fPhotonMomentum;
  G4double PdotN;
  G4double E1_perp, E1_parl;
  G4double s1, E2_perp, E2_parl, E2_total, transCoeff, refCoeff, absCoeff;
  G4double E2_abs, C_parl, C_perp;
  G4double alpha;
  G4ThreeVector A_trans, A_paral, E1pp, E1pl;
  // G4bool Inside  = false;
  // G4bool Swap    = false;
  G4bool through = false;
  G4bool done = false;

  // convert absorption length to complex refractive index: k(λ) = λ/(4pi*abslength(λ)) for abslength != 0 (which would correspont to "undefined")
  if (fAbsorptionLength1 != 0.)
  {
    fImagRIndex1 = wavelength / (4 * pi * fAbsorptionLength1);
  }
  else
  {
    fImagRIndex1 = 0.;
  }
  if (fAbsorptionLength2 != 0.)
  {
    fImagRIndex2 = wavelength / (4 * pi * fAbsorptionLength2);
  }
  else
  {
    fImagRIndex1 = 0.;
  }
  if (fCoatedAbsLength != 0.)
  {
    fCoatedImagRIndex = wavelength / (4 * pi * fCoatedAbsLength);
  }
  else
  {
    fImagRIndex1 = 0.;
  }

  // log_critical("fAbsorptionLength1 = {}, fAbsorptionLength2 = {}, fCoatedAbsLength = {}", fAbsorptionLength1, fAbsorptionLength2, fCoatedAbsLength);
  // log_critical("fImagRIndex1 = {}, fImagRIndex2 = {}, fCoatedImagRIndex = {}", fImagRIndex1, fImagRIndex2, fCoatedImagRIndex);

  do
  {
    if (through)
    {
      // Swap = !Swap;
      through = false;
      fGlobalNormal = -fGlobalNormal;
      G4SwapPtr(fMaterial1, fMaterial2);
      G4SwapObj(&fRindex1, &fRindex2);
    }

    if (fFinish == polished)
    {
      fFacetNormal = fGlobalNormal;
    }
    else
    {
      fFacetNormal = GetFacetNormal(fOldMomentum, fGlobalNormal);
    }

    PdotN = fOldMomentum * fFacetNormal;
    G4double cost1 = -PdotN;
    G4double sint2, cost2 = 0.;

    if (std::abs(cost1) < 1.0 - fCarTolerance)
    {
      fSint1 = std::sqrt(1. - cost1 * cost1);
      sint2 = fSint1 * fRindex1 / fRindex2; // Snell's Law
      sintTL = fSint1 * fRindex1 / fCoatedRindex;
    }
    else // If cost1 is close to 1.0, indicating total internal reflection, some values are set to zero
    {
      fSint1 = 0.0;
      sint2 = 0.0;
      sintTL = 0.0;
    }

    if (fSint1 > 0.0) // incident ray is oblique
    {
      A_trans = fOldMomentum.cross(fFacetNormal);
      A_trans = A_trans.unit();
      E1_perp = fOldPolarization * A_trans;
      E1pp = E1_perp * A_trans;
      E1pl = fOldPolarization - E1pp;
      E1_parl = E1pl.mag();
    }
    else
    {
      A_trans = fOldPolarization;
      E1_perp = 0.0;
      E1_parl = 1.0;
    }

    s1 = fRindex1 * cost1;

    if (cost1 > 0.0)
    {
      cost2 = std::sqrt(1. - sint2 * sint2);
    }
    else
    {
      cost2 = -std::sqrt(1. - sint2 * sint2);
    }

    transCoeff = 0.0;

    if (sintTL >= 1.0) // Check for total internal reflection --> Angle > Angle Limit
    {
      // Swap = false;
    }
    E2_perp = 2. * s1 * E1_perp / (fRindex1 * cost1 + fRindex2 * cost2);
    E2_parl = 2. * s1 * E1_parl / (fRindex2 * cost1 + fRindex1 * cost2);
    E2_total = E2_perp * E2_perp + E2_parl * E2_parl;

    log_critical("Start GetFresnelThroughThinLayer");

    OpticalLayerResult lResults = GetFresnelThroughThinLayer(sintTL, E1_perp, E1_parl, wavelength, cost1, cost2);
    refCoeff = lResults.Reflectivity;
    transCoeff = lResults.Transmittance;
    absCoeff = lResults.Absorption;
    log_info("refCoeff = {}", refCoeff);
    log_info("transCoeff = {}", transCoeff);
    log_info("absCoeff = {}", absCoeff);

    // now check if the photon is reflected, transmitted or absorbed by creating a random number between 0 and 1

    G4double random = G4UniformRand(); // Generate a random number between 0 and 1

    if (random < (refCoeff))
    { // Photon is reflected

      log_critical("Photon is reflected");

      if (verboseLevel > 2)
        G4cout << "Reflection from " << fMaterial1->GetName() << " to "
               << fMaterial2->GetName() << G4endl;
      // If the random number generated is below the transmission coefficient, it means the photon is reflected.

      // Swap = false;

      if (sintTL >= 1.0)
      {
        fStatus = TotalInternalReflection;
      }
      else
      {
        fStatus = CoatedDielectricReflection;
      }

      PdotN = fOldMomentum * fFacetNormal;
      fNewMomentum = fOldMomentum - (2. * PdotN) * fFacetNormal;
      // The status is set to CoatedDielectricReflection, and the new momentum and polarization are determined for reflection

      if (fSint1 > 0.0)
      { // incident ray oblique

        E2_parl = fRindex2 * E2_parl / fRindex1 - E1_parl;
        E2_perp = E2_perp - E1_perp;
        E2_total = E2_perp * E2_perp + E2_parl * E2_parl;
        A_paral = fNewMomentum.cross(A_trans);
        A_paral = A_paral.unit();
        E2_abs = std::sqrt(E2_total);
        C_parl = E2_parl / E2_abs;
        C_perp = E2_perp / E2_abs;

        fNewPolarization = C_parl * A_paral + C_perp * A_trans;
      }
      else
      { // incident ray perpendicular
        if (fRindex2 > fRindex1)
        {
          fNewPolarization = -fOldPolarization;
        }
        else
        {
          fNewPolarization = fOldPolarization;
        }
      }
    }
    else if (random < transCoeff+refCoeff)
    { // Photon is transmitted

      log_critical("Photon is transmitted");

      if (verboseLevel > 2)
        G4cout << "Transmission from " << fMaterial1->GetName() << " to "
               << fMaterial2->GetName() << G4endl;

      // Inside = !Inside;
      through = true;

      /* if (fEfficiency > 0.) //###### Add  new Absoption here #####
      {
        DoAbsorption();
        return;
      }
      else
      { */
      if (sintTL >= 1.0)
      {
        fStatus = CoatedDielectricFrustratedTransmission;
      }
      else
      {
        fStatus = CoatedDielectricRefraction;
      }

      if (fSint1 > 0.0)
      { // incident ray oblique

        alpha = cost1 - cost2 * (fRindex2 / fRindex1);
        fNewMomentum = fOldMomentum + alpha * fFacetNormal;
        fNewMomentum = fNewMomentum.unit();
        A_paral = fNewMomentum.cross(A_trans);
        A_paral = A_paral.unit();
        E2_abs = std::sqrt(E2_total);
        C_parl = E2_parl / E2_abs;
        C_perp = E2_perp / E2_abs;

        fNewPolarization = C_parl * A_paral + C_perp * A_trans;
      }
      else
      { // incident ray perpendicular
        fNewMomentum = fOldMomentum;
        fNewPolarization = fOldPolarization;
      }
      //}
    }
    else
    { // Photon is absorbed

      log_critical("Photon is absorbed");
      if (verboseLevel > 2)
        G4cout << "Absorption in " << fMaterial1->GetName() << G4endl;

      fStatus = Detection;
      // aParticleChange.ProposeTrackStatus(fStopAndKill);
      aParticleChange.ProposeLocalEnergyDeposit(0.0);
      fNewMomentum = fOldMomentum;
      fNewPolarization = fOldPolarization;

      aParticleChange.ProposeTrackStatus(fStopAndKill);
      done = fStatus == Detection;
    }

    if (fStatus != Detection) {
      fOldMomentum = fNewMomentum.unit();
      fOldPolarization = fNewPolarization.unit();
      if ((fStatus == CoatedDielectricFrustratedTransmission) || (fStatus == CoatedDielectricRefraction))
      {
        done = (fNewMomentum * fGlobalNormal <= 0.0);
        // Condition checks if the dot product of the new momentum and the global normal is less than or equal to zero.
        // This means that the new momentum is pointing into or away from the material.
      }
      else
      {
        done = (fNewMomentum * fGlobalNormal >= -fCarTolerance);
        // Checks if the dot product is greater than or equal to -fCarTolerance.
        // This is done to ensure that the new momentum is not almost parallel to the surface
      }
    // For frustrated transmission or refraction, the loop continues until the photon goes into or away from the material
    // For other cases, the loop continues until the photon deviates significantly from being parallel to the surface
    }
  } while (!done);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
OpticalLayerResult G4OpBoundaryProcess::GetFresnelThroughThinLayer(G4double sinTL,
                                                                   G4double E1_perp,
                                                                   G4double E1_parl,
                                                                   G4double wavelength, G4double cost1, G4double cost2)
{

  G4complex Reflectivity, Refl_TE, Refl_TM, Transmittance, Trans_TE, Trans_TM, Abs;
  G4complex R4L, R4L_TE, R4L_TM, T4L, T4L_TE, T4L_TM, A4L, A4L_TE, A4L_TM; // Reflection, Transmittance and Absorption for a 4-Layer system
  G4double gammaTL, costTL;

  G4complex i(0, 1);
  G4complex rTM, rTE, tTE, tTM;
  G4complex r1toTL, rTLto2;
  G4complex r1toTL_TE, rTLto2_TE, r1toTL_TM, rTLto2_TM, t1toTL_TE, tTLto2_TE, t1toTL_TM, tTLto2_TM;
  G4double k0 = 2 * pi / wavelength;
  OpticalLayerResult lResult;

  G4complex fComplexRindex1(fRindex1, fImagRIndex1);
  G4complex fComplexRindex2(fRindex2, fImagRIndex2);
  G4complex fComplexCoatedRindex(fCoatedRindex, fCoatedImagRIndex);

  // Angle > Angle limit (relevant for n1>n2, not the case here)
  if (sinTL >= 1.0)
  {
    log_critical("sinTL >= 1");
    if (fCoatedFrustratedTransmission)
    { // Frustrated transmission

      if (cost1 > 0.0)
      {
        gammaTL = std::sqrt(fRindex1 * fRindex1 * fSint1 * fSint1 -
                            fCoatedRindex * fCoatedRindex);
      }
      else
      {
        gammaTL = -std::sqrt(fRindex1 * fRindex1 * fSint1 * fSint1 -
                             fCoatedRindex * fCoatedRindex);
      }

      // TE
      r1toTL = (fRindex1 * cost1 - i * gammaTL) / (fRindex1 * cost1 + i * gammaTL);
      rTLto2 = (i * gammaTL - fRindex2 * cost2) / (i * gammaTL + fRindex2 * cost2);
      // log_critical("TE: r1toTL ={}", std::real(r1toTL));
      // log_critical("TE: rTLto2 ={}", std::real(rTLto2));
      if (cost1 != 0.0)
      {
        rTE = (r1toTL + rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL)) /
              (1.0 + r1toTL * rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL));
        // log_critical("rTE ={}", std::real(rTE));
      }
      // TM
      r1toTL = (fRindex1 * i * gammaTL - fCoatedRindex * fCoatedRindex * cost1) /
               (fRindex1 * i * gammaTL + fCoatedRindex * fCoatedRindex * cost1);
      rTLto2 = (fCoatedRindex * fCoatedRindex * cost2 - fRindex2 * i * gammaTL) /
               (fCoatedRindex * fCoatedRindex * cost2 + fRindex2 * i * gammaTL);
      // log_critical("fRindex1 = {}", fRindex1);
      // log_critical("gammaTL = {}", gammaTL);
      // log_critical("fCoatedRindex = {}", fCoatedRindex);
      // log_critical("cost1 = {}", cost1);
      // log_critical("TM: r1toTL ={}", std::real(r1toTL));
      // log_critical("TM: rTLto2 ={}", std::real(rTLto2));
      if (cost1 != 0.0)
      {
        rTM = (r1toTL + rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL)) /
              (1.0 + r1toTL * rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL));
        // log_critical("rTM_Zähler ={}", std::real(r1toTL + rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL)));
        // log_critical("rTM_Nenner ={}", std::real(1.0 + r1toTL * rTLto2 * std::exp(-2 * k0 * fCoatedThickness * gammaTL)));
        // log_critical("exp() = {}", std::real(std::exp(-2 * k0 * fCoatedThickness * gammaTL)));
        // log_critical("rTM ={}", std::real(rTM));
      }
    }
    else
    { // Total reflection
      lResult.Reflectivity = 1.0;
      log_critical("Total Reflection");
      // return(1.0);
    }
  }

  // Angle <= Angle limit # -> The interesting part
  else // if (sinTL < 1.0)
  {

    if (cost1 > 0.0)
    {
      costTL = std::sqrt(1. - sinTL * sinTL);
    }
    else
    {
      costTL = -std::sqrt(1. - sinTL * sinTL);
    }

    G4complex beta = k0 * fComplexCoatedRindex * fCoatedThickness * costTL;

    // TE (s)
    r1toTL = (fComplexRindex1 * cost1 - fComplexCoatedRindex * costTL) / (fComplexRindex1 * cost1 + fComplexCoatedRindex * costTL);
    rTLto2 = (fComplexCoatedRindex * costTL - fComplexRindex2 * cost2) / (fComplexCoatedRindex * costTL + fComplexRindex2 * cost2);
    r1toTL_TE = r1toTL;
    rTLto2_TE = rTLto2;
    log_info("TE: r1toTL_TE ={}", std::real(r1toTL_TE));
    log_info("TE: rTLto2_TE ={}", std::real(rTLto2_TE));

    t1toTL_TE = 2.0 * fComplexRindex1 * cost1 / (fComplexRindex1 * cost1 + fComplexCoatedRindex * costTL);
    tTLto2_TE = 2.0 * fComplexCoatedRindex * costTL / (fComplexCoatedRindex * costTL + fComplexRindex2 * cost2);
    log_info("TE: t1toTL_TE ={}", std::real(t1toTL_TE));
    log_info("TE: tTLto2_TE ={}", std::real(tTLto2_TE));

    if (cost1 != 0.0)
    {
      // beta = k0 * fComplexCoatedRindex * fCoatedThickness * costTL
      rTE = (r1toTL + rTLto2 * std::exp(2.0 * i * beta)) /
            (1.0 + r1toTL * rTLto2 * std::exp(2.0 * i * beta));
      tTE = (t1toTL_TE * tTLto2_TE * std::exp(i * beta)) /
            (1.0 + r1toTL_TE * rTLto2_TE * std::exp(2.0 * i * beta));
      log_info("rTE = {}", std::real(rTE));
      log_info("tTE = {}", std::real(tTE));
    }

    // TM (p)
    r1toTL = (fComplexCoatedRindex * cost1 - fComplexRindex1 * costTL) / (fComplexRindex1 * costTL + fComplexCoatedRindex * cost1);
    rTLto2 = (fComplexRindex2 * costTL - fComplexCoatedRindex * cost2) / (fComplexCoatedRindex * cost2 + fComplexRindex2 * costTL);
    r1toTL_TM = r1toTL;
    rTLto2_TM = rTLto2;
    log_info("TM: r1toTL_TM = {}", std::real(r1toTL_TM));
    log_info("TM: rTLto2_TM = {}", std::real(rTLto2_TM));

    t1toTL_TM = 2.0 * fComplexRindex1 * cost1 / (fComplexRindex1 * costTL + fComplexCoatedRindex * cost1);
    tTLto2_TM = 2.0 * fComplexCoatedRindex * costTL / (fComplexCoatedRindex * cost2 + fComplexRindex2 * costTL);
    log_info("TM: t1toTL_TM = {}", std::real(t1toTL_TM));
    log_info("TM: tTLto2_TM = {}", std::real(tTLto2_TM));

    if (cost1 != 0.0)
    {
      rTM = (r1toTL + rTLto2 * std::exp(2.0 * i * beta)) /
            (1.0 + r1toTL * rTLto2 * std::exp(2.0 * i * beta));
      tTM = (t1toTL_TM * tTLto2_TM * std::exp(i * beta)) /
            (1.0 + r1toTL_TM * rTLto2_TM * std::exp(2.0 * i * beta));
      log_info("rTM = {}", std::real(rTM));
      log_info("tTM = {}", std::real(tTM));
    }
  }

  // Old formulas god knows where from
  // Refl_TE = (rTE * conj(rTE)) * (E1_perp * E1_perp) / (E1_perp * E1_perp + E1_parl * E1_parl);
  // Refl_TM = (rTM * conj(rTM)) * (E1_parl * E1_parl) / (E1_perp * E1_perp + E1_parl * E1_parl);
  // Reflectivity = Refl_TE + Refl_TM;

  Refl_TE = std::abs(rTE) * std::abs(rTE);
  Refl_TM = std::abs(rTM) * std::abs(rTM);
  Reflectivity = (Refl_TE + Refl_TM) / 2.0;

  Trans_TE = (fRindex2 * cost2) / (fRindex1 * cost1) * std::abs(tTE) * std::abs(tTE);
  Trans_TM = (fRindex2 * cost2) / (fRindex1 * cost1) * std::abs(tTM) * std::abs(tTM);
  Transmittance = (Trans_TE + Trans_TM) / 2.0;
  log_info("fRindex1 = {}", fRindex1);
  log_info("cost1 = {}", cost1);
  log_info("fRindex2 = {}", fRindex2);
  log_info("cost2 = {}", cost2);

  Abs = 1.0 - std::real(Reflectivity) - std::real(Transmittance);

  /* Reflectance + Transmittance but for a explicit 4-Layer system - 4L=4Layer
    (The old function is still above and returns the "Reflectivity" without considering a four layer system)

    R4L_TE = r1toTL_TE*r1toTL_TE + rTE*rTE  * (1.0 - r1toTL_TE*r1toTL_TE) * (1.0 -r1toTL_TE*r1toTL_TE) / (1.0 - r1toTL_TE*r1toTL_TE * rTE*rTE);
    R4L_TM = r1toTL_TM*r1toTL_TM + rTM*rTM  * (1.0 - r1toTL_TM*r1toTL_TM) * (1.0 -r1toTL_TM*r1toTL_TM) / (1.0 - r1toTL_TM*r1toTL_TM * rTM*rTM);
    R4L = (R4L_TE + R4L_TM) / 2.0;

    T4L_TE = (fCoatedThickness * costTL) / (fRindex1 * cost1) * t1toTL_TE *t1toTL_TE * (1.0 - r1toTL_TE*r1toTL_TE)*(1.0 - r1toTL_TE*r1toTL_TE) / (1.0 - r1toTL_TE*r1toTL_TE * rTE*rTE);
    T4L_TM = (fCoatedThickness * costTL) / (fRindex1 * cost1) * t1toTL_TM *t1toTL_TM * (1.0 - r1toTL_TM*r1toTL_TM)*(1.0 - r1toTL_TM*r1toTL_TM) / (1.0 - r1toTL_TM*r1toTL_TM * rTM*rTM);
    T4L = (T4L_TE + T4L_TM) / 2.0;

    A4L_TE = 1.0 - R4L_TE - T4L_TE;
    A4L_TM = 1.0 - R4L_TM - T4L_TM;
    A4L = (A4L_TE + A4L_TM) / 2.0;
  */

  lResult.Reflectivity = std::real(Reflectivity);
  lResult.Transmittance = std::real(Transmittance);
  lResult.Absorption = std::real(Abs);

  return lResult;
}