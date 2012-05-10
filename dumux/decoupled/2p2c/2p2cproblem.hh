// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*****************************************************************************
 *   Copyright (C) 2010 by Benjamin Faigle, Markus Wolff                     *
 *   Institute for Modelling Hydraulic and Environmental Systems             *
 *   University of Stuttgart, Germany                                        *
 *   email: <givenname>.<name>@iws.uni-stuttgart.de                          *
 *                                                                           *
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/
/*!
 * \file
 * \brief Base class for sequential 2p2c compositional problems
 */
#ifndef DUMUX_IMPETPROBLEM_2P2C_HH
#define DUMUX_IMPETPROBLEM_2P2C_HH

#include <dumux/decoupled/2p/impes/impesproblem2p.hh>
#include <dumux/decoupled/common/variableclass.hh>
#include <dumux/decoupled/2p2c/2p2cproperties.hh>

#include <dune/common/fvector.hh>

namespace Dumux
{
/*!
 * \ingroup IMPEC
 * \ingroup IMPETproblems
 * \brief  Base class for all compositional 2-phase problems which use an impet algorithm
 *
 * Extends IMPESProblem2P by the compositional the boundary formulation and initial conditions.
 * These can be specified via a feed mass fractions \f$ Z^k \f$ or a saturation, specified by
 * the appropriate flag.
 */
template<class TypeTag>
class IMPETProblem2P2C : public IMPESProblem2P<TypeTag>
{
    typedef IMPESProblem2P<TypeTag> ParentType;
    typedef typename GET_PROP_TYPE(TypeTag, Problem) Implementation;
    typedef typename GET_PROP_TYPE(TypeTag, TimeManager) TimeManager;
    typedef typename GET_PROP_TYPE(TypeTag, Indices) Indices;

    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GridView::Grid                         Grid;
    typedef typename GET_PROP_TYPE(TypeTag, Scalar)   Scalar;

    typedef typename GridView::Traits::template Codim<0>::Entity Element;

    // material properties
    typedef typename GET_PROP_TYPE(TypeTag, SpatialParams)    SpatialParams;


    enum {
        dim = Grid::dimension,
        dimWorld = Grid::dimensionworld
    };

    typedef Dune::FieldVector<Scalar, dimWorld>      GlobalPosition;

public:
    /*!
     * \brief The standard constructor
     *
     * \param timeManager The time manager
     * \param gridView The grid view
     */
    IMPETProblem2P2C(TimeManager &timeManager, const GridView &gridView)
        : ParentType(timeManager, gridView)
    { }
    /*!
     * \brief The constructor for a given spatialParameters
     *
     * This constructor uses a predefined SpatialParams object that was created (e.g. in
     * the problem) and does not create one in the base class.
     *
     * \param timeManager The time manager
     * \param gridView The grid view
     * \param spatialParams SpatialParams instantiation
     */
    IMPETProblem2P2C(TimeManager &timeManager, const GridView &gridView, SpatialParams &spatialParams)
        : ParentType(timeManager, gridView, spatialParams)
    { }

    virtual ~IMPETProblem2P2C()
    { }
    /*!
     * \name Problem parameters
     */
    // \{
    //! Saturation initial condition (dimensionless)
    /*! The problem is initialized with the following saturation. Both
     * phases are assumed to contain an equilibrium concentration of the
     * correspondingly other component.
     * \param element The element.
     */
    Scalar initSat(const Element& element) const
    {
        return asImp_().initSatAtPos(element.geometry().center());
    }
    //! Saturation initial condition (dimensionless) at given position
    /*! Has to be provided if initSat() is not used in the specific problem.
     *  \param globalPos The global position.
     */
    Scalar initSatAtPos(const GlobalPosition& globalPos) const
    {
        DUNE_THROW(Dune::NotImplemented, "please specify initial saturation in the problem"
                                            " using an initSatAtPos() method!");
        return NAN;
    }

    //! Concentration initial condition (dimensionless)
    /*! The problem is initialized with a  feed mass fraction:
     * Mass of component 1 per total mass \f$\mathrm{[-]}\f$. This directly
     * enters the flash calucalation.
     * \param element The element.
     */
    Scalar initConcentration(const Element& element) const
    {
        return asImp_().initConcentrationAtPos(element.geometry().center());
    }
    //! Concentration initial condition (dimensionless)
    /*! Has to be provided if initConcentration() is not used in the specific problem.
     *  \param globalPos The global position.
     */
    Scalar initConcentrationAtPos(const GlobalPosition& globalPos) const
    {
        DUNE_THROW(Dune::NotImplemented, "please specify initial Concentration in the problem"
                                            " using an initConcentrationAtPos() method!");
        return NAN;
    }
    // \}

private:
    //! Returns the implementation of the problem (i.e. static polymorphism)
    Implementation &asImp_()
    { return *static_cast<Implementation *>(this); }

    //! \copydoc Dumux::IMPETProblem::asImp_()
    const Implementation &asImp_() const
    { return *static_cast<const Implementation *>(this); }

protected:
    //! Sets entries of the primary variable vector to zero
    //
    void setZero(typename GET_PROP_TYPE(TypeTag, PrimaryVariables) &values, const int equation = -1) const
    {
        if (equation == Indices::pressureEqIdx)
        {
            values[Indices::pressureEqIdx] = 0.;
        }
        else if(equation == Indices::contiNEqIdx or Indices::contiWEqIdx)
        {
            values[Indices::contiNEqIdx] =0.;
            values[Indices::contiWEqIdx] =0.;
        }
        else if (equation == -1)
        {
            values[Indices::pressureEqIdx] = 0.;
            values[Indices::contiNEqIdx] =0.;
            values[Indices::contiWEqIdx] =0.;
        }
        else
            DUNE_THROW(Dune::InvalidStateException, "vector of primary variables can not be set properly");
    }
};

}

#endif
