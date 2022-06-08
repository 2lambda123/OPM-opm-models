// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.

  Consult the COPYING file in the top-level source directory of this
  module for the precise wording of the license and the list of
  copyright holders.
*/
/*!
 * \file
 *
 * \copydoc Opm::BlackOilLocalResidual
 */
#ifndef EWOMS_BLACK_OIL_LOCAL_TPFA_RESIDUAL_HH
#define EWOMS_BLACK_OIL_LOCAL_TPFA_RESIDUAL_HH

#include "blackoilproperties.hh"
#include "blackoilsolventmodules.hh"
#include "blackoilextbomodules.hh"
#include "blackoilpolymermodules.hh"
#include "blackoilenergymodules.hh"
#include "blackoilfoammodules.hh"
#include "blackoilbrinemodules.hh"
#include "blackoildiffusionmodule.hh"
#include "blackoilmicpmodules.hh"
#include <opm/material/fluidstates/BlackOilFluidState.hpp>

namespace Opm {
/*!
 * \ingroup BlackOilModel
 *
 * \brief Calculates the local residual of the black oil model.
 */
template <class TypeTag>
class BlackOilLocalResidualTPFA : public GetPropType<TypeTag, Properties::DiscLocalResidual>
{
    using IntensiveQuantities = GetPropType<TypeTag, Properties::IntensiveQuantities>;
    using ExtensiveQuantities = GetPropType<TypeTag, Properties::ExtensiveQuantities>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;
    using Indices = GetPropType<TypeTag, Properties::Indices>;
    using Scalar = GetPropType<TypeTag, Properties::Scalar>;
    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;
    using EqVector = GetPropType<TypeTag, Properties::EqVector>;
    using RateVector = GetPropType<TypeTag, Properties::RateVector>;
    using FluidSystem = GetPropType<TypeTag, Properties::FluidSystem>;
    using GridView = GetPropType<TypeTag, Properties::GridView>;

    enum { conti0EqIdx = Indices::conti0EqIdx };
    enum { numEq = getPropValue<TypeTag, Properties::NumEq>() };
    enum { numPhases = getPropValue<TypeTag, Properties::NumPhases>() };
    enum { numComponents = getPropValue<TypeTag, Properties::NumComponents>() };
    
    enum { dimWorld = GridView::dimensionworld };
    enum { gasPhaseIdx = FluidSystem::gasPhaseIdx };
    enum { oilPhaseIdx = FluidSystem::oilPhaseIdx };
    enum { waterPhaseIdx = FluidSystem::waterPhaseIdx };

    enum { gasCompIdx = FluidSystem::gasCompIdx };
    enum { oilCompIdx = FluidSystem::oilCompIdx };
    enum { waterCompIdx = FluidSystem::waterCompIdx };
    enum { compositionSwitchIdx = Indices::compositionSwitchIdx };

    static const bool waterEnabled = Indices::waterEnabled;
    static const bool gasEnabled = Indices::gasEnabled;
    static const bool oilEnabled = Indices::oilEnabled;
    static const bool compositionSwitchEnabled = (compositionSwitchIdx >= 0);

    static constexpr bool blackoilConserveSurfaceVolume = getPropValue<TypeTag, Properties::BlackoilConserveSurfaceVolume>();
    static constexpr bool enableEnergy = getPropValue<TypeTag, Properties::EnableEnergy>();
    static constexpr bool enableDiffusion = getPropValue<TypeTag, Properties::EnableDiffusion>();

