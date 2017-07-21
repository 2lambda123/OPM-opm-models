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
 * \copydoc Ewoms::StokesIntensiveQuantities
 */
#ifndef EWOMS_STOKES_INTENSIVE_QUANTITIES_HH
#define EWOMS_STOKES_INTENSIVE_QUANTITIES_HH

#include "stokesproperties.hh"

#include <ewoms/models/common/energymodule.hh>

#include <opm/material/fluidstates/CompositionalFluidState.hpp>
#include <opm/common/Valgrind.hpp>

#include <dune/geometry/quadraturerules.hh>
#include <dune/common/fvector.hh>

#include <vector>

namespace Ewoms {

/*!
 * \ingroup StokesModel
 * \ingroup IntensiveQuantities
 *
 * \brief Contains the intensive quantities of the Stokes model.
 */
template <class TypeTag>
class StokesIntensiveQuantities
    : public GET_PROP_TYPE(TypeTag, DiscIntensiveQuantities)
    , public EnergyIntensiveQuantities<TypeTag, GET_PROP_VALUE(TypeTag, EnableEnergy) >
{
    typedef typename GET_PROP_TYPE(TypeTag, DiscIntensiveQuantities) ParentType;
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, Evaluation) Evaluation;
    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, Indices) Indices;
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
    typedef typename GET_PROP_TYPE(TypeTag, ElementContext) ElementContext;

    enum { numComponents = FluidSystem::numComponents };
    enum { dim = GridView::dimension };
    enum { dimWorld = GridView::dimensionworld };
    enum { pressureIdx = Indices::pressureIdx };
    enum { moleFrac1Idx = Indices::moleFrac1Idx };
    enum { phaseIdx = GET_PROP_VALUE(TypeTag, StokesPhaseIndex) };
    enum { enableEnergy = GET_PROP_VALUE(TypeTag, EnableEnergy) };

    typedef typename GridView::ctype CoordScalar;
    typedef Dune::FieldVector<Evaluation, dimWorld> DimEvalVector;
    typedef Dune::FieldVector<Scalar, dimWorld> DimVector;
    typedef Dune::FieldVector<CoordScalar, dim> LocalPosition;
    typedef Ewoms::EnergyIntensiveQuantities<TypeTag, enableEnergy> EnergyIntensiveQuantities;
    typedef Opm::CompositionalFluidState<Evaluation, FluidSystem, enableEnergy> FluidState;
    typedef Opm::MathToolbox<Evaluation> Toolbox;