    using Toolbox = MathToolbox<Evaluation>;
    using SolventModule = BlackOilSolventModule<TypeTag>;
    using ExtboModule = BlackOilExtboModule<TypeTag>;
    using PolymerModule = BlackOilPolymerModule<TypeTag>;
    using EnergyModule = BlackOilEnergyModule<TypeTag>;
    using FoamModule = BlackOilFoamModule<TypeTag>;
    using BrineModule = BlackOilBrineModule<TypeTag>;
    using DiffusionModule = BlackOilDiffusionModule<TypeTag, enableDiffusion>;
    using MICPModule = BlackOilMICPModule<TypeTag>;

public:
    /*!
     * \copydoc FvBaseLocalResidual::computeStorage
     */
    template <class LhsEval>
    void computeStorage(Dune::FieldVector<LhsEval, numEq>& storage,
                        const ElementContext& elemCtx,
                        unsigned dofIdx,
                        unsigned timeIdx) const
    {
        const IntensiveQuantities& intQuants = elemCtx.intensiveQuantities(dofIdx, timeIdx);
        computeStorage(storage,
                       intQuants,
                       timeIdx);
    }
    template <class LhsEval>
    void computeStorage(Dune::FieldVector<LhsEval, numEq>& storage,
                        const IntensiveQuantities& intQuants,
                        unsigned timeIdx) const{   
        // retrieve the intensive quantities for the SCV at the specified point in time
        const auto& fs = intQuants.fluidState();
        storage = 0.0;

        for (unsigned phaseIdx = 0; phaseIdx < numPhases; ++phaseIdx) {
            if (!FluidSystem::phaseIsActive(phaseIdx)) {
                if (Indices::numPhases == 3) { // add trivial equation for the pseudo phase
                    unsigned activeCompIdx = Indices::canonicalToActiveComponentIndex(FluidSystem::solventComponentIndex(phaseIdx));
                    if (timeIdx == 0)
                        storage[conti0EqIdx + activeCompIdx] = variable<LhsEval>(0.0, conti0EqIdx + activeCompIdx);
                    else
                        storage[conti0EqIdx + activeCompIdx] = 0.0;
                }
                continue;
            }

            unsigned activeCompIdx = Indices::canonicalToActiveComponentIndex(FluidSystem::solventComponentIndex(phaseIdx));
            LhsEval surfaceVolume =
                Toolbox::template decay<LhsEval>(fs.saturation(phaseIdx))
                * Toolbox::template decay<LhsEval>(fs.invB(phaseIdx))
                * Toolbox::template decay<LhsEval>(intQuants.porosity());

            storage[conti0EqIdx + activeCompIdx] += surfaceVolume;

            // account for dissolved gas
            if (phaseIdx == oilPhaseIdx && FluidSystem::enableDissolvedGas()) {
                unsigned activeGasCompIdx = Indices::canonicalToActiveComponentIndex(gasCompIdx);
                storage[conti0EqIdx + activeGasCompIdx] +=
                    Toolbox::template decay<LhsEval>(intQuants.fluidState().Rs())
                    * surfaceVolume;
            }

            // account for vaporized oil
            if (phaseIdx == gasPhaseIdx && FluidSystem::enableVaporizedOil()) {
                unsigned activeOilCompIdx = Indices::canonicalToActiveComponentIndex(oilCompIdx);
                storage[conti0EqIdx + activeOilCompIdx] +=
                    Toolbox::template decay<LhsEval>(intQuants.fluidState().Rv())
                    * surfaceVolume;
            }

            // account for vaporized water
            if (phaseIdx == gasPhaseIdx && FluidSystem::enableVaporizedWater()) {
                unsigned activeWaterCompIdx = Indices::canonicalToActiveComponentIndex(waterCompIdx);
                storage[conti0EqIdx + activeWaterCompIdx] +=
                    Toolbox::template decay<LhsEval>(intQuants.fluidState().Rvw())
                    * surfaceVolume;
            }
        }

        adaptMassConservationQuantities_(storage, intQuants.pvtRegionIndex());

        // deal with solvents (if present)
        SolventModule::addStorage(storage, intQuants);

        // deal with zFracton (if present)
        ExtboModule::addStorage(storage, intQuants);

        // deal with polymer (if present)
        PolymerModule::addStorage(storage, intQuants);

        // deal with energy (if present)
        EnergyModule::addStorage(storage, intQuants);

        // deal with foam (if present)
        FoamModule::addStorage(storage, intQuants);

        // deal with salt (if present)
        BrineModule::addStorage(storage, intQuants);

        // deal with micp (if present)
        MICPModule::addStorage(storage, intQuants);
    }

    /*!
     * \copydoc FvBaseLocalResidual::computeFlux
     */
    void computeFlux(RateVector& flux,
                     const ElementContext& elemCtx,
                     unsigned scvfIdx,
                     unsigned timeIdx) const
    {
        assert(timeIdx == 0);

        flux = 0.0;
        // need for dary flux calculation
        const auto& problem = elemCtx.problem();
        const auto& stencil = elemCtx.stencil(timeIdx);
        const auto& scvf = stencil.interiorFace(scvfIdx);

        unsigned interiorDofIdx = scvf.interiorIndex();
        unsigned exteriorDofIdx = scvf.exteriorIndex();
        assert(interiorDofIdx != exteriorDofIdx);

        //unsigned I = stencil.globalSpaceIndex(interiorDofIdx);
        //unsigned J = stencil.globalSpaceIndex(exteriorDofIdx);
        Scalar Vin = elemCtx.dofVolume(interiorDofIdx, /*timeIdx=*/0);
        Scalar Vex = elemCtx.dofVolume(exteriorDofIdx, /*timeIdx=*/0);
        const auto& globalIndexIn = stencil.globalSpaceIndex(interiorDofIdx);
        const auto& globalIndexEx = stencil.globalSpaceIndex(exteriorDofIdx);
        Scalar trans = problem.transmissibility(elemCtx, interiorDofIdx, exteriorDofIdx);
        Scalar faceArea = scvf.area();
        Scalar thpres = problem.thresholdPressure(globalIndexIn, globalIndexEx);

        // estimate the gravity correction: for performance reasons we use a simplified
        // approach for this flux module that assumes that gravity is constant and always
        // acts into the downwards direction. (i.e., no centrifuge experiments, sorry.)
        Scalar g = elemCtx.problem().gravity()[dimWorld - 1];

        const auto& intQuantsIn = elemCtx.intensiveQuantities(interiorDofIdx, timeIdx);
        const auto& intQuantsEx = elemCtx.intensiveQuantities(exteriorDofIdx, timeIdx);

        // this is quite hacky because the dune grid interface does not provide a
        // cellCenterDepth() method (so we ask the problem to provide it). The "good"
        // solution would be to take the Z coordinate of the element centroids, but since
        // ECL seems to like to be inconsistent on that front, it needs to be done like
        // here...
        Scalar zIn = problem.dofCenterDepth(elemCtx, interiorDofIdx, timeIdx);
        Scalar zEx = problem.dofCenterDepth(elemCtx, exteriorDofIdx, timeIdx);

        // the distances from the DOF's depths. (i.e., the additional depth of the
        // exterior DOF)
        Scalar distZ = zIn - zEx;

        //
        //const ExtensiveQuantities& extQuants = elemCtx.extensiveQuantities(scvfIdx, timeIdx);
        unsigned focusDofIdx = elemCtx.focusDofIndex();
        for (unsigned phaseIdx = 0; phaseIdx < numPhases; ++ phaseIdx) {
            if (!FluidSystem::phaseIsActive(phaseIdx))
                continue;
            // darcy flux calculation
            short dnIdx;
            //
            short upIdx;
            Evaluation pressureDifference;
            ExtensiveQuantities::calculatePhasePressureDiff_(upIdx,
                                                             dnIdx,
                                                             pressureDifference,
                                                             intQuantsIn,
                                                             intQuantsEx,
                                                             scvfIdx,//input
                                                             timeIdx,//input
                                                             phaseIdx,//input
                                                             interiorDofIdx,//input
                                                             exteriorDofIdx,//intput
                                                             Vin,
                                                             Vex,
                                                             globalIndexIn,
                                                             globalIndexEx,
                                                             distZ*g,
                                                             thpres);
            
            
            
            const IntensiveQuantities& up = (upIdx == interiorDofIdx) ? intQuantsIn : intQuantsEx;
                unsigned globalIndex;
                if(upIdx == interiorDofIdx){
                    //up = intQuantsIn;
                    globalIndex = globalIndexIn;
                }else{
                    //up = intQuantsEx;
                    globalIndex = globalIndexEx;
                }
                // TODO: should the rock compaction transmissibility multiplier be upstreamed
                // or averaged? all fluids should see the same compaction?!
                //const auto& globalIndex = stencil.globalSpaceIndex(upstreamIdx);
                const Evaluation& transMult =
                    problem.template rockCompTransMultiplier<Evaluation>(up, globalIndex);
                Evaluation darcyFlux;
                if(pressureDifference == 0){
                    darcyFlux = 0.0; //NB maybe we could drop calculations
                }else{
                    if (upIdx == interiorDofIdx)
                        darcyFlux =
                            pressureDifference*up.mobility(phaseIdx)*transMult*(-trans/faceArea);
                    else
                        darcyFlux =
                            pressureDifference*(Toolbox::value(up.mobility(phaseIdx))*Toolbox::value(transMult)*(-trans/faceArea));
                }
                //const auto& darcyFlux = extQuants.volumeFlux(phaseIdx);                       
                //unsigned upIdx = static_cast<unsigned>(extQuants.upstreamIndex(phaseIdx));
                               
                //const IntensiveQuantities& up = elemCtx.intensiveQuantities(upIdx, timeIdx);
                unsigned pvtRegionIdx = up.pvtRegionIndex();
                using FluidState = typename IntensiveQuantities::FluidState;
                if (upIdx == focusDofIdx){
                    const auto& invB = getInvB_<FluidSystem, FluidState, Evaluation>(up.fluidState(), phaseIdx, pvtRegionIdx);
                    const auto& surfaceVolumeFlux = invB*darcyFlux;           
                    evalPhaseFluxes_<Evaluation,Evaluation,FluidState>(flux, phaseIdx, pvtRegionIdx, surfaceVolumeFlux, up.fluidState());
                }else{
                    const auto& invB = getInvB_<FluidSystem, FluidState, Scalar>(up.fluidState(), phaseIdx, pvtRegionIdx);
                    const auto& surfaceVolumeFlux = invB*darcyFlux;               
                    evalPhaseFluxes_<Scalar,Evaluation,FluidState>(flux, phaseIdx, pvtRegionIdx, surfaceVolumeFlux, up.fluidState());
                }
            
            
        }

        // deal with solvents (if present)
        SolventModule::computeFlux(flux, elemCtx, scvfIdx, timeIdx);

        // deal with zFracton (if present)
        ExtboModule::computeFlux(flux, elemCtx, scvfIdx, timeIdx);

        // deal with polymer (if present)
        PolymerModule::computeFlux(flux, elemCtx, scvfIdx, timeIdx);

        // deal with energy (if present)
        EnergyModule::computeFlux(flux, elemCtx, scvfIdx, timeIdx);

        // deal with foam (if present)
        FoamModule::computeFlux(flux, elemCtx, scvfIdx, timeIdx);

        // deal with salt (if present)
        BrineModule::computeFlux(flux, elemCtx, scvfIdx, timeIdx);

        // deal with micp (if present)
        MICPModule::computeFlux(flux, elemCtx, scvfIdx, timeIdx);

        DiffusionModule::addDiffusiveFlux(flux, elemCtx, scvfIdx, timeIdx);
    }