public:
    StokesIntensiveQuantities()
    {}

    StokesIntensiveQuantities(const StokesIntensiveQuantities& other)
        : ParentType()
    { std::memcpy(this, &other, sizeof(other)); }

    StokesIntensiveQuantities& operator=(const StokesIntensiveQuantities& other)
    { std::memcpy(this, &other, sizeof(other)); return *this; }

    /*!
     * \copydoc IntensiveQuantities::update
     */
    void update(const ElementContext& elemCtx, unsigned dofIdx, unsigned timeIdx)
    {
        ParentType::update(elemCtx, dofIdx, timeIdx);

        EnergyIntensiveQuantities::updateTemperatures_(fluidState_, elemCtx, dofIdx, timeIdx);

        const auto& priVars = elemCtx.primaryVars(dofIdx, timeIdx);
        fluidState_.setPressure(phaseIdx, priVars.makeEvaluation(pressureIdx, timeIdx));
        Opm::Valgrind::CheckDefined(fluidState_.pressure(phaseIdx));

        // set the saturation of the phase to 1. for the stokes model,
        // saturation is not a meaningful quanity, but it allows us to
        // reuse infrastructure written for the porous media models
        // more easily (e.g. the energy module)
        fluidState_.setSaturation(phaseIdx, 1.0);

        // set the phase composition
        Evaluation sumx = 0;
        for (unsigned compIdx = 1; compIdx < numComponents; ++compIdx) {
            fluidState_.setMoleFraction(phaseIdx, compIdx,
                                        priVars.makeEvaluation(moleFrac1Idx + compIdx - 1, timeIdx));
            sumx += fluidState_.moleFraction(phaseIdx, compIdx);
        }
        fluidState_.setMoleFraction(phaseIdx, 0, 1.0 - sumx);

        // create NullParameterCache and do dummy update
        typename FluidSystem::template ParameterCache<Evaluation> paramCache;
        paramCache.updateAll(fluidState_);

        fluidState_.setDensity(phaseIdx, FluidSystem::density(fluidState_, paramCache, phaseIdx));
        fluidState_.setViscosity(phaseIdx, FluidSystem::viscosity(fluidState_, paramCache, phaseIdx));

        // energy related quantities
        EnergyIntensiveQuantities::update_(fluidState_, paramCache, elemCtx, dofIdx, timeIdx);

        // the effective velocity of the control volume
        for (unsigned dimIdx = 0; dimIdx < dimWorld; ++dimIdx)
            velocityCenter_[dimIdx] = priVars.makeEvaluation(Indices::velocity0Idx + dimIdx, timeIdx);

        // the gravitational acceleration applying to the material
        // inside the volume
        gravity_ = elemCtx.problem().gravity();
    }

    /*!
     * \brief Update all gradients for a given sub-control volume.
     *
     * These gradients can actually be considered as extensive quantities, but since they
     * are attributed to the sub-control volumes and they are also not primary variables,
     * they are hacked into the framework like this.
     *
     * \param elemCtx The execution context from which the method is called.
     * \param dofIdx The index of the sub-control volume for which the
     *               intensive quantities should be calculated.
     * \param timeIdx The index for the time discretization for which
     *                the intensive quantities should be calculated
     */
    void updateScvGradients(const ElementContext& elemCtx, unsigned dofIdx, unsigned timeIdx)
    {
        auto focusDofIdx = elemCtx.focusDofIndex();

        // calculate the pressure gradient at the SCV using finite
        // element gradients
        pressureGrad_ = 0.0;
        for (unsigned i = 0; i < elemCtx.numDof(/*timeIdx=*/0); ++i) {
            const auto& feGrad = elemCtx.stencil(timeIdx).subControlVolume(dofIdx).gradCenter[i];
            const auto& fs = elemCtx.intensiveQuantities(i, timeIdx).fluidState();

            if (i == focusDofIdx) {
                for (unsigned dimIdx = 0; dimIdx < dimWorld; ++dimIdx)
                    pressureGrad_[dimIdx] += feGrad[dimIdx]*fs.pressure(phaseIdx);
            }
            else {
                for (unsigned dimIdx = 0; dimIdx < dimWorld; ++dimIdx)
                    pressureGrad_[dimIdx] += feGrad[dimIdx]*Toolbox::value(fs.pressure(phaseIdx));
            }

            Opm::Valgrind::CheckDefined(feGrad);
            Opm::Valgrind::CheckDefined(pressureGrad_);
        }

        // integrate the velocity over the sub-control volume
        // const auto& elemGeom = elemCtx.element().geometry();
        const auto& stencil = elemCtx.stencil(timeIdx);
        const auto& scvLocalGeom = stencil.subControlVolume(dofIdx).localGeometry();

        Dune::GeometryType geomType = scvLocalGeom.type();
        static const unsigned quadratureOrder = 2;
        const auto& rule = Dune::QuadratureRules<Scalar, dimWorld>::rule(geomType, quadratureOrder);

        // calculate the average velocity inside of the sub-control volume
        velocity_ = 0.0;
        for (auto it = rule.begin(); it != rule.end(); ++it) {
            const auto& posScvLocal = it->position();
            const auto& posElemLocal = scvLocalGeom.global(posScvLocal);

            DimEvalVector velocityAtPos = velocityAtPos_(elemCtx, timeIdx, posElemLocal);
            Scalar weight = it->weight();
            Scalar detjac = 1.0;

            for (unsigned dimIdx = 0; dimIdx < dimWorld; ++dimIdx) {
                velocity_[dimIdx] += weight*detjac*velocityAtPos[dimIdx];
            }
        }

        // since we want the average velocity, we have to divide the
        // integrated value by the volume of the SCV
        //velocity_ /= stencil.subControlVolume(dofIdx).volume;
    }

    /*!
     * \brief Returns the thermodynamic state of the fluid for the
     * control-volume.
     */
    const FluidState& fluidState() const
    { return fluidState_; }

    /*!
     * \brief Returns the porosity of the medium
     *
     * For the Navier-Stokes model this quantity does not make sense
     * because there is no porous medium. The method is included here
     * to allow the Navier-Stokes model share the energy module with
     * the porous-media models.
     */
    Scalar porosity() const
    { return 1.0; }

    /*!
     * \brief Returns the average velocity in the sub-control volume.
     */
    const DimEvalVector& velocity() const
    { return velocity_; }

    /*!
     * \brief Returns the velocity at the center in the sub-control volume.
     */
    const DimEvalVector& velocityCenter() const
    { return velocityCenter_; }

    /*!
     * \brief Returns the pressure gradient in the sub-control volume.
     */
    const DimEvalVector& pressureGradient() const
    { return pressureGrad_; }

    /*!
     * \brief Returns the gravitational acceleration vector in the
     *        sub-control volume.
     */
    const DimVector& gravity() const
    { return gravity_; }

private:
    DimEvalVector velocityAtPos_(const ElementContext& elemCtx,
                                 unsigned timeIdx,
                                 const LocalPosition& localPos) const
    {
        auto focusDofIdx = elemCtx.focusDofIndex();

        auto& feCache = elemCtx.gradientCalculator().localFiniteElementCache();
        const auto& localFiniteElement = feCache.get(elemCtx.element().type());

        typedef Dune::FieldVector<Scalar, 1> ShapeValue;
        std::vector<ShapeValue> shapeValue;

        localFiniteElement.localBasis().evaluateFunction(localPos, shapeValue);

        DimEvalVector result(0.0);
        for (unsigned dofIdx = 0; dofIdx < elemCtx.numDof(/*timeIdx=*/0); dofIdx++) {
            const auto& vCenter = elemCtx.intensiveQuantities(dofIdx, timeIdx).velocityCenter();

            if (dofIdx == focusDofIdx) {
                for (unsigned dimIdx = 0; dimIdx < dimWorld; ++dimIdx)
                    result[dimIdx] += shapeValue[dofIdx][0]*vCenter[dimIdx];
            }
            else {
                for (unsigned dimIdx = 0; dimIdx < dimWorld; ++dimIdx)
                    result[dimIdx] += shapeValue[dofIdx][0]*Toolbox::value(vCenter[dimIdx]);
            }
        }

        return result;
    }

    DimEvalVector velocity_;
    DimEvalVector velocityCenter_;
    DimVector gravity_;
    DimEvalVector pressureGrad_;
    FluidState fluidState_;
};

} // namespace Ewoms

#endif