    /*!
     * \copydoc FvBaseLocalResidual::computeSource
     */
    void computeSource(RateVector& source,
                       const ElementContext& elemCtx,
                       unsigned dofIdx,
                       unsigned timeIdx) const
    {
        // retrieve the source term intrinsic to the problem
        elemCtx.problem().source(source, elemCtx, dofIdx, timeIdx);

        // deal with MICP (if present)
        MICPModule::addSource(source, elemCtx, dofIdx, timeIdx);

        // scale the source term of the energy equation
        if (enableEnergy)
            source[Indices::contiEnergyEqIdx] *= getPropValue<TypeTag, Properties::BlackOilEnergyScalingFactor>();
    }

    template <class UpEval, class FluidState>
    static void evalPhaseFluxes_(RateVector& flux,
                                 unsigned phaseIdx,
                                 unsigned pvtRegionIdx,
                                 const ExtensiveQuantities& extQuants,
                                 const FluidState& upFs){
        
        const auto& invB = getInvB_<FluidSystem, FluidState, UpEval>(upFs, phaseIdx, pvtRegionIdx);
        const auto& surfaceVolumeFlux = invB*extQuants.volumeFlux(phaseIdx);           
        evalPhaseFluxes_<UpEval>(flux,
                         phaseIdx,
                         pvtRegionIdx,
                         surfaceVolumeFlux,
                         upFs);
    
    }
    /*!
     * \brief Helper function to calculate the flux of mass in terms of conservation
     *        quantities via specific fluid phase over a face.
     */
    template <class UpEval, class Eval,class FluidState>
    static void evalPhaseFluxes_(RateVector& flux,
                                 unsigned phaseIdx,
                                 unsigned pvtRegionIdx,
                                 const Eval& surfaceVolumeFlux,//value of RateVector
                                 const FluidState& upFs)
    {
        //const auto& invB = getInvB_<FluidSystem, FluidState, UpEval>(upFs, phaseIdx, pvtRegionIdx);
        //const auto& surfaceVolumeFlux = invB*extQuants.volumeFlux(phaseIdx);
        unsigned activeCompIdx = Indices::canonicalToActiveComponentIndex(FluidSystem::solventComponentIndex(phaseIdx));

        if (blackoilConserveSurfaceVolume)
            flux[conti0EqIdx + activeCompIdx] += surfaceVolumeFlux;
        else
            flux[conti0EqIdx + activeCompIdx] += surfaceVolumeFlux*FluidSystem::referenceDensity(phaseIdx, pvtRegionIdx);

        if (phaseIdx == oilPhaseIdx) {
            // dissolved gas (in the oil phase).
            if (FluidSystem::enableDissolvedGas()) {
                const auto& Rs = BlackOil::getRs_<FluidSystem, FluidState, UpEval>(upFs, pvtRegionIdx);

                unsigned activeGasCompIdx = Indices::canonicalToActiveComponentIndex(gasCompIdx);
                if (blackoilConserveSurfaceVolume)
                    flux[conti0EqIdx + activeGasCompIdx] += Rs*surfaceVolumeFlux;
                else
                    flux[conti0EqIdx + activeGasCompIdx] += Rs*surfaceVolumeFlux*FluidSystem::referenceDensity(gasPhaseIdx, pvtRegionIdx);
            }
        }
        else if (phaseIdx == gasPhaseIdx) {
            // vaporized oil (in the gas phase).
            if (FluidSystem::enableVaporizedOil()) {
                const auto& Rv = BlackOil::getRv_<FluidSystem, FluidState, UpEval>(upFs, pvtRegionIdx);

                unsigned activeOilCompIdx = Indices::canonicalToActiveComponentIndex(oilCompIdx);
                if (blackoilConserveSurfaceVolume)
                    flux[conti0EqIdx + activeOilCompIdx] += Rv*surfaceVolumeFlux;
                else
                    flux[conti0EqIdx + activeOilCompIdx] += Rv*surfaceVolumeFlux*FluidSystem::referenceDensity(oilPhaseIdx, pvtRegionIdx);
            }
             // vaporized water (in the gas phase).
            if (FluidSystem::enableVaporizedWater()) {
                const auto& Rvw = BlackOil::getRvw_<FluidSystem, FluidState, UpEval>(upFs, pvtRegionIdx);

                unsigned activeWaterCompIdx = Indices::canonicalToActiveComponentIndex(waterCompIdx);
                if (blackoilConserveSurfaceVolume)
                    flux[conti0EqIdx + activeWaterCompIdx] += Rvw*surfaceVolumeFlux;
                else
                    flux[conti0EqIdx + activeWaterCompIdx] += Rvw*surfaceVolumeFlux*FluidSystem::referenceDensity(waterPhaseIdx, pvtRegionIdx);
            }
        }
    }

    /*!
     * \brief Helper function to convert the mass-related parts of a Dune::FieldVector
     *        that stores conservation quantities in terms of "surface-volume" to the
     *        conservation quantities used by the model.
     *
     * Depending on the value of the BlackoilConserveSurfaceVolume property, the model
     * either conserves mass by means of "surface volume" of the components or mass
     * directly. In the former case, this method is a no-op; in the latter, the values
     * passed are multiplied by their respective pure component's density at surface
     * conditions.
     */
    template <class Scalar>
    static void adaptMassConservationQuantities_(Dune::FieldVector<Scalar, numEq>& container, unsigned pvtRegionIdx)
    {
        if (blackoilConserveSurfaceVolume)
            return;

        // convert "surface volume" to mass. this is complicated a bit by the fact that
        // not all phases are necessarily enabled. (we here assume that if a fluid phase
        // is disabled, its respective "main" component is not considered as well.)

        if (waterEnabled) {
            unsigned activeWaterCompIdx = Indices::canonicalToActiveComponentIndex(waterCompIdx);
            container[conti0EqIdx + activeWaterCompIdx] *=
                FluidSystem::referenceDensity(waterPhaseIdx, pvtRegionIdx);
        }

        if (gasEnabled) {
            unsigned activeGasCompIdx = Indices::canonicalToActiveComponentIndex(gasCompIdx);
            container[conti0EqIdx + activeGasCompIdx] *=
                FluidSystem::referenceDensity(gasPhaseIdx, pvtRegionIdx);
        }

        if (oilEnabled) {
            unsigned activeOilCompIdx = Indices::canonicalToActiveComponentIndex(oilCompIdx);
            container[conti0EqIdx + activeOilCompIdx] *=
                FluidSystem::referenceDensity(oilPhaseIdx, pvtRegionIdx);
        }
    }
};

} // namespace Opm

#endif
