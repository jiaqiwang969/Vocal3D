// ****************************************************************************
// This file is part of VocalTractLab3D.
// Copyright (C) 2022, Peter Birkholz, Dresden, Germany
// www.vocaltractlab.de
// author: Peter Birkholz and R�mi Blandin
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//
// ****************************************************************************

#include "Acoustic3dSimulation.h"
#include "Constants.h"
#include "Dsp.h"
#include "TlModel.h"
#include "TdsModel.h"
#include <algorithm>
#include <chrono>    // to get the computation time
#include <ctime>  
#include <string>
#include <regex>

// for Eigen
#include <Eigen/Dense>

// for CGAL
#include "Delaunay_mesh_vertex_base_with_info_2.h"
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Delaunay_mesh_face_base_2.h>
#include <CGAL/Delaunay_mesh_size_criteria_2.h>
#include <CGAL/Delaunay_mesher_2.h>
#include <CGAL/Boolean_set_operations_2.h>
#include <CGAL/Aff_transformation_2.h>
#include <CGAL/Delaunay_mesher_no_edge_refinement_2.h>
#include <CGAL/Polyline_simplification_2/simplify.h>
#include <CGAL/convex_hull_2.h>

// type for Eigen
typedef Eigen::MatrixXd Matrix;

// Types for CGAL
typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Delaunay_mesh_vertex_base_with_info_2<unsigned int, K>    Vb;
typedef CGAL::Delaunay_mesh_face_base_2<K> Fb;
typedef CGAL::Triangulation_data_structure_2<Vb, Fb> Tds;
typedef CGAL::Exact_intersections_tag                     Itag;
typedef CGAL::Constrained_Delaunay_triangulation_2<K, Tds, Itag> CDT;
typedef CGAL::Delaunay_mesh_size_criteria_2<CDT> Criteria;
typedef CDT::Point                    Point;
typedef CGAL::Point_3<K>                Point_3;
typedef CGAL::Vector_2<K>                Vector;
typedef CGAL::Polygon_2<K>                            Polygon_2;
typedef CGAL::Polygon_with_holes_2<K>                 Polygon_with_holes_2;
typedef std::list<Polygon_with_holes_2>               Pwh_list_2;
typedef CGAL::Aff_transformation_2<K>        Transformation;
typedef CGAL::Aff_transformation_3<K>         Transformation3;
typedef CGAL::Delaunay_mesher_no_edge_refinement_2<CDT, Criteria> MesherNoRefine;
typedef CGAL::Polyline_simplification_2::Stop_below_count_ratio_threshold Stop;
typedef CGAL::Polyline_simplification_2::Squared_distance_cost Cost;

// ****************************************************************************
// Independant functions
// ****************************************************************************

// ****************************************************************************
// Generate the coordinates of the points necessary for Gauss integration
// in each triangle of mesh

void gaussPointsFromMesh(vector<Point> &pts, vector<double> & areaFaces, const CDT &cdt)
{
  int numPts(cdt.number_of_vertices());
  double quadPtCoord[3][2]{ {1. / 6., 1. / 6.}, {2. / 3., 1. / 6.}, {1. / 6., 2. / 3.} };

  pts.clear();
  pts.reserve(numPts);
  areaFaces.clear();
  areaFaces.reserve(cdt.number_of_faces());

  for (auto itF = cdt.finite_faces_begin(); itF != cdt.finite_faces_end(); itF++)
  {
    // compute the area of the face
    areaFaces.push_back(abs(
      itF->vertex(0)->point().x() * (itF->vertex(1)->point().y() - itF->vertex(2)->point().y())
      + itF->vertex(1)->point().x() * (itF->vertex(2)->point().y() - itF->vertex(0)->point().y())
      + itF->vertex(2)->point().x() * (itF->vertex(0)->point().y() - itF->vertex(1)->point().y())) / 2);
  
    // create the Gauss integration points
    for (int g(0); g < 3; g++)
    {
      pts.push_back(Point(
      (1 - quadPtCoord[g][0] - quadPtCoord[g][1]) * itF->vertex(0)->point().x()
        + quadPtCoord[g][0] * itF->vertex(1)->point().x()
        + quadPtCoord[g][1] * itF->vertex(2)->point().x(),
        (1 - quadPtCoord[g][0] - quadPtCoord[g][1]) * itF->vertex(0)->point().y()
        + quadPtCoord[g][0] * itF->vertex(1)->point().y()
        + quadPtCoord[g][1] * itF->vertex(2)->point().y()));
    }
  }
}

// ****************************************************************************
// Check if 2 contours are similar with a distance criterion

bool similarContours(Polygon_2& cont1, Polygon_2& cont2, double minDist)
{
  bool similar(false);
  if (cont1.size() == cont2.size())
  {
    int nPt(cont1.size());
    similar = true;
    for (int i(0); i < nPt; i++)
    {
      if ((abs(cont1[i].x() - cont2[i].x()) > minDist)
        || (abs(cont1[i].y() - cont2[i].y()) > minDist))
      { 
        similar = false;
        break;
      }
    }
  }
  return similar;
}

// ****************************************************************************
// Constructor.
// ****************************************************************************

Acoustic3dSimulation::Acoustic3dSimulation()
// initialise the physical constants
  : m_geometryImported(false),
  m_reloadGeometry(true),
  m_meshDensity(5.),
  m_idxSecNoiseSource(25), // for /sh/ 212, for vowels 25
  m_glottisBoundaryCond(IFINITE_WAVGUIDE),
  m_mouthBoundaryCond(RADIATION),
  m_contInterpMeth(AREA)
{
  m_simuParams.temperature = 31.4266; // for 350 m/s
  m_simuParams.volumicMass = STATIC_PRESSURE_CGS * MOLECULAR_MASS / (GAS_CONSTANT *
    (m_simuParams.temperature + KELVIN_SHIFT));
  m_simuParams.numIntegrationStep = 3;
  m_simuParams.orderMagnusScheme = 2;
  m_simuParams.maxCutOnFreq = 20000.;
  m_simuParams.propMethod = MAGNUS;
  m_simuParams.viscoThermalLosses = true;
  m_simuParams.wallLosses = true;
  m_simuParams.constantWallImped = false;
  m_simuParams.wallAdmit = complex<double>(0.005, 0.);
  m_simuParams.sndSpeed = (sqrt(ADIABATIC_CONSTANT * STATIC_PRESSURE_CGS / m_simuParams.volumicMass));
  m_crossSections.reserve(2 * VocalTract::NUM_CENTERLINE_POINTS);
  m_simuParams.percentageLosses = 1.;
  m_simuParams.curved = true;
  m_simuParams.varyingArea = true;
  m_simuParams.junctionLosses = false;
  m_simuParams.needToComputeModesAndJunctions = true;
  m_simuParams.radImpedPrecomputed = false;
  m_simuParams.radImpedGridDensity = 15.;
  m_simuParams.integrationMethodRadiation = GAUSS;

  // for transfer function computation
  m_simuParams.maxComputedFreq = 10000.; // (double)SAMPLING_RATE / 2.;
  m_simuParams.spectrumLgthExponent = 10;
  m_simuParams.tfPoint.push_back(Point_3(3., 0., 0.));

  // for acoustic field computation
  m_simuParams.freqField = 5000.;
  m_simuParams.fieldPhysicalQuantity = PRESSURE;
  m_simuParams.showAmplitude = true;
  m_simuParams.fieldIndB = true;
  m_simuParams.fieldResolution = 30;
  m_simuParams.fieldResolutionPicture = 30;
  m_simuParams.computeRadiatedField = false;
  m_simuParams.computeFieldImage = true;

  m_oldSimuParams = m_simuParams;

  m_maxAmpField = -1.;
  m_minAmpField = -1.;
  m_maxPhaseField = 0.;
  m_minPhaseField = 0.;

  m_numFreq = 1 << (m_simuParams.spectrumLgthExponent - 1);
  m_numFreqPicture = m_numFreq;
  spectrum.setNewLength(2 * m_numFreq);
  spectrumNoise.setNewLength(2 * m_numFreq);

  m_lastFreqComputed = NAN;

  setBoundarySpecificAdmittance();
}

// ****************************************************************************
// Set the boundary specific admittance depending if frequency dependant losses
// are taken into account or not

void Acoustic3dSimulation::setBoundarySpecificAdmittance()
{
  if (m_simuParams.viscoThermalLosses)
  {
    //********************************************
    // compute boundary specific admittances 
    // for visco-thermal losses
    //********************************************

    // characteristic viscous boundary layer length
    double lv(AIR_VISCOSITY_CGS / m_simuParams.volumicMass / m_simuParams.sndSpeed);

    // characteristic thermal boundary layer length
    double lt(HEAT_CONDUCTION_CGS * MOLECULAR_MASS / m_simuParams.volumicMass / m_simuParams.sndSpeed /
    SPECIFIC_HEAT_CGS);

    // viscous boundary specific admittance
    m_simuParams.viscousBndSpecAdm = complex<double>(1., 1.) * sqrt(M_PI * lv / m_simuParams.sndSpeed);

    // thermal boundary specific admittance
    m_simuParams.thermalBndSpecAdm = complex<double>(1., 1.) * sqrt(M_PI * lt / m_simuParams.sndSpeed)
    * (ADIABATIC_CONSTANT - 1.);
  }
  else
  {

    //********************************************
    // Simple boundary specific admittance
    //********************************************

    m_simuParams.viscousBndSpecAdm = complex<double>(0., 0.);
    m_simuParams.thermalBndSpecAdm = complex<double>(0.005, 0.);

  }
}

// ****************************************************************************
/// Destructor.
// ****************************************************************************

Acoustic3dSimulation::~Acoustic3dSimulation(){}

// ****************************************************************************
// Static data.
// ****************************************************************************

Acoustic3dSimulation *Acoustic3dSimulation::instance = NULL;

// ****************************************************************************
/// Returns the one instance of this class.
// ****************************************************************************

Acoustic3dSimulation *Acoustic3dSimulation::getInstance()
{
  if (instance == NULL)
  {
    instance = new Acoustic3dSimulation();
  }
  return instance;
}

// ****************************************************************************
// ****************************************************************************
// Set computation parameters

void Acoustic3dSimulation::setSimulationParameters(double meshDensity,
  int secNoiseSource, struct simulationParameters simuParams, 
  enum openEndBoundaryCond cond, enum contourInterpolationMethod scalingMethod)
{
  m_meshDensity = meshDensity;
  m_idxSecNoiseSource = secNoiseSource;
  m_mouthBoundaryCond = cond;
  m_contInterpMeth = scalingMethod;
  m_simuParams = simuParams;

  m_numFreq = 1 << (m_simuParams.spectrumLgthExponent - 1);

  if (m_simuParams.viscoThermalLosses)
  {
    setBoundarySpecificAdmittance();
  }
}

// ****************************************************************************
// Clean the log file and print the simulation parameters

void Acoustic3dSimulation::generateLogFileHeader(bool cleanLog) {

  double freqSteps((double)SAMPLING_RATE / 2. / (double)m_numFreq);
  int numFreqComputed((int)ceil(m_simuParams.maxComputedFreq / freqSteps));

  ofstream log;
  if (cleanLog) {
    log.open("log.txt", ofstream::out | ofstream::trunc);
    log.close();
  }

  log.open("log.txt", ofstream::app);

  // print the date of the simulation
  time_t start_time = std::chrono::system_clock::to_time_t(chrono::system_clock::now());
  log << ctime(&start_time) << endl;

  if (m_geometryImported)
  {
    log << "Geometry imported from csv file:\n  " << m_geometryFile << endl;
  }
  else
  {
    log << "Geometry is from VocalTractLab" << endl;
  }
  log << endl;

  log << "PHYSICAL PARAMETERS:" << endl;
  log << "Temperature " << m_simuParams.temperature << " \xB0" <<  "C" << endl;
  log << "Volumic mass: " << m_simuParams.volumicMass << " g/cm^3" << endl;
  log << "Sound speed: " << m_simuParams.sndSpeed << " cm/s" << endl;
  log << endl;

  log << "BOUNDARY CONDITIONS:" << endl;
  log << "Percentage losses " << m_simuParams.percentageLosses * 100. << " %" << endl;
  if (m_simuParams.viscoThermalLosses)
  {
    log << "Visco-thermal losses included" << endl;
    log << "viscous boundary specific admittance " << m_simuParams.viscousBndSpecAdm
      << " g.cm^-2 .s^-1" << endl;
    log << "thermal boundary specific admittance " << m_simuParams.thermalBndSpecAdm
      << " g.cm^-2 .s^-1" << endl;
  }
  if (m_simuParams.wallLosses)
  {
    log << "Wall losse included" << endl;
  }
  if (m_simuParams.constantWallImped)
  {
    log << "Constant wall admittance " << m_simuParams.wallAdmit
      << " g.cm^-2 .s^-1" << endl;
  }
  log << "glottis boundary condition: ";
  switch (m_glottisBoundaryCond)
  {
  case HARD_WALL:
    log << "HARD_WALL" << endl;
    break;
  case IFINITE_WAVGUIDE:
    log << "IFINITE_WAVGUIDE" << endl;
    break;
  }
  log << "mouth boundary condition: ";
  switch (m_mouthBoundaryCond)
  {
  case RADIATION:
    log << "RADIATION" << endl;
    log << "Radiation impedance grid density: "
      << m_simuParams.radImpedGridDensity << endl;
    log << "Integration method: ";
    switch (m_simuParams.integrationMethodRadiation)
    {
    case DISCRETE:
      log << "DISCRETE";
      break;
    case GAUSS:
      log << "GAUSS";
      break;
    }
    log << endl;
    log << "Radiation impedance precomputed: ";
    if (m_simuParams.radImpedPrecomputed)
    {
      log << "YES" << endl;
    }
    else
    {
      log << "NO" << endl;
    }
    break;
  case IFINITE_WAVGUIDE:
    log << "IFINITE_WAVGUIDE" << endl;
    break;
  case HARD_WALL:
    log << "HARD_WALL" << endl;
    break;
  case ADMITTANCE_1:
    log << "ADMITTANCE_1" << endl;
    break;
  case ZERO_PRESSURE:
    log << "ZERO_PRESSURE" << endl;
    break;
  }
  log << endl;

  log << "MODE COMPUTATION PARAMETERS:" << endl;
  log << "Mesh density: " << m_meshDensity << endl;
  log << "Max cut-on frequency: " << m_simuParams.maxCutOnFreq << " Hz" << endl;
  log << "Compute modes and junction matrices: ";
  if (m_simuParams.needToComputeModesAndJunctions) { log << "YES"; }
  else { log << "NO"; }
  log << endl << endl;

  log << "INTEGRATION SCHEME PARAMETERS:" << endl;
  log << "Propagation mmethod: ";
  switch (m_simuParams.propMethod)
  {
  case MAGNUS:
    log << "MAGNUS order " << m_simuParams.orderMagnusScheme << endl;
    log << "Number of integration steps: " << m_simuParams.numIntegrationStep << endl;
    break;
  case STRAIGHT_TUBES:
    log << "STRAIGHT_TUBES" << endl;
    break;
  }
  if (m_simuParams.curved)
  {
    log << "Take into account curvature" << endl;
  }
  else
  {
    log << "No curvature" << endl;
  }
  if (m_simuParams.varyingArea)
  {
    log << "Area variation within segments taken into account" << endl;
    log << "scaling factor computation method : ";
    switch (m_contInterpMeth)
    {
    case AREA:
      log << "AREA" << endl;
      break;
    case BOUNDING_BOX:
      log << "BOUNDING_BOX" << endl;
      break;
    case FROM_FILE:
      log << "FROM_FILE" << endl;
      break;
    }
  }
  else
  {
    log << "No area variation in the segments" << endl;
  }
  if (m_simuParams.junctionLosses)
  {
    log << "Take into account losses at the junctions" << endl;
  }
  log << endl;

  log << "TRANSFER FUNCTION COMPUTATION PARAMETERS:" << endl;
  log << "Index of noise source section: " << m_idxSecNoiseSource << endl;
  log << "Maximal computed frequency: " << m_simuParams.maxComputedFreq
    << " Hz" << endl;
  log << "Spectrum exponent " << m_simuParams.spectrumLgthExponent << endl;
  log << "Frequency steps: " 
    << (double)SAMPLING_RATE / 2. / (double)(1 << (m_simuParams.spectrumLgthExponent - 1))
    << " Hz" << endl;
  log << "Number of simulated frequencies: " << numFreqComputed << endl;
  log << "Transfer function point (cm): " <<  endl;
  for (auto pt : m_simuParams.tfPoint)
  {
    log << pt << endl;
  }
  log << endl;

  log << "ACOUSTIC FIELD COMPUTATION PARAMETERS:" << endl;
  switch (m_simuParams.fieldPhysicalQuantity)
  {
  case PRESSURE:
    log << "Pressure ";
    break;

  case VELOCITY:
    log << "Velocity ";
    break;

  case IMPEDANCE:
    log << "Impedance ";
    break;

  case ADMITTANCE:
    log << "Admittance ";
    break;
  }
  if (m_simuParams.showAmplitude)
  {
    log << "amplitude ";
  }
  else
  {
    log << "phase ";
  }
  log << "field computation at " << m_simuParams.freqField
  << " Hz with " << m_simuParams.fieldResolution << " points per cm" << endl;
  log << "Spatial resolution for field picture: " 
    << m_simuParams.fieldResolutionPicture << " points per cm" << endl;
  log << "Bounding box:" << endl;
  log << "min x " << m_simuParams.bbox[0].x() << endl;
  log << "max x " << m_simuParams.bbox[1].x() << endl;
  log << "min y " << m_simuParams.bbox[0].y() << endl;
  log << "max y " << m_simuParams.bbox[1].y() << endl;
  log << "Compute radiated field ";
  if (m_simuParams.computeRadiatedField)
  {
    log << "YES" << endl;
  }
  else
  {
    log << "NO" << endl;
  }
  log << endl;
  log.close();
}

// ****************************************************************************

void Acoustic3dSimulation::setContourInterpolationMethod(enum contourInterpolationMethod method)
{
  m_contInterpMeth = method;
}

// ****************************************************************************
// Add a cross-section at the end

void Acoustic3dSimulation::addCrossSectionFEM(double area,
  double spacing,
  Polygon_2 contours, vector<int> surfacesIdx,
  double length, Point2D ctrLinePt, Point2D normal, double scalingFactors[2])
{
  m_crossSections.push_back(unique_ptr< CrossSection2d>(new CrossSection2dFEM(
    ctrLinePt, normal, area, spacing, contours, surfacesIdx,
    length, scalingFactors)));
}

// ****************************************************************************

void Acoustic3dSimulation::addCrossSectionRadiation(Point2D ctrLinePt, Point2D normal,
  double radius, double PMLThickness)
{
  m_crossSections.push_back(unique_ptr< CrossSection2d>(new
    CrossSection2dRadiation(ctrLinePt, normal,
      radius, PMLThickness)));
}

// ****************************************************************************
// create the meshes and compute the propagation modes
void Acoustic3dSimulation::computeMeshAndModes()
{
  //ofstream mesh;
  ofstream log;
  log.open("log.txt", ofstream::app);

  auto start = std::chrono::system_clock::now();
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;

  for (int i(0); i < m_crossSections.size(); i++)
  {
    start = std::chrono::system_clock::now();
    m_crossSections[i]->setSpacing(sqrt(m_crossSections[i]->area()) / m_meshDensity);
    m_crossSections[i]->buildMesh();
    end = std::chrono::system_clock::now();
    elapsed_seconds = end - start;
    log << "Seg " << i << " mesh, nb vertices: "
      << m_crossSections[i]->numberOfVertices()
      << " time: " << elapsed_seconds.count() << " s ";

    start = std::chrono::system_clock::now();
    m_crossSections[i]->computeModes(m_simuParams);
    end = std::chrono::system_clock::now();
    elapsed_seconds = end - start;
    log << m_crossSections[i]->numberOfModes() 
      << " modes computed, time: "
      << elapsed_seconds.count() << " s" << endl;
  }

  log.close();
}

// ****************************************************************************

void Acoustic3dSimulation::computeMeshAndModes(int segIdx)
{
  ofstream log;
  log.open("log.txt", ofstream::app);

  // generate mesh
  auto start = std::chrono::system_clock::now();
  m_crossSections[segIdx]->setSpacing(sqrt(m_crossSections[segIdx]->area()) / m_meshDensity);
  m_crossSections[segIdx]->buildMesh();
  auto end = std::chrono::system_clock::now();
  auto elapsed_seconds = end - start;
  log << "Sec " << segIdx << " mesh, nb vertices: "
    << m_crossSections[segIdx]->numberOfVertices()
    << " time: " << elapsed_seconds.count() << " s ";

  // compute modes
  start = std::chrono::system_clock::now();
  m_crossSections[segIdx]->computeModes(m_simuParams);
  end = std::chrono::system_clock::now();
  elapsed_seconds = end - start;
  log << m_crossSections[segIdx]->numberOfModes()
    << " modes computed, time: "
    << elapsed_seconds.count() << " s" << endl;

  log.close();
}

// ****************************************************************************
// Compute the junction matrices of a given segment

void Acoustic3dSimulation::computeJunctionMatrices(int segIdx)
{
  int nModes, nModesNext;
  vector<Matrix> matrixF;
  Polygon_2 contour, nextContour, intersecCont;
  double scaling[2];
  int nextSec;
  Vector ctlShift;
  int idxMinArea;
  Pwh_list_2 intersections;
  double spacing;
  vector<Point> pts;
  vector<double> areaFaces;
  CDT cdt;
  Matrix interpolation1, interpolation2;
  double quadPtWeight = 1. / 3.;

  if (m_crossSections[segIdx]->numNextSec() > 0)
  {
    nModes = m_crossSections[segIdx]->numberOfModes();

    matrixF.clear();

    // get the contour of the current cross-section
    contour.clear();
    scaling[0] = m_crossSections[segIdx]->scaleOut();
    Transformation scale(CGAL::SCALING, scaling[0]);

    // The centerline at the end of the segment can be different from the one 
    // at the beginning of the next segment
    nextSec = m_crossSections[segIdx]->nextSec(0);
    ctlShift = Vector(m_crossSections[nextSec]->ctrLinePtIn(),
      m_crossSections[segIdx]->ctrLinePtOut());
    ctlShift = Vector(0., ctlShift * m_crossSections[segIdx]->normalOut());
    Transformation translate(CGAL::TRANSLATION, ctlShift);

    contour = transform(translate, transform(scale, m_crossSections[segIdx]->contour()));

    // loop over the folowing connected cross-sections
    for (int ns(0); ns < m_crossSections[segIdx]->numNextSec(); ns++)
    {
      nextSec = m_crossSections[segIdx]->nextSec(ns);
      nModesNext = m_crossSections[nextSec]->numberOfModes();

      Matrix F(Matrix::Zero(nModes, nModesNext));

      // get the next contour
      nextContour.clear();
      scaling[1] = m_crossSections[nextSec]->scaleIn();
      Transformation scale(CGAL::SCALING, scaling[1]);
      nextContour = transform(scale, m_crossSections[nextSec]->contour());
      if (contour.area() >= nextContour.area())
      {
        idxMinArea = 1;
      }
      else
      {
        idxMinArea = 0;
      }
      nextContour = m_crossSections[nextSec]->contour();

      //////////////////////////////////////////////////////////////
      // Compute the intersections of the contours
      //////////////////////////////////////////////////////////////

      // compute the intersection between the contours of the current
      intersections.clear();
      if ((typeid(*m_crossSections[nextSec]) == typeid(CrossSection2dRadiation))
        || m_crossSections[segIdx]->isJunction())
      {
        intersections.push_back(Polygon_with_holes_2(contour));
      }
      else if (m_crossSections[nextSec]->isJunction())
      {
        intersections.push_back(Polygon_with_holes_2(nextContour));
      }
      else
      {
        if (!similarContours(contour, nextContour, MINIMAL_DISTANCE_DIFF_POLYGONS))
        {
          CGAL::intersection(contour, nextContour, std::back_inserter(intersections));
        }
        else
        {
          intersections.push_back(Polygon_with_holes_2(nextContour));
        }
      }

      spacing = min(scaling[0] * m_crossSections[segIdx]->spacing(),
        m_crossSections[nextSec]->spacing());

      if (intersections.size() > 0)
      {

        // loop over the intersection contours
        for (auto it = intersections.begin(); it != intersections.end(); ++it)
        {
          //////////////////////////////////////////////////////////////
          // Mesh the intersection surfaces and generate integration points
          //////////////////////////////////////////////////////////////

          // mesh the intersection contours
          intersecCont.clear();
          pts.clear();
          areaFaces.clear();
          cdt.clear();
          intersecCont = it->outer_boundary();
          cdt.insert_constraint(intersecCont.begin(), intersecCont.end(), true);
          CGAL::refine_Delaunay_mesh_2(cdt, Criteria(0.125, spacing));

          // remove the faces which lies outside of the contour
          for (auto itF = cdt.finite_faces_begin();
            itF != cdt.finite_faces_end(); ++itF)
          {
            if (!itF->is_in_domain())
            {
              cdt.delete_face(itF);
            }
          }

          gaussPointsFromMesh(pts, areaFaces, cdt);

          //////////////////////////////////////////////////////////////
          // Interpolate modes
          //////////////////////////////////////////////////////////////

          // interpolate the modes of the first cross-section
          interpolation1 = m_crossSections[segIdx]->interpolateModes(pts
            , 1. / scaling[0], -ctlShift);

          // interpolate the modes of the next cross-section
          interpolation2 = m_crossSections[nextSec]->interpolateModes(pts
            , 1. / scaling[1]);

          //////////////////////////////////////////////////////////////
          // Compute scatering matrix F
          //////////////////////////////////////////////////////////////

          // loop over faces to integrate the modes product
          for (int f(0); f < areaFaces.size(); f++)
          {
            if (areaFaces[f] != 0)
            {
              // loop over modes of the first cross-section
              for (int m(0); m < nModes; m++)
              {
                // loop over modes of the next cross-section
                for (int n(0); n < nModesNext; n++)
                {
                  // loop over the Gauss integration points
                  for (int g(0); g < 3; g++)
                  {
                    F(m, n) += areaFaces[f] * interpolation1(f * 3 + g, m) *
                      interpolation2(f * 3 + g, n) * quadPtWeight
                      / scaling[0] / scaling[1];
                  }
                }
              }
            }
          }
        }
      }
      matrixF.push_back(F);
    }
    m_crossSections[segIdx]->setMatrixF(matrixF);
  }
}

// ****************************************************************************
// Compute the junction matrices between the different cross-sections
void Acoustic3dSimulation::computeJunctionMatrices(bool computeG)
{
  Polygon_2 contour, nextContour, intersecCont, prevContour;
  Pwh_list_2 intersections, differences;
  vector<Point> pts;
  vector<double> areaFaces;
  CDT cdt;
  double spacing, scaling[2], areaDiff;
  Vector u, v, ctlShift;
  Matrix interpolation1, interpolation2;
  int nModes, nModesNext, nextSec, prevSec;
  double quadPtCoord[3][2]{ {1. / 6., 1. / 6.}, {2. / 3., 1. / 6.}, {1. / 6., 2. / 3.} };
  double quadPtWeight = 1. / 3.;
  vector<Matrix> matrixF;
  int idxMinArea;
  int tmpNextcontained, tmpPrevContained;
  Pwh_list_2 differencesLoc;
  vector<Matrix> matrixGStart;
  vector<Matrix> matrixGEnd;
  vector<int> nextContained;
  vector<int> prevContained;
  vector<Point> seeds;
  seeds.push_back(Point(0., 0.));

  // loop over the cross-section
  for (int i(0); i < m_crossSections.size(); i++)
  {
    if (m_crossSections[i]->numNextSec() > 0)
    {
      nModes = m_crossSections[i]->numberOfModes();

      matrixF.clear();

      // get the contour of the current cross-section
      contour.clear();
      scaling[0] = m_crossSections[i]->scaleOut();
      Transformation scale(CGAL::SCALING, scaling[0]);
      // The centerline at the end of the segment can be different from the one 
      // at the beginning of the next segment
      nextSec = m_crossSections[i]->nextSec(0);
      ctlShift = Vector(m_crossSections[nextSec]->ctrLinePtIn(),
        m_crossSections[i]->ctrLinePtOut());
      ctlShift = Vector(0., ctlShift * m_crossSections[i]->normalOut());
      Transformation translate(CGAL::TRANSLATION, ctlShift);
      contour = transform(translate, transform(scale, m_crossSections[i]->contour()));

      if (computeG) {
        nextContained.clear();
        nextContained.reserve(1);
        prevContained.clear();
        prevContained.reserve(m_crossSections[i]->numNextSec());
        differences.clear();
        differencesLoc.clear();
        differencesLoc.push_back(Polygon_with_holes_2(contour));
        nextContained.push_back(-1);
      }

      // loop over the folowing connected cross-sections
      for (int ns(0); ns < m_crossSections[i]->numNextSec(); ns++)
      {
        nextSec = m_crossSections[i]->nextSec(ns);
        nModesNext = m_crossSections[nextSec]->numberOfModes();

        Matrix F(Matrix::Zero(nModes, nModesNext));

        // get the next contour
        nextContour.clear();
        scaling[1] = m_crossSections[nextSec]->scaleIn();
        Transformation scale(CGAL::SCALING, scaling[1]);
        nextContour = transform(scale, m_crossSections[nextSec]->contour());
        if (contour.area() >= nextContour.area())
        {
          idxMinArea = 1;
        }
        else
        {
          idxMinArea = 0;
        }
        nextContour = m_crossSections[nextSec]->contour();

        //////////////////////////////////////////////////////////////
        // Compute the intersections of the contours
        //////////////////////////////////////////////////////////////

        // compute the intersection between the contours of the current
        intersections.clear();
        if ((typeid(*m_crossSections[nextSec]) == typeid(CrossSection2dRadiation))
          || m_crossSections[i]->isJunction())
        {
          intersections.push_back(Polygon_with_holes_2(contour));
        }
        else if (m_crossSections[nextSec]->isJunction())
        {
          intersections.push_back(Polygon_with_holes_2(nextContour));
        }
        else
        {
          if (!similarContours(contour, nextContour, MINIMAL_DISTANCE_DIFF_POLYGONS))
          {
            CGAL::intersection(contour, nextContour, std::back_inserter(intersections));
          }
          else
          {
            intersections.push_back(Polygon_with_holes_2(nextContour));
          }
        }

        if (computeG) {
          // compute the difference between this contour and the next one
          differences.clear();
          for (auto itD = differencesLoc.begin();
            itD != differencesLoc.end(); itD++)
          {
            // check if the next contour is contained inside without intersecting
            tmpNextcontained = nextSec;
            // loop over the points of the next contour
            for (auto itP = nextContour.begin(); itP != nextContour.end(); itP++)
            {
              if ((itD->outer_boundary()).has_on_unbounded_side(*itP))
              {
                tmpNextcontained = -1;
                break;
              }
            }
            if (tmpNextcontained != -1) { nextContained.back() = tmpNextcontained; }

            CGAL::difference(itD->outer_boundary(), nextContour, std::back_inserter(differences));

          }
          differencesLoc = differences;
        }

        spacing = min(scaling[0] * m_crossSections[i]->spacing(), 
          m_crossSections[nextSec]->spacing());

        if (intersections.size() > 0)
        {

          // loop over the intersection contours
          for (auto it = intersections.begin(); it != intersections.end(); ++it)
          {
            //////////////////////////////////////////////////////////////
            // Mesh the intersection surfaces and generate integration points
            //////////////////////////////////////////////////////////////

            // mesh the intersection contours
            intersecCont.clear();
            pts.clear();
            areaFaces.clear();
            cdt.clear();
            intersecCont = it->outer_boundary();
            cdt.insert_constraint(intersecCont.begin(), intersecCont.end(), true);
            CGAL::refine_Delaunay_mesh_2(cdt, Criteria(0.125, spacing));

            // remove the faces which lies outside of the contour
            for (auto itF = cdt.finite_faces_begin();
              itF != cdt.finite_faces_end(); ++itF)
            {
              if (!itF->is_in_domain())
              {
                cdt.delete_face(itF);
              }
            }

            gaussPointsFromMesh(pts, areaFaces, cdt);

            //////////////////////////////////////////////////////////////
            // Interpolate modes
            //////////////////////////////////////////////////////////////

            // interpolate the modes of the first cross-section
            interpolation1 = m_crossSections[i]->interpolateModes(pts
              ,1. / scaling[0], -ctlShift
            );

            // interpolate the modes of the next cross-section
            interpolation2 = m_crossSections[nextSec]->interpolateModes(pts
              ,1. / scaling[1]
            );

            //////////////////////////////////////////////////////////////
            // Compute scatering matrix F
            //////////////////////////////////////////////////////////////

            // loop over faces to integrate the modes product
            for (int f(0); f < areaFaces.size(); f++)
            {
              if (areaFaces[f] != 0)
              {
                // loop over modes of the first cross-section
                for (int m(0); m < nModes; m++)
                {
                  // loop over modes of the next cross-section
                  for (int n(0); n < nModesNext; n++)
                  {
                    // loop over the Gauss integration points
                    for (int g(0); g < 3; g++)
                    {
                      F(m, n) += areaFaces[f] * interpolation1(f * 3 + g,m) *
                        interpolation2(f * 3 + g,n) * quadPtWeight
                             / scaling[0] / scaling[1];
                    }
                  }
                }
              }
            }
          }
        }
        matrixF.push_back(F);
      }
      m_crossSections[i]->setMatrixF(matrixF);
    }

    if (computeG) {
      //////////////////////////////////////////////////////////////
      // Compute matrices G.
      //////////////////////////////////////////////////////////////
      

      //////////////////////////////////////////////////////////////
      // MATRIX G CORRESPONDING TO THE END OF THE CURRENT SECTION
      //////////////////////////////////////////////////////////////

      if (m_crossSections[i]->numNextSec() > 0)
      {
        matrixGEnd.clear();
        Matrix Ge(nModes, nModes);
        Ge = Matrix::Zero(nModes, nModes);
        areaDiff = 0.;

        //////////////////////////////////////////////////////////////
        // Mesh differences between contours
        //////////////////////////////////////////////////////////////

        // loop over the difference contours
        for (auto it = differencesLoc.begin();
          it != differencesLoc.end(); it++)
        {
          // add the area of this part of the difference to the 
          // total area of the difference
          areaDiff += it->outer_boundary().area();

          cdt.clear();
          cdt.insert_constraint((it->outer_boundary()).begin(),
            (it->outer_boundary()).end(), true);

          if (nextContained[0] != -1)
          {
            // if the next contour is completely contained in this one
            nextContour.clear();
            nextContour = m_crossSections[nextContained[0]]->contour();
            cdt.insert_constraint(nextContour.begin(), nextContour.end(), true);
            MesherNoRefine mesher(cdt, Criteria(0., spacing));

            // generate seed to avoid meshing inside the next contour
            seeds.back() = Point(nextContour.begin()->x() + VocalTract::PROFILE_SAMPLE_LENGTH / 2,
              nextContour.begin()->y());
            mesher.set_seeds(seeds.begin(), seeds.end(), false);
            mesher.refine_mesh();
          }
          else
          {
            MesherNoRefine mesher(cdt, Criteria(0., spacing));
            mesher.refine_mesh();
          }

          // remove the faces which lies outside of the contour
          for (auto itF = cdt.finite_faces_begin();
            itF != cdt.finite_faces_end(); ++itF)
          {
            if (!itF->is_in_domain())
            {
              cdt.delete_face(itF);
            }
          }

          gaussPointsFromMesh(pts, areaFaces, cdt);

          interpolation1 = m_crossSections[i]->interpolateModes(pts, scaling[0]);

          //////////////////////////////////////////////////////////////
          // Compute matrix G
          //////////////////////////////////////////////////////////////

          // loop over faces to integrate the modes product
          for (int f(0); f < areaFaces.size(); f++)
          {
            if (areaFaces[f] != 0)
            {
              // loop over modes
              for (int m(0); m < nModes; m++)
              {
                // loop over modes
                for (int n(m); n < nModes; n++)
                {
                  // loop over the Gauss integration points
                  for (int g(0); g < 3; g++)
                  {
                    Ge(m, n) += areaFaces[f] * interpolation1(f * 3 + g, m) *
                      interpolation1(f * 3 + g, n) * quadPtWeight;
                  }
                  if (m != n) { Ge(n, m) = Ge(m, n); }
                }
              }
            }
          }
        }

        Ge = (Matrix::Identity(nModes, nModes) - Ge).fullPivLu().inverse();

        m_crossSections[i]->setMatrixGend(Ge);
      }

      if (m_crossSections[i]->numPrevSec() > 0)
      {
        //////////////////////////////////////////////////////////////
        // COMPUTE THE DIFFERENCE WITH THE PREVIOUS SECTION
        //////////////////////////////////////////////////////////////

        differences.clear();
        differencesLoc.clear();
        differencesLoc.push_back(Polygon_with_holes_2(contour));
        prevContained.clear();
        prevContained.push_back(-1);

        //log << "Before compute difference Gstart" << endl;

        // loop over the preceding connected cross-sections
        for (int ps(0); ps < m_crossSections[i]->numPrevSec(); ps++)
        {
          prevSec = m_crossSections[i]->prevSec(ps);
          prevContour = m_crossSections[prevSec]->contour();

          // compute the difference between the contour and the previous one
          differences.clear();
          for (auto it = differencesLoc.begin();
            it != differencesLoc.end(); it++)
          {
            // check if the previous contour is contained inside without 
            // intersecting
            tmpPrevContained = prevSec;
            // loop over the points of the previous contour
            for (auto itP = prevContour.begin();
              itP != prevContour.end(); itP++)
            {
              if ((it->outer_boundary()).has_on_unbounded_side(*itP))
              {
                tmpPrevContained = -1;
                break;
              }
            }
            if (tmpPrevContained != -1) { prevContained.back() = tmpPrevContained; }

            CGAL::difference(it->outer_boundary(), prevContour,
              std::back_inserter(differences));
          }
          differencesLoc = differences;
        }

        //////////////////////////////////////////////////////////////
        // MATRIX G CORRESPONDING TO THE BEGINING OF THE SECTION
        //////////////////////////////////////////////////////////////

        matrixGStart.clear();
        Matrix Gs(nModes, nModes);
        Gs = Matrix::Zero(nModes, nModes);
        areaDiff = 0.;

        // loop over the difference contours
        for (auto it = differencesLoc.begin();
          it != differencesLoc.end(); it++)
        {
          // add the area of this part of the difference to the 
          // total area of the difference
          areaDiff += it->outer_boundary().area();

          // mesh the surface
          cdt.clear();
          cdt.insert_constraint((it->outer_boundary()).begin(),
            (it->outer_boundary()).end(), true);

          if (prevContained[0] != -1)
          {
            // if the previous contour is completely contained inside 
            // the current one
            prevContour = m_crossSections[prevContained[0]]->contour();
            cdt.insert_constraint(prevContour.begin(), prevContour.end(), true);
            MesherNoRefine mesher(cdt, Criteria(0., spacing));

            // generate seed to avoid meshing inside the previous contour
            seeds.back() = Point(prevContour.begin()->x() + VocalTract::PROFILE_SAMPLE_LENGTH / 2,
              prevContour.begin()->y());
            mesher.set_seeds(seeds.begin(), seeds.end(), false);
            mesher.refine_mesh();
          }
          else
          {
            MesherNoRefine mesher(cdt, Criteria(0., spacing));
            mesher.refine_mesh();
          }

          // remove the faces which are outside of the domain
          for (auto itF = cdt.finite_faces_begin();
            itF != cdt.finite_faces_end(); itF++)
          {
            if (!itF->is_in_domain())
            {
              cdt.delete_face(itF);
            }
          }

          // generate the integration points
          gaussPointsFromMesh(pts, areaFaces, cdt);

          // interpolate the propagation modes
          interpolation2 = m_crossSections[i]->interpolateModes(pts);

          // loop over the faces to integrate the modes product
          for (int f(0); f < areaFaces.size(); f++)
          {
            if (areaFaces[f] != 0.)
            {
              // loop over modes
              for (int m(0); m < nModes; m++)
              {
                // loop over modes
                for (int n(m); n < nModes; n++)
                {
                  // loop over Gauss integration points
                  for (int g(0); g < 3; g++)
                  {
                    Gs(m, n) += areaFaces[f] * interpolation2(f * 3 + g, m)
                      * interpolation2(f * 3 + g,n) * quadPtWeight;
                  }
                  if (m != n) { Gs(n, m) = Gs(m, n); }
                }
              }
            }
          }
        }
        Gs = (Matrix::Identity(nModes, nModes) - Gs).fullPivLu().inverse();
        m_crossSections[i]->setMatrixGstart(Gs);
      }
    }
  }
}

// **************************************************************************
// Propagate the impedance and admittance up to the other end of the geometry
// taking into account branches

void Acoustic3dSimulation::propagateImpedAdmitBranch(vector< Eigen::MatrixXcd> Q0, double freq,
  vector<int> startSections, vector<int> endSections, double direction)
{
  bool addSegsToList, isNotEndSeg, isNotInList;
  int m, n, idx, mn, ns;
  vector<int> prevSegs, nextSegs;
  vector<vector<int>> segToProp;
  vector<Matrix> Ftmp;
  Eigen::MatrixXcd Qout, Qini;
  Matrix F;
  complex<double> wallInterfaceAdmit(1i * 2. * M_PI * freq *
    m_simuParams.thermalBndSpecAdm / m_simuParams.sndSpeed);

  std::chrono::duration<double> time;

  ofstream log("log.txt", ofstream::app);
  log << "Start branches" << endl;

  // initialise the list of segment lists to propagate
  for (auto it : startSections)
  {
    segToProp.push_back(vector<int>());
    segToProp.back().push_back(it);
  }

  // while there are non propagated segments in the list
  ns = 0;
  while (ns < segToProp.size())
  {
    log << "ns = " << ns << endl;
    log << "Segs ";
    for (auto it : segToProp[ns])
    {
      log << it << "  ";
    }
    log << endl;

    // if the segment is an initial segment
    if (ns < startSections.size())
    {
      if (m_crossSections[segToProp[ns][0]]->computeImpedance())
      {
        m_crossSections[segToProp[ns][0]]->propagateMagnus(Q0[ns], m_simuParams, freq, direction, IMPEDANCE, &time);
      }
      else
      {
        m_crossSections[segToProp[ns][0]]->propagateMagnus(Q0[ns], m_simuParams, freq, direction, ADMITTANCE, &time);
      }
    }
    else
    {
      // get the list of previous sections
      if (direction > 0)
      {
        prevSegs = m_crossSections[segToProp[ns][0]]->prevSections();
      }
      else
      {
        prevSegs = m_crossSections[segToProp[ns][0]]->nextSections();
      }
      log << "Prevsegs: ";
      for (auto it : prevSegs)
      {
        log << it << "  ";
      }
      log << endl;

      //***********************************
      // if the previous segment is larger
      //***********************************

      if (m_crossSections[segToProp[ns][0]]->area() < m_crossSections[prevSegs[0]]->area())
      {
        // in this case there can be only one segment connected to the current segment

        // Get the mode matching matrices
        if (direction > 0)
        {
          Ftmp = m_crossSections[prevSegs[0]]->getMatrixF();
        }
        else
        {
          // for each segment of the segment group
          Ftmp.clear();
          for (auto it : segToProp[ns])
          {
            Ftmp.push_back(m_crossSections[it]->getMatrixF()[0].transpose());
          }
        }
        // determine the dimension m,n of the concatenated mode matching matrix
        m = Ftmp[0].rows();
        n = 0;
        for (auto it : Ftmp) { n += it.cols(); }
        F.resize(m, n);
        // concatenate the mode matching matrices
        for (auto it : Ftmp) { F << it; }

        // get the output impedance of the previous segment
        // if the admittance have been computed in this segment
        if (!m_crossSections[prevSegs[0]]->computeImpedance())
        {
          // compute the corresponding impedance
          Qout = m_crossSections[prevSegs[0]]->Yin().fullPivLu().inverse();
        }
        else
        {
          Qout = m_crossSections[prevSegs[0]]->Zin();
        }

        log << "F\n" << F << endl << endl;

        // Compute the input impedance
        Qini = F.transpose() * Qout * F;

        log << "Qini\n" << Qini.cwiseAbs() << endl << endl;

        // propagate the impedance in each of the connected tube
        idx = 0;
        for (auto it : segToProp[ns])
        {
          // get the number of modes
          mn = m_crossSections[it]->numberOfModes();
          // propagate the impedance, the initial impedance of each connected
          // tube is a submatrix of Qini
          m_crossSections[it]->propagateMagnus(Qini.block(idx, idx, mn, mn),
            m_simuParams, freq, direction, IMPEDANCE, &time);
          m_crossSections[it]->setComputImpedance(true);
          idx += mn;
        }
      }

      //***************************************
      // if the previous segment(s) is smaller
      //***************************************

      else 
      {
        //get the mode matching matrices
        Ftmp.clear();
        if (direction > 0)
        {
          for (auto it : prevSegs)
          {
            Ftmp.push_back(m_crossSections[it]->getMatrixF()[0].transpose());
          }
        }
        else
        {
          Ftmp = m_crossSections[segToProp[ns][0]]->getMatrixF();
        }
        // determine the dimension m,n of the concatenated mode matching matrix
        m = Ftmp[0].rows();
        n = 0;
        for (auto it : Ftmp) { n += it.cols(); }
        F.resize(m, n);
        // concatenate the mode matching matrices
        for (auto it : Ftmp) { F << it; }

        log << "F\n" << F << endl << endl;

        // build the output admittance matrix of all the previous segments
        Qout.setZero(n, n);
        idx = 0;
        for (auto it : prevSegs)
        {
          // get the mode number of the previous segment
          mn = m_crossSections[it]->numberOfModes();
          if (m_crossSections[it]->computeImpedance())
          {
            Qout.block(idx, idx, mn, mn) =
              m_crossSections[it]->Zin().fullPivLu().inverse();
          }
          else
          {
            Qout.block(idx, idx, mn, mn) = m_crossSections[it]->Yin();
          }
          idx += mn;
        }

        log << "Qout\n" << Qout.cwiseAbs() << endl << endl;

        // compute the input admittance matrix
        Qini = F * Qout * F.transpose();
        m_crossSections[segToProp[ns][0]]->propagateMagnus(Qini, m_simuParams, freq,
          direction, ADMITTANCE, &time);
        m_crossSections[segToProp[ns][0]]->setComputImpedance(false);
      }
    }

    //***********************************************************************
    // add the following connected segments to list of segments to propagate
    //***********************************************************************

    for (auto it : segToProp[ns])
    {
      // check if the segment is an end segment
      isNotEndSeg = true;
      for (auto endSec : endSections)
      {
        if (it == endSec) { isNotEndSeg = false; break; }
      }

      // get the indexes of the next connected segments
      if (direction > 0)
      {
        nextSegs = m_crossSections[it]->nextSections();
      }
      else
      {
        nextSegs = m_crossSections[it]->prevSections();
      }

      // check if there are connected segments 
      if (nextSegs.size() > 0) {

        // Check if the next segments are already in the list of segments 
        // to propagate
        isNotInList = true;
        for (int i(ns); i < segToProp.size(); i++)
        {
          if (segToProp[i][0] == nextSegs[0]) { isNotInList = false; break; }
        }

        if (isNotEndSeg && isNotInList)
        {
          // check if the connected segments can be added to the list
          //
          // it is necessary to check only for the first segment of the 
          // list since either there is only one segment connected to several
          // others, or there is several segments, but connected to the same

          if (nextSegs.size() > 1)
          {
            addSegsToList = true;
          }
          else
          {
            if (direction > 0)
            {
              prevSegs = m_crossSections[nextSegs[0]]->prevSections();
            }
            else
            {
              prevSegs = m_crossSections[nextSegs[0]]->nextSections();
            }

            for (auto prevSeg : prevSegs)
            {
              addSegsToList = false;
              // check if this index is among all the segment indexes which 
              // have already been propagated
              for (int i(ns); i > -1; i--)
              {
                for (auto propSeg : segToProp[i])
                {
                  if (propSeg == prevSeg) { addSegsToList = true; break; }
                }
                if (addSegsToList) { break; }
              }
              if (!addSegsToList) { break; }
            }
          }
          // if all the connected segments have been propagated
          // and the segments are not already in the list
          if (addSegsToList)
          {
            segToProp.push_back(nextSegs);
          }
        }
      }
    }
    if (ns < segToProp.size()) { ns++; }
  }
  log.close();
}

// **************************************************************************
// Propagate the impedance and admittance up to the other end of the geometry

void Acoustic3dSimulation::propagateImpedAdmit(Eigen::MatrixXcd& startImped,
  Eigen::MatrixXcd& startAdmit, double freq, int startSection, int endSection,
  std::chrono::duration<double> *time)
{
  int direction;
  if (startSection > endSection)
  {
    direction = -1;
  }
  else
  {
    direction = 1;
  }
  propagateImpedAdmit(startImped, startAdmit, freq, startSection, endSection,
    time, direction);
}

// ****************************************************************************

void Acoustic3dSimulation::propagateImpedAdmit(Eigen::MatrixXcd & startImped,
  Eigen::MatrixXcd & startAdmit, double freq, int startSection, int endSection,
  std::chrono::duration<double> *time, int direction)
{
  Eigen::MatrixXcd prevImped;
  Eigen::MatrixXcd prevAdmit;
  vector<Matrix> F;
  Matrix G;
  int numSec(m_crossSections.size()), nI, nPs;
  int prevSec;
  double areaRatio;
  complex<double> wallInterfaceAdmit(1i*2.*M_PI*freq* 
    m_simuParams.thermalBndSpecAdm/m_simuParams.sndSpeed);
  vector<Eigen::MatrixXcd> inputImped;

  // set the initial impedance and admittance matrices
  m_crossSections[startSection]->clearImpedance();
  m_crossSections[startSection]->clearAdmittance();

  // set the propagation direction of the first section
  m_crossSections[startSection]->setZdir(direction);
  m_crossSections[startSection]->setYdir(direction);
  
  switch(m_simuParams.propMethod)
  {
  case MAGNUS:
    m_crossSections[startSection]->propagateMagnus(startAdmit, m_simuParams,
      freq, (double)direction, ADMITTANCE, time);
    inputImped.clear();
    for (auto it : m_crossSections[startSection]->Y())
    {
      inputImped.push_back(it.fullPivLu().inverse());
    }
    m_crossSections[startSection]->setImpedance(inputImped);
    break;
  case STRAIGHT_TUBES:
    m_crossSections[startSection]->propagateImpedAdmitStraight(startImped, startAdmit,
      freq, m_simuParams, 100.,
      m_crossSections[max(0, min(numSec, startSection+direction))]->area());
    break;
  }

  // loop over sections
  for (int i(startSection + direction); i != (endSection + direction); i += direction)
  {

    prevSec = i - direction;
    m_crossSections[i]->clearImpedance();
    m_crossSections[i]->clearAdmittance();
    m_crossSections[i]->setZdir(direction);
    m_crossSections[i]->setYdir(direction);

    nI = m_crossSections[i]->numberOfModes();
    nPs = m_crossSections[prevSec]->numberOfModes();

    // Extract the scaterring matrix and its complementary
    F.clear();
    if (direction == -1)
    {
      F = m_crossSections[i]->getMatrixF();
      if (m_crossSections[i]->area() > m_crossSections[prevSec]->area())
      {
        G = Matrix::Identity(nI, nI) - F[0] * F[0].transpose();
      }
      else
      {
        G = Matrix::Identity(nPs, nPs) - F[0].transpose() * F[0];
      }
    }
    else
    {
      F = m_crossSections[prevSec]->getMatrixF();
      if (m_crossSections[i]->area() > m_crossSections[prevSec]->area())
      {
        G = Matrix::Identity(nI, nI) - F[0].transpose() * F[0];
      }
      else
      {
        G = Matrix::Identity(nPs, nPs) - F[0] * F[0].transpose();
      }
    }

    prevImped = Eigen::MatrixXcd::Zero(m_crossSections[i]->numberOfModes(),
      m_crossSections[i]->numberOfModes());
    prevAdmit = Eigen::MatrixXcd::Zero(m_crossSections[i]->numberOfModes(),
      m_crossSections[i]->numberOfModes());
    
    switch (m_simuParams.propMethod)
    {
    case MAGNUS:
      if (direction == -1)
      {
      // case of a contraction: area(i) > area(ps)
        if ((m_crossSections[i]->area() * 
                    pow(m_crossSections[i]->scaleOut(), 2)) >
           (m_crossSections[prevSec]->area() * 
            pow(m_crossSections[prevSec]->scaleIn(), 2)))
        {
          if (m_simuParams.junctionLosses)
          {
            prevAdmit +=
              (pow(m_crossSections[i]->scaleOut(), 2) /
                pow(m_crossSections[prevSec]->scaleIn(), 2)) *
              F[0] * m_crossSections[prevSec]->Yin()
              * (F[0].transpose()) - wallInterfaceAdmit * G;
          }
          else
          {
            prevAdmit +=
              (pow(m_crossSections[i]->scaleOut(), 2) /
                pow(m_crossSections[prevSec]->scaleIn(), 2)) *
              F[0] * m_crossSections[prevSec]->Yin()
              * (F[0].transpose());
          }
        }
      // case of an expansion: area(i) < area(ps)
        else
        {
          if (m_simuParams.junctionLosses)
          {
            prevImped +=
              (pow(m_crossSections[prevSec]->scaleIn(), 2) /
                pow(m_crossSections[i]->scaleOut(), 2)) *
              F[0] * m_crossSections[prevSec]->Zin()
              * (Matrix::Identity(nPs, nPs) -
                wallInterfaceAdmit *
                G * m_crossSections[prevSec]->Zin()).inverse()
              * (F[0].transpose());
          }
          else
          {
            prevImped +=
              (pow(m_crossSections[prevSec]->scaleIn(), 2) /
                pow(m_crossSections[i]->scaleOut(), 2)) *
              F[0] * m_crossSections[prevSec]->Zin()
              * (F[0].transpose());
          }
        prevAdmit += prevImped.fullPivLu().inverse();
        }
      }
      else
      {
      // case of a contraction: area(i) > area(ps)
        if ((m_crossSections[i]->area() * 
                    pow(m_crossSections[i]->scaleIn(), 2)) >
           (m_crossSections[prevSec]->area() * 
            pow(m_crossSections[prevSec]->scaleOut(), 2)))
        {
          if (m_simuParams.junctionLosses)
          {
            prevAdmit +=
              (pow(m_crossSections[i]->scaleIn(), 2) /
                pow(m_crossSections[prevSec]->scaleOut(), 2)) *
                (F[0].transpose()) *
              m_crossSections[prevSec]->Yout() * F[0]
              + wallInterfaceAdmit * G;
          }
          else
          {
            prevAdmit +=
              (pow(m_crossSections[i]->scaleIn(), 2) /
                pow(m_crossSections[prevSec]->scaleOut(), 2)) *
                (F[0].transpose()) *
              m_crossSections[prevSec]->Yout() * F[0];
          }
        }
      // case of an expansion: area(i) < area(ps)
        else
        {
          if (m_simuParams.junctionLosses)
          {
            prevImped +=
              (pow(m_crossSections[prevSec]->scaleOut(), 2) /
                pow(m_crossSections[i]->scaleIn(), 2)) *
                F[0].transpose() * m_crossSections[prevSec]->Zout()
              * (Matrix::Identity(nPs, nPs) + wallInterfaceAdmit *
                G*m_crossSections[prevSec]->Zout()).inverse() * F[0];
          }
          else
          {
            prevImped +=
              (pow(m_crossSections[prevSec]->scaleOut(), 2) /
                pow(m_crossSections[i]->scaleIn(), 2)) *
                F[0].transpose() * m_crossSections[prevSec]->Zout() * F[0];
          }
        prevAdmit += prevImped.fullPivLu().inverse();
        }
      }

      break;
    case STRAIGHT_TUBES:

      areaRatio = max(m_crossSections[prevSec]->area(),
        m_crossSections[i]->area()) /
        min(m_crossSections[prevSec]->area(),
          m_crossSections[i]->area());
      if (direction == -1)
      {
        // case of a contraction
        if (m_crossSections[i]->area() > m_crossSections[prevSec]->area())
        {
          prevAdmit += areaRatio * F[0] *
            m_crossSections[prevSec]->Yin()
            * (F[0].transpose());
          prevImped += prevAdmit.fullPivLu().inverse();
        }
        // case of an expansion
        else
        {
          prevImped += areaRatio * F[0] *
            m_crossSections[prevSec]->Zin()
            * (F[0].transpose());
          prevAdmit += prevImped.fullPivLu().inverse();
        }
      }
      else
      {
        // case of a contraction
        if (m_crossSections[i]->area() > m_crossSections[prevSec]->area())
        {
          prevAdmit += areaRatio * F[0] *
            m_crossSections[prevSec]->Yout()
            * (F[0].transpose());
          prevImped += prevAdmit.fullPivLu().inverse();
        }
        // case of an expansion
        else
        {
          prevImped += areaRatio * F[0] *
            m_crossSections[prevSec]->Zout()
            * (F[0].transpose());
          prevAdmit += prevImped.fullPivLu().inverse();
        }
      }
      break;
    }

    // propagate admittance in the section
    switch (m_simuParams.propMethod) {
    case MAGNUS:
      m_crossSections[i]->propagateMagnus(prevAdmit, m_simuParams,
        freq, (double)direction, ADMITTANCE, time);
      inputImped.clear();
      for (auto it : m_crossSections[i]->Y())
      {
        inputImped.push_back(it.fullPivLu().inverse());
      }
      m_crossSections[i]->setImpedance(inputImped);
      break;
    case STRAIGHT_TUBES:
      m_crossSections[i]->propagateImpedAdmitStraight(prevImped, prevAdmit,
        freq, m_simuParams, m_crossSections[prevSec]->area(),
        m_crossSections[max(0,min(numSec, i + direction))]->area());
      break;
    }
  }
}

// **************************************************************************
// Propagate the axial velocity up to the other end of the geometry

void Acoustic3dSimulation::propagateVelocityPress(Eigen::MatrixXcd& startVelocity,
  Eigen::MatrixXcd& startPressure, double freq, int startSection, int endSection,
  std::chrono::duration<double> *time)
{
  int direction;
  if (startSection > endSection)
  {
    direction = -1;
  }
  else
  {
    direction = 1;
  }
  propagateVelocityPress(startVelocity, startPressure, freq, startSection, endSection,
    time, direction);
}

// ****************************************************************************

void Acoustic3dSimulation::propagateVelocityPress(Eigen::MatrixXcd &startVelocity,
  Eigen::MatrixXcd& startPressure, double freq, int startSection, 
  int endSection, std::chrono::duration<double> *time, int direction)
{
  Eigen::MatrixXcd prevVelo(startVelocity), prevPress(startPressure);
  vector<Eigen::MatrixXcd> tmpQ, P, Y;
  vector<Matrix> F;
  Matrix G;
  int numSec(m_crossSections.size());
  int numX(m_simuParams.numIntegrationStep);
  int nextSec, nI, nNs;
  Eigen::MatrixXcd pressure;
  double areaRatio, tau, scaling;
  complex<double> wallInterfaceAdmit(1i * 2. * M_PI * freq * 
    m_simuParams.thermalBndSpecAdm / m_simuParams.sndSpeed);
  
  // loop over sections
  for (int i(startSection); i != (endSection); i += direction)
  {

    nextSec = i + direction;

    m_crossSections[i]->clearAxialVelocity();
    m_crossSections[i]->clearAcPressure();
    m_crossSections[i]->setQdir(direction);
    m_crossSections[i]->setPdir(direction);
    nI = m_crossSections[i]->numberOfModes();
    nNs = m_crossSections[nextSec]->numberOfModes();

    // propagate axial velocity and acoustic pressure in the section
    switch (m_simuParams.propMethod) {
    case MAGNUS:
      m_crossSections[i]->propagateMagnus(prevPress, m_simuParams,
        freq, (double)direction, PRESSURE, time);
      tmpQ.clear(); P.clear(); Y.clear();
      P = m_crossSections[i]->P();
      Y = m_crossSections[i]->Y();
      numX = Y.size();
      for (int pt(0); pt < numX; pt++ )
      {
        if (numX > 1) {
          if (direction == 1) {
            tau = (double)pt / (double)(numX - 1);
          }
          else
          {
            tau = (double)(numX - 1 - pt) / (double)(numX - 1);
          }
        }
        else
        {
          tau = 1.;
        }
        scaling = m_crossSections[i]->scaling(tau);
        tmpQ.push_back(Y[numX - 1 - pt] * P[pt]);
      }
      m_crossSections[i]->setAxialVelocity(tmpQ);
      break;
    case STRAIGHT_TUBES:
      m_crossSections[i]->propagatePressureVelocityStraight(prevVelo,
        prevPress, freq, m_simuParams,
        m_crossSections[nextSec]->area());
      break;
    }

    // get the scattering matrix 
    F.clear();
    if (direction == 1)
    {
      F = m_crossSections[i]->getMatrixF();
      if (m_crossSections[i]->area() > m_crossSections[nextSec]->area())
      {
        G = Matrix::Identity(nI, nI) - F[0] * F[0].transpose();
      }
      else
      {
        G = Matrix::Identity(nNs, nNs) - F[0].transpose() * F[0];
      }
    }
    else
    {
      F = m_crossSections[nextSec]->getMatrixF();
      if (m_crossSections[i]->area() > m_crossSections[nextSec]->area())
      {
        G = Matrix::Identity(nI, nI) - F[0].transpose() * F[0];
      }
      else
      {
        G = Matrix::Identity(nNs, nNs) - F[0] * F[0].transpose();
      }
    }

    prevVelo = Eigen::MatrixXcd::Zero(m_crossSections[nextSec]->numberOfModes(), 1);
    prevPress = Eigen::MatrixXcd::Zero(m_crossSections[nextSec]->numberOfModes(), 1);
    switch (m_simuParams.propMethod)
    {
    case MAGNUS:
      if (direction == -1)
      {
          // if the section contracts: area(i) > area(ns)
          if ((m_crossSections[i]->area()*
                     pow(m_crossSections[i]->scaleIn(), 2)) >
            (m_crossSections[nextSec]->area() *
             pow(m_crossSections[nextSec]->scaleOut(), 2)))
              {
                prevPress += F[0] *
                    m_crossSections[i]->Pin()
                  * m_crossSections[i]->scaleIn()
                  / m_crossSections[nextSec]->scaleOut();
                prevVelo +=
                  m_crossSections[nextSec]->Yout() * prevPress;
              }
          // if the section expends: area(i) < area(ns)
          else
          {
            if (m_simuParams.junctionLosses)
            {
              prevVelo +=
                (Matrix::Identity(nNs, nNs) + wallInterfaceAdmit *
                  G * m_crossSections[nextSec]->Zin()).inverse() *
                F[0] * m_crossSections[i]->Qin()
                * m_crossSections[nextSec]->scaleOut()
                / m_crossSections[i]->scaleIn();
            }
            else
            {
              prevVelo +=
                F[0] * m_crossSections[i]->Qin()
                * m_crossSections[nextSec]->scaleOut()
                / m_crossSections[i]->scaleIn();
            }
              prevPress +=
              m_crossSections[nextSec]->Zout() * prevVelo;
          }
      }
      else
      {
          // if the section contracts: area(i) > area(ns)
          if ((m_crossSections[i]->area()*
                     pow(m_crossSections[i]->scaleOut(), 2)) >
            (m_crossSections[nextSec]->area() *
             pow(m_crossSections[nextSec]->scaleIn(), 2)))
          {
            prevPress += 
                (F[0].transpose()) *
              m_crossSections[i]->Pout()
              * m_crossSections[i]->scaleOut()
              / m_crossSections[nextSec]->scaleIn();
            prevVelo +=
              m_crossSections[nextSec]->Yin() * prevPress;
          }
          // if the section expends: area(i) < area(ns)
          else
          {
            if (m_simuParams.junctionLosses)
            {
              prevVelo +=
                (Matrix::Identity(nNs, nNs) - wallInterfaceAdmit *
                  G * m_crossSections[nextSec]->Zin()).inverse() *
                (F[0].transpose()) * m_crossSections[i]->Qout()
                * m_crossSections[nextSec]->scaleIn()
                / m_crossSections[i]->scaleOut();
            }
            else
            {
              prevVelo +=
                  (F[0].transpose()) * m_crossSections[i]->Qout()
                * m_crossSections[nextSec]->scaleIn()
                / m_crossSections[i]->scaleOut();
            }
          prevPress += m_crossSections[nextSec]->Zin() * prevVelo;
          }
      }

      break;
    case STRAIGHT_TUBES:
      areaRatio = sqrt(max(m_crossSections[nextSec]->area(),
        m_crossSections[i]->area()) /
        min(m_crossSections[nextSec]->area(),
          m_crossSections[i]->area()));
      if (direction == -1)
      {
        // if the section expends
        if (m_crossSections[nextSec]->area() >
          m_crossSections[i]->area())
        {
          prevVelo += areaRatio * (F[0].transpose()) *
            m_crossSections[i]->Qin();
          prevPress +=
            m_crossSections[nextSec]->Zout() * prevVelo;
        }
        // if the section contracts
        else
        {
          prevPress += areaRatio * (F[0].transpose()) *
            m_crossSections[i]->Pin();
          prevVelo +=
            m_crossSections[nextSec]->Yout() * prevPress;
        }
      }
      else
      {
        // if the section expends
        if (m_crossSections[nextSec]->area() >
          m_crossSections[i]->area())
        {
          prevVelo += areaRatio * (F[0].transpose()) *
            m_crossSections[i]->Qout();
          prevPress +=
            m_crossSections[nextSec]->Zin() * prevVelo;
        }
        // if the section contracts
        else
        {
          prevPress += areaRatio * (F[0].transpose()) *
            m_crossSections[i]->Pout();
          prevVelo +=
            m_crossSections[nextSec]->Yin() * prevPress;
        }
      }
      break;
    }
  }

  // propagate in the last section
  m_crossSections[endSection]->clearAxialVelocity();
  m_crossSections[endSection]->clearAcPressure();
  m_crossSections[endSection]->setQdir(direction);
  m_crossSections[endSection]->setPdir(direction);
  switch (m_simuParams.propMethod) {
  case MAGNUS:
    m_crossSections[endSection]->propagateMagnus(prevPress, m_simuParams,
      freq, (double)direction, PRESSURE, time);
    tmpQ.clear(); P.clear(); Y.clear();
    Y = m_crossSections[endSection]->Y();
    P = m_crossSections[endSection]->P();
    numX = Y.size();
    for (int pt(0); pt < numX; pt++)
    {
      if (numX > 1) {
        if (direction == 1) {
          tau = (double)pt / (double)(numX - 1);
        }
        else
        {
          tau = (double)(numX - 1 - pt) / (double)(numX - 1);
        }
      }
      else
      {
        tau = 1.;
      }
      scaling = m_crossSections[endSection]->scaling(tau);
      tmpQ.push_back(Y[numX - 1 - pt] * P[pt]);
    }
    m_crossSections[endSection]->setAxialVelocity(tmpQ);
    break;
  case STRAIGHT_TUBES:
    m_crossSections[endSection]->propagatePressureVelocityStraight(prevVelo,
      prevPress, freq, m_simuParams, 100.);
    break;
  }
}

// **************************************************************************
// Extract the internal acoustic field at a point 
// (given in cartesian coordinates)


Eigen::VectorXcd Acoustic3dSimulation::acousticField(vector<Point_3> queryPt)
{
  Eigen::VectorXcd field(queryPt.size()); 
  
  for (int i(0); i < queryPt.size(); i++)
  {
    field(i) = acousticField(queryPt[i]);
  }  

  return(field);
}

// ****************************************************************************

complex<double> Acoustic3dSimulation::acousticField(Point_3 queryPt)
{
  bool ptFound(false);
  int numSec(m_crossSections.size());
  Point_3 outPt;
  complex<double> field(NAN, NAN);

  Vector vec, endNormal(m_crossSections.back()->normalOut());
  Point endCenterLine(m_crossSections.back()->ctrLinePtOut());
  double angle;
  vector<Point_3> radPts;
  Eigen::VectorXcd radPress;
  radPts.reserve(1);
  radPts.push_back(Point_3());

  // check if the point is in the radiation domain
  vec = Vector(endCenterLine, Point(queryPt.x(), queryPt.z()));
  // for straight segments
  if (abs(m_crossSections.back()->circleArcAngle()) <= MINIMAL_DISTANCE)
  {
    double length(m_crossSections.back()->ctrLinePtOut().x()
        - m_crossSections.back()->ctrLinePtIn().x());
    if (signbit(vec.x() * length))
    {
      angle = -1.;
    }
    else
    {
      angle = 1.;
    }  
  }
  else
  // for curved segments
  {
    if (signbit(m_crossSections.back()->curvRadius() * m_crossSections.back()->circleArcAngle()))
    {
      angle = M_PI - fmod(atan2(vec.y(), vec.x()) -
        atan2(endNormal.y(), endNormal.x()) + 2. * M_PI, 2. * M_PI);
    }
    else
    {
      angle = fmod(atan2(vec.y(), vec.x()) -
        atan2(endNormal.y(), endNormal.x()) + 2. * M_PI, 2. * M_PI) - M_PI;
    }
  }

  if (angle <= 0.)
  {
    for (int s(0); s < numSec; s++)
    {
      if (m_crossSections[s]->getCoordinateFromCartesianPt(queryPt, outPt, false))
      {
        ptFound = true;
        field = m_crossSections[s]->interiorField(outPt, m_simuParams);
        break;
      }
    }
    if (!ptFound)
    {
      field = complex<double>(NAN, NAN);
    }
  }
  else if (m_simuParams.computeRadiatedField)
  {
    radPts[0] = Point_3(vec.x(), queryPt.y(), vec.y());
    RayleighSommerfeldIntegral(radPts, radPress, m_simuParams.freqField, numSec - 1);
    field = radPress(0);
  }

  return(field);
}

// **************************************************************************
// If the query point (in the sagittal plane) is inside a segment return true
// and write the segment index

bool Acoustic3dSimulation::findSegmentContainingPoint(Point queryPt, int& idxSeg)
{
  Point_3 outPt;
  bool segFound(false);

  for (int i(0); i < m_crossSections.size(); i++)
  {
    if (m_crossSections[i]->getCoordinateFromCartesianPt(
      Point_3(queryPt.x(), 0., queryPt.y()), outPt, true))
    {
      idxSeg = i;
      segFound = true;
      break;
    }
  }

  return(segFound);
}

// **************************************************************************
// set the variables for the computation of acoustic field

void Acoustic3dSimulation::prepareAcousticFieldComputation()
{
  // set the field resolution of the field picture as the one used for the computation
  m_simuParams.fieldResolutionPicture = m_simuParams.fieldResolution;

  // save the bounding box corresponding to this field computation
  for (int i(0); i < 2; i++) { m_simuParams.bboxLastFieldComputed[i] = m_simuParams.bbox[i]; }

  m_lx = m_simuParams.bbox[1].x() - m_simuParams.bbox[0].x();
  m_ly = m_simuParams.bbox[1].y() - m_simuParams.bbox[0].y();
  m_nPtx = round(m_lx * (double)m_simuParams.fieldResolution);
  m_nPty = round(m_ly * (double)m_simuParams.fieldResolution);

  m_field.resize(m_nPty, m_nPtx);
  m_field.setConstant(NAN);
  m_maxAmpField = 0.;
  m_minAmpField = 100.;
  m_maxPhaseField = 0.;
  m_minPhaseField = 0.;
}

// **************************************************************************
// Extract the acoustic field of a line of a matrix of points

void Acoustic3dSimulation::acousticFieldInLine(int idxLine)
{
  Point_3 queryPt;

  for (int j(0); j < m_nPty; j++)
  {
    // generate cartesian coordinates point to search
    queryPt = Point_3(m_lx * (double)idxLine / (double)(m_nPtx - 1) + m_simuParams.bbox[0].x(), 0.,
      m_ly * (double)j / (double)(m_nPty - 1) + m_simuParams.bbox[0].y());

    m_field(j, idxLine) = acousticField(queryPt);

    // compute the minimal and maximal amplitude of the field
    m_maxAmpField = max(m_maxAmpField, abs(m_field(j, idxLine)));
    m_minAmpField = min(m_minAmpField, abs(m_field(j, idxLine)));

    // compute the minimal and maximal phase of the field
    m_maxPhaseField = max(m_maxPhaseField, arg(m_field(j, idxLine)));
    m_minPhaseField = min(m_minPhaseField, arg(m_field(j, idxLine)));
  }
}

// **************************************************************************
// Extract the acoustic field in a plane

void Acoustic3dSimulation::acousticFieldInPlane()
{
  int cnt(0);

  ofstream log("log.txt", ofstream::app);

  prepareAcousticFieldComputation();

  for (int i(0); i < m_nPtx; i++)
  {
    acousticFieldInLine(i);
    cnt += m_nPty;

    log << 100 * cnt / m_nPtx / m_nPty << " % of field points computed" << endl;
  }

  log.close();
}

// **************************************************************************
// Solve the wave problem at a given frequency

void Acoustic3dSimulation::solveWaveProblem(VocalTract* tract, double freq, 
  bool precomputeRadImped, std::chrono::duration<double> &time,
  std::chrono::duration<double> *timeExp)
{
  int numSec(m_crossSections.size()); 
  int lastSec(numSec - 1);

  ofstream log("log.txt", ofstream::app);

  // for time tracking
  auto start = std::chrono::system_clock::now();
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds;

  //******************************************************
  // Compute the modes and junction matrices if necessary 
  //******************************************************

  if (m_simuParams.needToComputeModesAndJunctions)
  {
    // set mode number to 0 to make sure that it is defined by the maximal cutoff 
    // frequency when modes are computed
    for (int i(0); i < m_crossSections.size(); i++)
    {
      m_crossSections[i]->setModesNumber(0);
    }

    // compute modes
    start = std::chrono::system_clock::now();
    for (int i(0); i < m_crossSections.size(); i++)
    {
      computeMeshAndModes(i);
    }
    end = std::chrono::system_clock::now();
    elapsed_seconds = end - start;
    log << "Time mesh and modes: " << elapsed_seconds.count() << endl;

    // compute junction matrices
    start = std::chrono::system_clock::now();
    for (int i(0); i < m_crossSections.size(); i++)
    {
      computeJunctionMatrices(i);
      log << "Junction segment " << i << " computed" << endl;
    }
    //computeJunctionMatrices(false);
    end = std::chrono::system_clock::now();
    elapsed_seconds = end - start;
    log << "Time junction matrices: " << elapsed_seconds.count() << endl;

    if (precomputeRadImped && (m_mouthBoundaryCond == RADIATION))
    {
      start = std::chrono::system_clock::now();
      preComputeRadiationMatrices(16, lastSec);
      end = std::chrono::system_clock::now();
      elapsed_seconds = end - start;
      log << "Time radiation impedance: " << elapsed_seconds.count() << endl;
    }

    m_simuParams.needToComputeModesAndJunctions = false;
  }

  if (precomputeRadImped && !m_simuParams.radImpedPrecomputed 
    && (m_mouthBoundaryCond == RADIATION))
  {
    start = std::chrono::system_clock::now();
    preComputeRadiationMatrices(16, lastSec);
    end = std::chrono::system_clock::now();
    elapsed_seconds = end - start;
    log << "Time radiation impedance: " << elapsed_seconds.count() << endl;
  }

  // Actually solve the wave problem
  solveWaveProblem(tract, freq, time, timeExp);

  log.close();
}

// **************************************************************************

void Acoustic3dSimulation::solveWaveProblem(VocalTract* tract, double freq,
  std::chrono::duration<double>& time, std::chrono::duration<double>* timeExp)
{
  int numSec(m_crossSections.size());
  int lastSec(numSec - 1);
  int mn;

  m_lastFreqComputed = freq;

  //******************************************************
  // Set sources parameters
  //******************************************************

  auto start = std::chrono::system_clock::now();

  // generate mode amplitude source matrices
  mn = m_crossSections[0]->numberOfModes();
  Eigen::MatrixXcd inputVelocity(Eigen::MatrixXcd::Zero(mn, 1));
  Eigen::MatrixXcd inputPressure(Eigen::MatrixXcd::Zero(mn, 1));

  //******************************************************
  // Propagate impedance, admittance, velocity and pressure
  //******************************************************

  // get the radiation impedance matrix
  mn = m_crossSections.back()->numberOfModes();
  Eigen::MatrixXcd radImped, radAdmit;
  switch (m_mouthBoundaryCond)
  {
  case RADIATION:
    getRadiationImpedanceAdmittance(radImped, radAdmit, freq, lastSec);
    break;
  case ADMITTANCE_1:
    radAdmit.setZero(mn, mn);
    radAdmit.diagonal() = Eigen::VectorXcd::Constant(mn, complex<double>(
      pow(m_crossSections[lastSec]->scaleOut(), 2), 0.));
    radImped = radAdmit.inverse();
    break;
  case ZERO_PRESSURE:
    radAdmit.setZero(mn, mn);
    radAdmit.diagonal() = Eigen::VectorXcd::Constant(mn, complex<double>(1e10, 0.));
    radImped = radAdmit.inverse();
    break;
  }

  // propagate impedance and admittance
  propagateImpedAdmit(radImped, radAdmit, freq, lastSec, 0, timeExp);

  // create input velocity, for a constant input velocity q = -j * w * rho * v 
  inputVelocity(0, 0) = -1i * 2. * M_PI * freq * m_simuParams.volumicMass
    * pow(m_crossSections[0]->scaleIn(), 3)
    * m_crossSections[0]->area();
  inputPressure = m_crossSections[0]->Zin() * inputVelocity;

  // propagate velocity and pressure
  propagateVelocityPress(inputVelocity, inputPressure, freq, 0, lastSec, timeExp);

  // count time for propagation
  auto end = std::chrono::system_clock::now();
  time += end - start;
}

// **************************************************************************
// Precompute parameters for transfer function computation

void Acoustic3dSimulation::precomputationsForTf()
{
  m_freqSteps = (double)SAMPLING_RATE / 2. / (double)m_numFreq;
  m_numFreqComputed = (int)ceil(m_simuParams.maxComputedFreq / m_freqSteps);

  // copy the number of frequencies for display
  m_numFreqPicture = m_numFreq;

  // activate computation of radiated field since in most of the 
  // cases the reception point of the transfer functions is outside
  m_simuParams.computeRadiatedField = true;

  generateLogFileHeader(false);

  // compute the coordinates of the transfer function point
  m_tfPoints = movePointFromExitLandmarkToGeoLandmark(m_simuParams.tfPoint);

  // resize the frequency vector
  m_tfFreqs.clear();
  m_tfFreqs.reserve(m_numFreqComputed);

  // resize the transfer function matrix
  m_glottalSourceTF.resize(m_numFreqComputed, m_simuParams.tfPoint.size());
  m_noiseSourceTF.resize(m_numFreqComputed, m_simuParams.tfPoint.size());

  // resize the plane mode input impedance vector
  m_planeModeInputImpedance.resize(m_numFreqComputed, 1);

  m_oldSimuParams = m_simuParams;
}

// **************************************************************************

void Acoustic3dSimulation::solveWaveProblemNoiseSrc(bool &needToExtractMatrixF, Matrix &F,
  double freq, std::chrono::duration<double>* time)
{
  //Matrix F;
  Eigen::MatrixXcd inputPressureNoise, upStreamImpAdm, radImped, radAdmit,
    prevPress, prevVelo;
  int lastSec(m_crossSections.size() - 1);

  ofstream log("log.txt", ofstream::app);

  if (m_idxSecNoiseSource < lastSec)
  {
    if (needToExtractMatrixF)
    {
      // extract the junction matrix of the noise source section
      F = m_crossSections[m_idxSecNoiseSource]->getMatrixF()[0];

      needToExtractMatrixF = false;
      log << "Matrix F extracted" << endl;
    }

    // generate mode amplitude matrices for the secondary source
    inputPressureNoise.setZero(
      m_crossSections[m_idxSecNoiseSource]->numberOfModes(), 1);
    inputPressureNoise(0, 0) = complex<double>(1., 0.);

    // save the input impedance of the upstream part
    // if the section expends
    if ((pow(m_crossSections[m_idxSecNoiseSource + 1]->scaleIn(), 2) *
      m_crossSections[m_idxSecNoiseSource + 1]->area()) >
      (pow(m_crossSections[m_idxSecNoiseSource]->scaleOut(), 2) *
        m_crossSections[m_idxSecNoiseSource]->area()))
    {
      upStreamImpAdm = m_crossSections[m_idxSecNoiseSource]->Zout();
    }
    // if the section contracts
    else
    {
      upStreamImpAdm = m_crossSections[m_idxSecNoiseSource]->Yout();
    }

    // set glottis boundary condition
    switch (m_glottisBoundaryCond)
    {
    case HARD_WALL:
    {
      int mn(m_crossSections[0]->numberOfModes());
      radImped.setZero(mn, mn);
      radImped.diagonal().setConstant(100000.);
      radAdmit.setZero(mn, mn);
      radAdmit.diagonal().setConstant(1. / 100000.);
      break;
    }
    case IFINITE_WAVGUIDE:
    {
      m_crossSections[0]->characteristicImpedance(radImped, freq, m_simuParams);
      m_crossSections[0]->characteristicAdmittance(radAdmit, freq, m_simuParams);
      break;
    }
    }

    // propagate impedance and admittance from the glottis to the location
    // of the second sound source
    propagateImpedAdmit(radImped, radAdmit, freq, 0, m_idxSecNoiseSource, time);

    // compute the pressure and the velocity at the entrance of the next section
    // if the section expends
    if ((pow(m_crossSections[m_idxSecNoiseSource + 1]->scaleIn(), 2) *
      m_crossSections[m_idxSecNoiseSource + 1]->area()) >
      (pow(m_crossSections[m_idxSecNoiseSource]->scaleOut(), 2) *
        m_crossSections[m_idxSecNoiseSource]->area()))
    {
      prevVelo = (F.transpose()) * ((freq * upStreamImpAdm + freq *
        m_crossSections[m_idxSecNoiseSource]->Zout()).householderQr()
        .solve(inputPressureNoise));
      prevPress = freq *
        m_crossSections[m_idxSecNoiseSource + 1]->Zin() * prevVelo;
    }
    // if the section contracts
    else
    {
      prevPress = (F.transpose()) * ((upStreamImpAdm +
        m_crossSections[m_idxSecNoiseSource]->Yout()).householderQr()
        .solve(-m_crossSections[m_idxSecNoiseSource]->Yout() *
          inputPressureNoise));

      prevVelo =
        m_crossSections[m_idxSecNoiseSource + 1]->Yin() * prevPress;
    }

    // propagate the pressure and the velocity in the upstream part
    propagateVelocityPress(prevVelo, prevPress, freq,
      min(m_idxSecNoiseSource + 1, lastSec), lastSec, time);
  }
  log.close();
}

// **************************************************************************

void Acoustic3dSimulation::computeGlottalTf(int idxFreq, double freq)
{
  m_simuParams.freqField = freq;
  m_glottalSourceTF.row(idxFreq) = acousticField(m_tfPoints);
  m_planeModeInputImpedance(idxFreq, 0) = m_crossSections[0]->Zin()(0, 0);
  m_tfFreqs.push_back(freq);
}

// **************************************************************************

void Acoustic3dSimulation::computeNoiseSrcTf(int idxFreq)
{
  m_noiseSourceTF.row(idxFreq) = acousticField(m_tfPoints);
}

// **************************************************************************

void Acoustic3dSimulation::generateSpectraForSynthesis(int tfIdx)
{
  spectrum.reset(2 * m_numFreq);
  spectrumNoise.reset(2 * m_numFreq);

  for (int i(0); i < m_numFreqComputed; i++)
  {
    spectrum.setValue(i, m_glottalSourceTF(i, tfIdx));
    spectrumNoise.setValue(i, m_noiseSourceTF(i, tfIdx));
  }

  for (int i(m_numFreq); i < 2 * m_numFreq; i++)
  {
    spectrum.re[i] = spectrum.re[2 * m_numFreq - i - 1];
    spectrum.im[i] = -spectrum.im[2 * m_numFreq - i - 1];
    spectrumNoise.re[i] = spectrumNoise.re[2 * m_numFreq - i - 1];
    spectrumNoise.im[i] = -spectrumNoise.im[2 * m_numFreq - i - 1];
  }
}

// **************************************************************************
// Compute the transfer function(s)

void Acoustic3dSimulation::computeTransferFunction(VocalTract* tract)
{
  double freq;
  Matrix F;

  bool needToExtractMatrixF(true);

  ofstream log("log.txt", ofstream::app);

  // for time tracking
  auto startTot = std::chrono::system_clock::now();
  auto start = std::chrono::system_clock::now();
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> timePropa(0.), timeComputeField(0.), timeExp(0.), time;

  precomputationsForTf();

  // loop over frequencies
  for (int i(0); i < m_numFreqComputed; i++)
  {
    freq = max(0.1, (double)i * m_freqSteps);
    log << "frequency " << i + 1 << "/" << m_numFreqComputed << " f = " << freq
      << " Hz" << endl;

    solveWaveProblem(tract, freq, true, timePropa, &timeExp);

    //*****************************************************************************
    //  Compute acoustic pressure 
    //*****************************************************************************

    start = std::chrono::system_clock::now();

    computeGlottalTf(i, freq);

    end = std::chrono::system_clock::now();
    timeComputeField += end - start;

    //*****************************************************************************
    //  Compute transfer function of the noise source
    //*****************************************************************************

    solveWaveProblemNoiseSrc(needToExtractMatrixF, F, freq, &time);

    if (m_idxSecNoiseSource < m_crossSections.size() - 1)
    {
      computeNoiseSrcTf(i);
    }
  }

  log << "\nTime propagation: " << timePropa.count() << endl;

  // generate spectra values for negative frequencies
  generateSpectraForSynthesis(0);

  // Export plane mode input impedance
  ofstream prop;
  prop.open("zin.txt");
  for (int i(0); i < m_numFreqComputed; i++)
  {
    prop << m_tfFreqs[i] << "  "
      << abs(1i * 2. * M_PI * m_tfFreqs[i] * m_simuParams.volumicMass *
        m_planeModeInputImpedance(i, 0)) << "  "
      << arg(1i * 2. * M_PI * m_tfFreqs[i] * m_simuParams.volumicMass *
        m_planeModeInputImpedance(i, 0)) << endl;
  }
  prop.close();

  end = std::chrono::system_clock::now();
  time = end - startTot;
  log << "\nTransfer function time (sec): " << time.count() << endl;
  log << "Time acoustic pressure computation: " << timeComputeField.count() << endl;
  log << "Time matrix exponential: " << timeExp.count() << endl;

  // print total time in HMS
  int hours(floor(time.count() / 3600.));
  int minutes(floor((time.count() - hours * 3600.) / 60.));
  double seconds(time.count() - (double)hours * 3600. -
    (double)minutes * 60.);
  log << "\nTransfer function time "
    << hours << " h " << minutes << " m " << seconds << " s" << endl;

  log.close();
}

// ****************************************************************************
// Run a simulation at a specific frequency and compute the aoustic field 

void Acoustic3dSimulation::computeAcousticField(VocalTract* tract)
{
  double freq(m_simuParams.freqField);

  generateLogFileHeader(true);
  ofstream log("log.txt", ofstream::app);

  // for time tracking
  auto start = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds, timeExp;

  solveWaveProblem(tract, freq, false, elapsed_seconds, &timeExp);

  acousticFieldInPlane();

  auto end = std::chrono::system_clock::now();
  elapsed_seconds = end - start;
  int hours(floor(elapsed_seconds.count() / 3600.));
  int minutes(floor((elapsed_seconds.count() - hours * 3600.) / 60.));
  double seconds(elapsed_seconds.count() - (double)hours * 3600. -
    (double)minutes * 60.);
  log << "\nAcoustic field computation time "
    << hours << " h " << minutes << " m " << seconds << " s" << endl;

  log.close();
}

// ****************************************************************************
// Run a simulation for a concatenation of cylinders

void Acoustic3dSimulation::coneConcatenationSimulation(string fileName)
{
  // for data extraction
  double endAdmit, freqField;
  vector<int> vIdx;
  vector<double> rads, shifts, scaleIn, scaleOut, lengths, curvAngles;
  ifstream ifs;
  string line, str;
  char separator(';');
  stringstream strSt;

  // for cross-section creation
  Polygon_2 contour;
  int nbAngles(100), nbSec;
  double angle, area, length, inAngle, inRadius, maxRad;
  double scalingFactors[2];
  vector<int> surfaceIdx(nbAngles, 0);

  // for solving the wave problem 
  bool reverse(false);
  int mn;
  double freq, freqMax, x, y, totalLength;
  Eigen::MatrixXcd radImped, radAdmit, inputVelocity, inputPressure;
  complex<double> pout, vout, yin;
  string::size_type idxStr;
  ofstream ofs, ofs2, ofs3, ofs4, ofs5, ofs6;
  Point ptOut;
  Point_3 pointComputeField;

  // for tracking time
  std::chrono::duration<double> time;

  m_geometryImported = true; // to have the good bounding box for modes plot
  //m_simuParams.sndSpeed = 34400;

  generateLogFileHeader(true);
  ofstream log("log.txt", ofstream::app);
  log << "\nStart cylinder concatenation simulation" << endl;
  if (reverse) { log << "Propagation direction reversed" << endl; }
  log << "Geometry from file " << fileName << endl;

  //***************************
  // load geometry parameters
  //***************************

  ifs.open(fileName);
  if (!ifs.is_open())
  {
    log << "failed to opened parameters file" << endl;
  }
  else
  {
    // extract number of modes
    vIdx.clear();
    getline(ifs, line); // to remove comment line
    getline(ifs, line);
    strSt.clear();
    strSt.str(line);
    while (getline(strSt, str, separator))
    {
      vIdx.push_back(stoi(str));
    }

    // extract radius
    rads.clear();
    getline(ifs, line); // to remove comment line
    getline(ifs, line);
    strSt.clear();
    strSt.str(line);
    while (getline(strSt, str, separator))
    {
      rads.push_back(stod(str));
    }

    // extract contour shift
    shifts.clear();
    getline(ifs, line); // to remove comment line
    getline(ifs, line);
    strSt.clear();
    strSt.str(line);
    while (getline(strSt, str, separator))
    {
      shifts.push_back(stod(str));
    }

    // extract scaling in
    scaleIn.clear();
    getline(ifs, line); // to remove comment line
    getline(ifs, line);
    strSt.clear();
    strSt.str(line);
    while (getline(strSt, str, separator))
    {
      scaleIn.push_back(stod(str));
    }

    // extract scaling out
    scaleOut.clear();
    getline(ifs, line); // to remove comment line
    getline(ifs, line);
    strSt.clear();
    strSt.str(line);
    while (getline(strSt, str, separator))
    {
      scaleOut.push_back(stod(str));
    }

    // extract length
    lengths.clear();
    getline(ifs, line); // to remove comment line
    getline(ifs, line);
    strSt.clear();
    strSt.str(line);
    while (getline(strSt, str, separator))
    {
      lengths.push_back(stod(str));
    }

    // extract curvature angle
    curvAngles.clear();
    getline(ifs, line); // to remove comment line
    getline(ifs, line);
    strSt.clear();
    strSt.str(line);
    while (getline(strSt, str, separator))
    {
      curvAngles.push_back(stod(str));
    }

    // extract end admittance
    getline(ifs, line); // to remove comment line
    getline(ifs, line);
    strSt.clear();
    strSt.str(line);
    getline(strSt, str, separator);
    endAdmit =  stod(str); // * pow(scaleOut[1], 2); // open end admittance
    getline(strSt, str, separator);
    // wall admittance
    m_simuParams.thermalBndSpecAdm = complex<double>(stod(str), 0.);

    // extract index frequency field extraction
    getline(ifs, line); // to remove comment line
    getline(ifs, line);
    freqField = stod(line);

  }

  log << "Geometry parameters extracted" << endl;

  //***********************
  // Create cross-sections
  //***********************

  maxRad = 0.;
  m_crossSections.clear();
  for (int s(0); s < vIdx.size(); s++)
  { 
    maxRad = max(maxRad, rads[s]);
    // Generate a circular contour
    contour.clear();
    for (int i(0); i < nbAngles; i++)
    {
      angle = 2. * M_PI * (double)(i) / (double)(nbAngles);
      contour.push_back(Point(rads[s] * cos(angle), rads[s] * (sin(angle) + shifts[s])));
    }

    area = pow(rads[s], 2) * M_PI;
    scalingFactors[0] = scaleIn[s];
    scalingFactors[1] = scaleOut[s];
    length = lengths[s];
    inAngle = curvAngles[s];
    inRadius = length / inAngle;
    addCrossSectionFEM(area, sqrt(area) / m_meshDensity, contour,
      surfaceIdx, length, Point2D(0., 0.), Point2D(0., 1.),
      scalingFactors);
    m_crossSections[s]->setCurvatureRadius(inRadius);
    m_crossSections[s]->setCurvatureAngle(inAngle);
    m_crossSections[s]->setModesNumber(vIdx[s]);
    if (s > 0){m_crossSections[s]->setPreviousSection(s - 1);}
    if (s < vIdx.size() -1) {m_crossSections[s]->setNextSection(s+1);}

    log << "Section " << s << " created" << endl;
  }
  nbSec = m_crossSections.size();
  // define bounding box for modes and mesh display
  m_maxCSBoundingBox.first = Point2D(-2. * maxRad, -2. * maxRad);
  m_maxCSBoundingBox.second = Point2D(2. * maxRad, 2. * maxRad);

  log << nbSec << " sections created" << endl;

  // Export cross-sections parameters
  for (int i(0); i < nbSec; i++)
  {
    log << "\nSection " << i << endl;
    log << *m_crossSections[i] << endl;
  }

  //*********************
  // solve wave problem
  //*********************

  computeMeshAndModes();
  log << "Modes computed" << endl;

  computeJunctionMatrices(false);
  log << "Junctions computed" << endl;

  // initialize input pressure and velocity vectors
  mn = m_crossSections[0]->numberOfModes();
  inputPressure.setZero(mn, 1);
  inputVelocity.setZero(mn, 1);

  freqMax = 10000.;
  m_numFreq = 501;
  idxStr = fileName.find_last_of("/\\");
  str = fileName.substr(0, idxStr + 1) + "tfMM.txt"; 
  ofs.open(str);
  // define the coordinate of the point where the acoustic field is computed
  // for transfer fucntion computation
  if (reverse) { ptOut = Point(0., shifts[0] * rads[0]); }
  else { ptOut = Point(0., shifts.back() * rads.back()); }
  log << "Point for transfer function computation " << ptOut << endl;
  for (int i(0); i < m_numFreq; i++)
  {
    freq = max(0.1, freqMax * (double)i / (double)(m_numFreq - 1));
    log << "f = " << freq << " Hz" << endl;

    if (reverse)
    {
      radAdmit.setZero(vIdx[0], vIdx[0]);
      radAdmit.diagonal() = Eigen::VectorXcd::Constant(vIdx[0], 
        complex<double>(pow(m_crossSections[0]->scaleIn(), 2) * endAdmit, 0.));
    }
    else
    {
      radAdmit.setZero(vIdx.back(), vIdx.back());
      radAdmit.diagonal() = Eigen::VectorXcd::Constant(vIdx.back(), 
        complex<double>(pow(m_crossSections[nbSec - 1]->scaleOut(), 2) * endAdmit, 0.));
    }
    radImped = radAdmit.inverse();

    if (m_simuParams.propMethod == STRAIGHT_TUBES)
    {
      radImped *= -1i * 2. * M_PI * freq * m_simuParams.volumicMass
        / m_crossSections[nbSec - 1]->area();
      radAdmit /= -1i * 2. * M_PI * freq * m_simuParams.volumicMass
        / m_crossSections[nbSec - 1]->area();
    }

    if (reverse) { propagateImpedAdmit(radImped, radAdmit, freq, 0, nbSec - 1, &time, 1); }
    else { propagateImpedAdmit(radImped, radAdmit, freq, nbSec - 1, 0, &time , -1); }

    if (reverse)
    {
      inputVelocity(0, 0) = -1i * 2. * M_PI * freq * m_simuParams.volumicMass
        * pow(m_crossSections[nbSec - 1]->scaleOut(), 2)
        * sqrt(m_crossSections[nbSec - 1]->area());
      inputPressure = m_crossSections[nbSec - 1]->Zout() * inputVelocity;
      propagateVelocityPress(inputVelocity, inputPressure, freq, nbSec - 1, 0, &time, -1);
      // compute the acoustic pressure and the particle velocity at the 
      // center of the exit surface
      pout = m_crossSections[0]->pin(ptOut); // reversed order
      vout = -m_crossSections[0]->qin(ptOut)
        / 1i / 2. / M_PI / freq / m_simuParams.volumicMass;
      yin = m_crossSections[0]->interiorField(
        Point_3(m_crossSections[0]->length(), ptOut.x(), ptOut.y()), 
        m_simuParams, ADMITTANCE);
    }
    else
    {
      inputVelocity(0, 0) = -1i * 2. * M_PI * freq * m_simuParams.volumicMass
        * pow(m_crossSections[0]->scaleIn(), 2)
        * sqrt(m_crossSections[0]->area());
      inputPressure = m_crossSections[0]->Zin() * inputVelocity;
      propagateVelocityPress(inputVelocity, inputPressure, freq, 0, nbSec - 1, &time, 1);
      // compute the acoustic pressure and the particle velocity at the 
      // center of the exit surface
      pout = m_crossSections[nbSec - 1]->pout(ptOut);
      vout = -m_crossSections[nbSec - 1]->qout(ptOut)
        / 1i / 2. / M_PI / freq / m_simuParams.volumicMass;
      yin = m_crossSections[nbSec - 1]->interiorField(
        Point_3(0., ptOut.x(), ptOut.y()), m_simuParams, ADMITTANCE);

    }

    // write result in a text file
    ofs << freq << "  "
      << "  " << abs(vout) // modulus of particle velocity
      << "  " << arg(vout) // phase of particle velocity
      << "  " << abs(pout) // modulus of acoustic pressure
      << "  " << arg(pout) // phase of acoustic pressure
      << "  " << abs(yin)  // modulus of the input impedance
      << "  " << arg(yin)  // phase of the input impedance
      << endl;
  }
  ofs.close();

  //******************************************************
  // Extract the acoustic field at a specified frequency
  //******************************************************

  if (freqField > 0.)
  {
    log << "Compute acoustic field at the frequency " << freqField << endl;

    freq = freqField;

    if (reverse)
    {
      radAdmit.setZero(vIdx[0], vIdx[0]);
      radAdmit.diagonal() = Eigen::VectorXcd::Constant(vIdx[0], complex<double>(
        pow(m_crossSections[0]->scaleIn(), 2) * endAdmit, 0.));
    }
    else
    {
      radAdmit.setZero(vIdx.back(), vIdx.back());
      radAdmit.diagonal() = Eigen::VectorXcd::Constant(vIdx.back(), complex<double>(
        pow(m_crossSections[nbSec - 1]->scaleOut(), 2) * endAdmit, 0.));
    }
    radImped = radAdmit.inverse();

    if (m_simuParams.propMethod == STRAIGHT_TUBES)
    {
      radImped *= -1i * 2. * M_PI * freq * m_simuParams.volumicMass
        / m_crossSections[nbSec - 1]->area();
      radAdmit /= -1i * 2. * M_PI * freq * m_simuParams.volumicMass
        / m_crossSections[nbSec - 1]->area();
    }

    if (reverse) { propagateImpedAdmit(radImped, radAdmit, freq, 0, nbSec - 1, &time, 1); }
    else { propagateImpedAdmit(radImped, radAdmit, freq, nbSec - 1, 0, &time, -1); }

    log << "Impedance propagated" << endl;

    if (reverse)
    {
      inputVelocity(0, 0) = -1i * 2. * M_PI * freq * m_simuParams.volumicMass
        * pow(m_crossSections[nbSec - 1]->scaleOut(), 2)
        * sqrt(m_crossSections[nbSec - 1]->area());
      inputPressure = m_crossSections[nbSec - 1]->Zout() * inputVelocity;
      propagateVelocityPress(inputVelocity, inputPressure, freq, nbSec - 1, 0, &time, -1);
    }
    else
    {
      inputVelocity(0, 0) = -1i * 2. * M_PI * freq * m_simuParams.volumicMass
        * pow(m_crossSections[0]->scaleIn(), 2)
        * sqrt(m_crossSections[0]->area());
      inputPressure = m_crossSections[0]->Zin() * inputVelocity;
      propagateVelocityPress(inputVelocity, inputPressure, freq, 0, nbSec - 1, &time, 1);
    }
    log << "Velocity and pressure propagated" << endl;

    // export the field in text files
    str = fileName.substr(0, idxStr + 1) + "q.txt"; 
    ofs2.open(str);
    str = fileName.substr(0, idxStr + 1) + "Y.txt"; 
    ofs3.open(str);
    str = fileName.substr(0, idxStr + 1) + "p.txt"; 
    ofs4.open(str);
    str = fileName.substr(0, idxStr + 1) + "cx.txt";
    ofs5.open(str);
    str = fileName.substr(0, idxStr + 1) + "cy.txt";
    ofs6.open(str);
    int cnt(0);
    totalLength = 0.;
    for (int s(0); s < nbSec; s++)
    {
      if (m_crossSections[s]->length() > 0.)
      {
        cnt = 0;
        for (int px(0); px < 100; px++)
        {
          for (int pz(0); pz < 51; pz++)
          {
            x = lengths[s] * (double)(px) / 99.;
            ofs5 << x + totalLength << "  ";
            y = rads[s] * (2. * (double)(pz) / 50. - 1.);
            ofs6 << y * m_crossSections[s]->scaling((double)(px) / 99.) << "  ";
            pointComputeField = Point_3(x, 0., y);
            ofs3 << abs(m_crossSections[s]->interiorField(pointComputeField, 
                  m_simuParams, ADMITTANCE)) << "  ";
            ofs2 << abs(m_crossSections[s]->q(pointComputeField, m_simuParams)) << "  ";
            ofs4 << abs(m_crossSections[s]->p(pointComputeField, m_simuParams)) << "  ";
            cnt++;
            log << "Point " << cnt << " over " << 100 * 51 << " " << pointComputeField << endl;
          }
          ofs2 << endl;
          ofs3 << endl;
          ofs4 << endl;
          ofs5 << endl;
          ofs6 << endl;
        }
        totalLength += m_crossSections[s]->length();
      }
    }
    ofs4.close();
    ofs3.close();
    ofs2.close();
  }

  log.close();
}

// ****************************************************************************
// Run specific simulation tests

void Acoustic3dSimulation::runTest(enum testType tType, string fileName)
{
  ofstream ofs, ofs2, ofs3, ofs4;
  ifstream ifs;
  string line, str;
  char separator(';');
  stringstream strs, txtField;
  ofstream log("log.txt", ofstream::app);
  log << "\nStart test" << endl;

  Polygon_2 contour;
  Point_3 pointComputeField;
  double radius, angle, area, length, inRadius, inAngle;
  int nbAngles(100);
  vector<int> surfaceIdx(nbAngles, 0), slectedModesIdx;
  double scalingFactors[2] = { 1., 1. };
  double a(5.5), b(3.2);
  double freq, freqMax;
  complex<double> result;
  int nbFreqs, mn;
  Eigen::MatrixXcd characImped, radImped, radAdmit, inputVelocity, inputPressure, field;
  vector<Point_3> radPts;
  vector<array<double, 2>> pts;
  vector<array<int, 3>> triangles;
  Eigen::VectorXcd radPress;
  vector<Eigen::MatrixXcd> Q;
  vector<Matrix> KR2;
  // to determine the rectangle mode indexes
  vector<array<int, 2>> modeIdxs;
  vector<int> vIdx;
  vector<double> k2, rads, shifts, scaleIn, scaleOut, lengths, curvAngles;
  int nCombinations, ie, je, me, ne;
  Matrix analyticE, E, modes;
  double E1y, E1z, E2y, E2z;

  auto start = std::chrono::system_clock::now();
  auto stop = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsedTime;
  int hours;
  int minutes;
  double seconds;

  switch(tType){
  case MATRIX_E:
    /////////////////////////////////////////////////////////////////////////////////
    //*********************************************
    // Matrix E 
    //*********************************************

    log << "Start matrix E test" << endl;

    // create contour
    contour.push_back(Point(0., 0.));
    contour.push_back(Point(a, 0.));
    contour.push_back(Point(a, b));
    contour.push_back(Point(0., b));
    m_maxCSBoundingBox.first = Point2D(-1.2*a, -1.2*a);
    m_maxCSBoundingBox.second = Point2D(1.2*a, 1.2*a);
    m_geometryImported = true; // to have the good bounding box for modes plot

    m_crossSections.clear();
    area = a*b;
    length = 20.;
    surfaceIdx.resize(4);
    addCrossSectionFEM(area, sqrt(area) / m_meshDensity, contour,
      surfaceIdx, length, Point2D(0., 0.), Point2D(0., 1.),
      scalingFactors);

    m_crossSections[0]->buildMesh();

    //*******************
    // export mesh
    //*******************

    // export points
    strs << "points_rec_" << m_meshDensity << ".txt";
    ofs.open(strs.str());
    pts = m_crossSections[0]->getPoints();
    for (auto it : pts)
    {
      ofs << it[0] << "  " << it[1] << endl;
    }
    ofs.close();

    // export triangles
    strs.str("");
    strs << "triangles_rec_" << m_meshDensity << ".txt";
    ofs.open(strs.str());
    triangles = m_crossSections[0]->getTriangles();
    for (auto it : triangles)
    {
      for (int i(0); i < 3; i++) { ofs << it[i] + 1 << "  "; }
      ofs << endl;
    }
    ofs.close();

    m_crossSections[0]->computeModes(m_simuParams);

    //***************************************
    // Export modes and propagation matrices
    //***************************************

    modes = m_crossSections[0]->getModes();
    strs.str("");
    strs << "modes_rec_" << m_meshDensity << ".txt";
    ofs.open(strs.str());
    ofs << modes << endl;
    ofs.close();

    strs.str("");
    strs << "eigen_freqs_rec_" << m_meshDensity << ".txt";
    ofs.open(strs.str());
    for (int i(0); i < 100; i++)
    {
      ofs << m_crossSections[0]->eigenFrequency(i) << endl;
    }
    ofs.close();

    modes = m_crossSections[0]->getMatrixC();
    strs.str("");
    strs << "C_rec_" << m_meshDensity << ".txt";
    ofs.open(strs.str());
    ofs << modes << endl;
    ofs.close();

    modes = m_crossSections[0]->getMatrixD();
    strs.str("");
    strs << "D_rec_" << m_meshDensity << ".txt";
    ofs.open(strs.str());
    ofs << modes << endl;
    ofs.close();

    modes = m_crossSections[0]->getMatrixE();
    strs.str("");
    strs << "E_rec_" << m_meshDensity << ".txt";
    ofs.open(strs.str());
    ofs << modes << endl;
    ofs.close();

    modes = m_crossSections[0]->getMatrixKR2(0);
    strs.str("");
    strs << "KR2_rec_" << m_meshDensity << ".txt";
    ofs.open(strs.str());
    ofs << modes << endl;
    ofs.close();

    //**********************************************
    // Compute matrix E from analytical expression
    //**********************************************

    // generate modes indexes
    mn = m_crossSections[0]->numberOfModes();
    nCombinations = 10000;
    modeIdxs.reserve(nCombinations);
    k2.reserve(nCombinations);
    vIdx.resize(nCombinations);
    iota(vIdx.begin(), vIdx.end(), 0);
    for (int m(0); m < 100; m++)
    {
      for (int n(0); n < 100; n++)
      {
        k2.push_back(pow((double)m * b / a, 2) + pow((double)n, 2));
        modeIdxs.push_back({ m,n });
      }
    }
    sort(vIdx.begin(), vIdx.end(), [&](int i, int j){return k2[i]<k2[j];});

    analyticE.setZero(mn, mn);
    for (int m(0); m < mn; m++)
    {
      for (int n(0); n < mn; n++)
      {
        // extract the indexes of the modes
        ie = modeIdxs[vIdx[m]][0];
        je = modeIdxs[vIdx[m]][1];
        me = modeIdxs[vIdx[n]][0];
        ne = modeIdxs[vIdx[n]][1];

        // compute E1y
        if (me == 0) {E1y = 0.;}
        if ((ie == 0) && (me != 0)) {E1y = sqrt(2.)*cos((double)me*M_PI);}
        if ((ie == me) && (me != 0)) {E1y = 0.5;}
        if ((ie != me) && (ie != 0) && (me != 0)){E1y = (double)me*(
          cos((double)(ie + me)*M_PI)/(double)(ie + me) -
          cos((double)(ie - me)*M_PI)/(double)(ie - me));}

        // compute E1z
        if (je == ne) {E1z = 1.;} else {E1z = 0.;}

        // compute E2y
        if (ie == me){E2y = 1.;} else {E2y = 0.;}

        // compute E2z
        if (ne == 0) {E2z = 0.;}
        if ((je == 0) && (ne != 0)) {E2z = sqrt(2.)*cos((double)(ne)*M_PI);}
        if ((je == ne) && (ne != 0)) {E2z = 0.5;}
        if ((je != ne) && (je != 0) && (ne != 0)) {E2z = (double)ne*(
          cos((double)(je + ne)*M_PI)/(double)(je + ne) -
          cos((double)(je - ne)*M_PI)/(double)(je - ne));} 
        
        // Compute E(m,n)
        analyticE(m,n) = E1y*E1z + E2y*E2z;
      }
    }

    ofs.open("anE.txt");
    ofs << analyticE << endl;
    ofs.close();
    ofs.open("nuE.txt");
    ofs << m_crossSections[0]->getMatrixE() << endl;
    ofs.close();

    log << "Test matrix E finished" << endl;

    break;
    /////////////////////////////////////////////////////////////////////////////////
  case DISCONTINUITY:

    //*********************************************
    // Discontinuity
    //*********************************************
  
    // Set the proper simulation parameters
    m_simuParams.numIntegrationStep = 165;
    m_meshDensity = 20.;
    m_simuParams.percentageLosses = 0.;
    m_simuParams.wallLosses = false;
    m_simuParams.curved = false;
    m_simuParams.maxCutOnFreq = 30000.;
    m_geometryImported = true; // to have the good bounding box for modes plot
    m_simuParams.sndSpeed = 34400;

    generateLogFileHeader(true);
  
    // Generate a circular contour
    radius = 4.;
    m_maxCSBoundingBox.first = Point2D(-radius, -radius);
    m_maxCSBoundingBox.second = Point2D(radius, radius);
    for (int i(0); i < nbAngles; i++)
    {
      angle = 2.*M_PI*(double)(i) / (double)(nbAngles);
      contour.push_back(Point(radius * cos(angle), radius * sin(angle)));
    }
  
    m_crossSections.clear();
    area = M_PI*pow(radius, 2);
    length = 30.;
    addCrossSectionFEM(area, sqrt(area) / m_meshDensity, contour,
      surfaceIdx, length, Point2D(0., 0.), Point2D(0., 1.),
      scalingFactors);
    m_crossSections[0]->setAreaVariationProfileType(GAUSSIAN);
  
    log << "Cross-section created" << endl;
  
    // Check the scaling factor
    ofs.open("sc.txt");
    for (int i(0); i < nbAngles; i++)
    {
      ofs << m_crossSections[0]->scaling((double)(i) / (double)(nbAngles - 1))
        << endl;
    }
    ofs.close();
  
    // Check the scaling factor derivative
    ofs.open("dsc.txt");
    for (int i(0); i < nbAngles; i++)
    {
      ofs << m_crossSections[0]->scalingDerivative((double)(i) / (double)(nbAngles - 1))
        << endl;
    }
    ofs.close();
  
    log << "Parameters set" << endl;
  
    // compute propagation modes
    m_crossSections[0]->buildMesh();
  
    log << "Mesh generated" << endl;
    //// extract mesh
    //CDT tr(m_crossSections[0]->triangulation());
    //ofs.open("mesh.txt");
    //for (auto it = tr.all_faces_begin(); it != tr.all_faces_end(); it++)
    //{
    //  for (int i(0); i < 3; i++)
    //  {
    //    ofs << it->vertex(i)->point().x() << "  "
    //  {
    //    ofs << it->vertex(i)->point().x() << "  "
    //      << it->vertex(i)->point().y() << endl;
    //  }
    //  ofs << "nan  nan" << endl;
    //}
    //ofs.close();
  
    m_crossSections[0]->computeModes(m_simuParams);
    log << m_crossSections[0]->numberOfModes() << " modes computed" << endl;

    slectedModesIdx.push_back(0);
    slectedModesIdx.push_back(5);
    slectedModesIdx.push_back(16);
    slectedModesIdx.push_back(31);
    slectedModesIdx.push_back(52);
    slectedModesIdx.push_back(106);

    m_crossSections[0]->selectModes(slectedModesIdx);

    // Extract value of the 1st mode
    E = m_crossSections[0]->getModes();
    log << "1st mode: " << E(0, 0) << endl;

    preComputeRadiationMatrices(16, 0);
  
    freqMax = 2500.;
    nbFreqs = 1500;
    ofs.open("imp.txt");
    ofs2.open("freqs.txt");
    ofs3.open("rad.txt");
    for (int i(0); i < nbFreqs; i++)
    {
      // get the output impedance
      freq = max(0.1, freqMax*(double)i/(double)(nbFreqs - 1));
      log << "f = " << freq << " Hz" << endl;
      
      interpolateRadiationImpedance(radImped, freq, 0);
      ofs3 << radImped.imag() << endl;
  
      // Propagate impedance
      m_crossSections[0]->propagateMagnus(radImped, m_simuParams, freq, -1., IMPEDANCE, &elapsedTime);
  
      ofs2 << freq << endl;
      ofs << m_crossSections[0]->Zin().cwiseAbs() << endl;
    }
    ofs3.close();
    ofs2.close();
    ofs.close();
    
    break;
    /////////////////////////////////////////////////////////////////////////////////
    case ELEPHANT_TRUNK:
    
    //*********************************************
    // Elephant trunk 
    //*********************************************

      start = std::chrono::system_clock::now();
    
    // Set the proper simulation parameters
    m_simuParams.viscoThermalLosses = false;
    m_simuParams.maxComputedFreq = 10000;
    m_simuParams.curved = true;
    m_geometryImported = true; // to have the good bounding box for modes plot

    generateLogFileHeader(true);

    // Generate a circular contour
    radius = 3.;
    m_maxCSBoundingBox.first = Point2D(-2.*radius, -2.*radius);
    m_maxCSBoundingBox.second = Point2D(2.*radius, 2.*radius);
    for (int i(0); i < nbAngles; i++)
    {
      angle = 2.*M_PI*(double)(i) / (double)(nbAngles);
      contour.push_back(Point(radius * cos(angle), radius * sin(angle)));
    }

    // create the cross-section
    m_crossSections.clear();
    area = pow(radius, 2) * M_PI;
    scalingFactors[0] = 0.25;
    scalingFactors[1] = 1.;
    inRadius = 1.25*4.*1.5;
    log << "inRadius " << inRadius << endl;
    inAngle = 2.26;
    length = inAngle * inRadius;
    log << "length: " << length << endl;
    addCrossSectionFEM(area, sqrt(area) / m_meshDensity, contour,
      surfaceIdx, length, Point2D(0., 0.), Point2D(-1., 0.),
      scalingFactors);
    m_crossSections[0]->setAreaVariationProfileType(ELEPHANT);
    m_crossSections[0]->setCurvatureRadius(-inRadius);
    m_crossSections[0]->setCurvatureAngle(inAngle);
    mn = min(100., m_simuParams.maxCutOnFreq);
    m_crossSections[0]->setModesNumber(mn);
  
    log << "Cross-section created" << endl;
    log << *m_crossSections[0] << endl;

    // Check the scaling factor
    ofs.open("sc.txt");
    for (int i(0); i < nbAngles; i++)
    {
      ofs << m_crossSections[0]->scaling((double)(i) / (double)(nbAngles - 1))
        << endl;
    }
    ofs.close();
  
    // Check the scaling factor derivative
    ofs.open("dsc.txt");
    for (int i(0); i < nbAngles; i++)
    {
      ofs << m_crossSections[0]->scalingDerivative((double)(i) / (double)(nbAngles - 1))
        << endl;
    }
    ofs.close();

    // compute propagation modes
    m_crossSections[0]->buildMesh();
    m_crossSections[0]->computeModes(m_simuParams);
    mn = m_crossSections[0]->numberOfModes();
    log << mn << " modes computed" << endl;

    // initialize input pressure and velocity vectors
    inputPressure.setZero(mn, 1);
    inputVelocity.setZero(mn, 1);

    // Open end boundary consition
    switch (m_mouthBoundaryCond)
    {
    case RADIATION:
      preComputeRadiationMatrices(16, 0);
      m_simuParams.computeRadiatedField = true;
      break;
    case ADMITTANCE_1:
      radAdmit.setZero(mn, mn);
      radAdmit.diagonal() = Eigen::VectorXcd::Constant(mn, complex<double>(1e15, 0.));
      radImped = radAdmit.inverse();
      break;
    }

    // define the position of the radiation point
    radPts.push_back(Point_3(3., 0., 0.));
    log << "tfPoint " << m_simuParams.tfPoint[0] << endl;
    pointComputeField = movePointFromExitLandmarkToGeoLandmark(m_simuParams.tfPoint[0]);
    log << "pointComputeField " << pointComputeField << endl;

    freqMax = m_simuParams.maxComputedFreq;
    ofs.open("press.txt");
    m_numFreq = 2001;
    for (int i(0); i < m_numFreq; i++)
    {
      freq = max(0.1, freqMax*(double)i/(double)(m_numFreq - 1));
      log << "f = " << freq << " Hz" << endl;

      if (m_mouthBoundaryCond == RADIATION) 
      {
        interpolateRadiationImpedance(radImped, freq, 0);
      }

      ofs2 << characImped.real() << endl;
      ofs3 << characImped.imag() << endl;

      //log << "Radiation impedance interpolated" << endl;
  
      switch (m_mouthBoundaryCond)
      {
      case RADIATION:
        m_crossSections[0]->propagateMagnus(radImped, m_simuParams, freq, -1., IMPEDANCE, &elapsedTime);
        break;
      case ADMITTANCE_1:
        m_crossSections[0]->propagateMagnus(radAdmit, m_simuParams, freq, -1., ADMITTANCE, &elapsedTime);
        break;
      }

      // propagate velocity or pressure
      inputVelocity(0, 0) = -1i * 2. * M_PI * freq * m_simuParams.volumicMass;

      switch (m_mouthBoundaryCond)
      {
      case RADIATION:
        m_crossSections[0]->propagateMagnus(inputVelocity, m_simuParams, freq, 1., VELOCITY, &elapsedTime);
        // compute radiated pressure
        RayleighSommerfeldIntegral(radPts, radPress, freq, 0);
        spectrum.setValue(i, radPress(0, 0));
        ofs << abs(1e5 * radPress(0) / 2. / M_PI) << "  "
          << arg(1e5 * radPress(0) / 2. / M_PI) << "  " << endl;
        break;
      case ADMITTANCE_1:
        inputPressure = m_crossSections[0]->Yin().inverse() * inputVelocity;
        m_crossSections[0]->propagateMagnus(inputPressure, m_simuParams, freq, 1., PRESSURE, &elapsedTime);
        result = m_crossSections[0]->area() * pow(m_crossSections[0]->scaleIn(), 2) * 1e5 *
          acousticField(pointComputeField);
        // export result
        ofs << freq << "  " << abs(result) << "  "
          << arg(result) << "  " << endl;
        break;
      }
    }
    ofs.close();

    //*************************
    // Compute acoustic field
    //*************************

    freq = m_simuParams.freqField;

    if (m_mouthBoundaryCond == RADIATION)
    {
      interpolateRadiationImpedance(radImped, freq, 0);
    }

    switch (m_mouthBoundaryCond)
    {
    case RADIATION:
      m_crossSections[0]->propagateMagnus(radImped, m_simuParams, freq, -1., IMPEDANCE, &elapsedTime);
      break;
    case ADMITTANCE_1:
      m_crossSections[0]->propagateMagnus(radAdmit, m_simuParams, freq, -1., ADMITTANCE, &elapsedTime);
      break;
    }

    inputVelocity(0, 0) = -1i * 2. * M_PI * freq * m_simuParams.volumicMass;

    switch (m_mouthBoundaryCond)
    {
    case RADIATION:
      m_crossSections[0]->propagateMagnus(inputVelocity, m_simuParams, freq, 1., VELOCITY, &elapsedTime);
      // compute radiated pressure
      break;
    case ADMITTANCE_1:
      inputPressure = m_crossSections[0]->Yin().inverse() * inputVelocity;
      m_crossSections[0]->propagateMagnus(inputPressure, m_simuParams, freq, 1., PRESSURE, &elapsedTime);
      break;
    }

    ofs2.open("field.txt");
    acousticFieldInPlane();
    txtField << m_field.cwiseAbs();
    ofs2 << regex_replace(txtField.str(), regex("-nan\\(ind\\)"), "nan");
    ofs2.close();

    //***********************************
    // get total time of the simulation
    stop = std::chrono::system_clock::now();
    elapsedTime = stop - start;
    hours = floor(elapsedTime.count() / 3600.);
    minutes = floor((elapsedTime.count() - hours * 3600.) / 60.);
    seconds = elapsedTime.count() - (double)hours * 3600. -
      (double)minutes * 60.;
    log << "\nTotal time "
      << hours << " h " << minutes << " m " << seconds << " s" << endl;
    ofs.close();

    break;
    /////////////////////////////////////////////////////////////////////////////////
    case SCALE_RAD_IMP:

    //*****************************************************
    // Test radiation impedance computation with scaling
    //*****************************************************

    // Generate reference radiation impedance
    //***************************************


      // Set the proper simulation parameters
      m_geometryImported = true; // to have the good bounding box for modes plot

      generateLogFileHeader(true);
      log << "Start test scale rad imped" << endl;

      // Generate a circular contour
      radius = 3.;
      m_maxCSBoundingBox.first = Point2D(-2. * radius, -2. * radius);
      m_maxCSBoundingBox.second = Point2D(2. * radius, 2. * radius);
      for (int i(0); i < nbAngles; i++)
      {
        angle = 2. * M_PI * (double)(i) / (double)(nbAngles);
        contour.push_back(Point(radius * cos(angle), radius * sin(angle)));
      }

      // creeate cross section
      m_crossSections.clear();
      area = pow(radius, 2) * M_PI;
      scalingFactors[0] = 1.;
      scalingFactors[1] = 1.;
      length = 1.;
      addCrossSectionFEM(area, sqrt(area) / m_meshDensity, contour,
        surfaceIdx, length, Point2D(0., 0.), Point2D(0., 1.),
        scalingFactors);
      m_crossSections[0]->setCurvatureRadius(1.);
      m_crossSections[0]->setCurvatureAngle(1.);

      log << "Cross-section created" << endl;

      // compute propagation modes
      m_crossSections[0]->buildMesh();
      m_crossSections[0]->computeModes(m_simuParams);
      mn = m_crossSections[0]->numberOfModes();
      log << mn << " modes computed" << endl;

      // compute radiation impedance
      preComputeRadiationMatrices(16, 0);

      log << "Precomputation of rad imped done" << endl;
      ofs.open("imp.txt");
      for (int i(0); i < m_numFreq; i++)
      {
        freq = max(0.1, m_simuParams.maxComputedFreq * 
          (double)i / (double)(m_numFreq - 1));
        log << "f = " << freq << " Hz" << endl;

        interpolateRadiationImpedance(radImped, freq, 0);

        ofs << freq << "  " << radImped(0, 0).real() << "  "
          << radImped(0, 0).imag() << "  "
          << radImped(1, 1).real() << "  "
          << radImped(1, 1).imag() << endl;
      }
      ofs.close();

      // Generate radiation impedance computed with scaling
      //***************************************************

      // Generate a circular contour
      contour.clear();
      radius = 1.5;
      m_maxCSBoundingBox.first = Point2D(-2. * radius, -2. * radius);
      m_maxCSBoundingBox.second = Point2D(2. * radius, 2. * radius);
      for (int i(0); i < nbAngles; i++)
      {
        angle = 2. * M_PI * (double)(i) / (double)(nbAngles);
        contour.push_back(Point(radius * cos(angle), radius * sin(angle)));
      }

      // creeate cross section
      m_crossSections.clear();
      area = pow(radius, 2) * M_PI;
      scalingFactors[0] = 1.;
      scalingFactors[1] = 2.;
      length = 1.;
      addCrossSectionFEM(area, sqrt(area) / m_meshDensity, contour,
        surfaceIdx, length, Point2D(0., 0.), Point2D(0., 1.),
        scalingFactors);
      m_crossSections[0]->setCurvatureRadius(1.);
      m_crossSections[0]->setCurvatureAngle(1.);

      log << "Cross-section created" << endl;

      // compute propagation modes
      m_crossSections[0]->buildMesh();
      m_crossSections[0]->computeModes(m_simuParams);
      mn = m_crossSections[0]->numberOfModes();
      log << mn << " modes computed" << endl;

      // compute radiation impedance
      preComputeRadiationMatrices(16, 0);

      log << "Precomputation of rad imped done" << endl;
      ofs.open("impS.txt");
      for (int i(0); i < m_numFreq; i++)
      {
        freq = max(0.1, m_simuParams.maxComputedFreq *
          (double)i / (double)(m_numFreq - 1));
        log << "f = " << freq << " Hz" << endl;

        interpolateRadiationImpedance(radImped, freq, 0);

        ofs << freq << "  " << radImped(0, 0).real() << "  "
          << radImped(0, 0).imag() << "  " 
          << radImped(1, 1).real() << "  "
          << radImped(1, 1).imag() << endl;
      }
      ofs.close();

    break;
  }

  log.close();
}

// ****************************************************************************

void Acoustic3dSimulation::cleanAcousticField()
{
  m_field.resize(0, 0);
}

// ****************************************************************************

double Acoustic3dSimulation::maxAmpField()
{
  if (m_simuParams.showAmplitude)
  {
    return m_maxAmpField;
  }
  else
  {
    return m_maxPhaseField;
  }
}

// ****************************************************************************

double Acoustic3dSimulation::minAmpField()
{
  if (m_simuParams.showAmplitude)
  {
    return m_minAmpField;
  }
  else
  {
    return m_minPhaseField;
  }
}

// ****************************************************************************
// given a point whose coordinate are expressed in the exit landmark
// return its coordinates in the cartesian landmark of the geometry

vector<Point_3> Acoustic3dSimulation::movePointFromExitLandmarkToGeoLandmark(vector<Point_3> pt)
{
  vector<Point_3> movedPoints;
  movedPoints.reserve(pt.size());

  for (auto it : pt)
  {
    movedPoints.push_back(
    movePointFromExitLandmarkToGeoLandmark(it)
    );
  }
  
  return(movedPoints);
}

// ****************************************************************************

Point_3 Acoustic3dSimulation::movePointFromExitLandmarkToGeoLandmark(Point_3 pt)
{
  Point ptVertPlane(pt.x(), pt.z());
  Vector endNormal(m_crossSections.back()->normalOut());
  Vector vertical(Vector(0., 1.));
  double angle;// angleCtlNorm;
  double circleArcAngle(m_crossSections.back()->circleArcAngle());

  
  angle = fmod(atan2(endNormal.y(), endNormal.x()) -
    atan2(vertical.y(), vertical.x()) + 2. * M_PI, 2. * M_PI);
  
  if (abs(circleArcAngle) > MINIMAL_DISTANCE)
  {
    if (signbit(m_crossSections.back()->curvRadius() * circleArcAngle))
    {
      angle -= M_PI;

      // rotate the point to compensate inclination of the end normal
      Transformation rotate(CGAL::ROTATION, sin(angle), cos(angle));
      ptVertPlane = rotate(Point(ptVertPlane.x(), -ptVertPlane.y()));
    }
    else
    {
      // rotate the point to compensate inclination of the end normal
      Transformation rotate(CGAL::ROTATION, sin(angle), cos(angle));
      ptVertPlane = rotate(ptVertPlane);
    }
  }
  else
  {
    Vector ctlVec(m_crossSections.back()->ctrLinePtOut(),
      m_crossSections.back()->ctrLinePtIn());
    double angleCtlNorm = fmod(atan2(ctlVec.y(), ctlVec.x()) -
      atan2(endNormal.y(), endNormal.x()) + 2. * M_PI, 2. * M_PI);
    Vector ptVec(ptVertPlane, m_crossSections.back()->ctrLinePtOut());
    double anglePtNorm = fmod(atan2(ptVec.y(), ptVec.x()) -
      atan2(endNormal.y(), endNormal.x()) + 2. * M_PI, 2. * M_PI);

    if (!signbit((angleCtlNorm - M_PI) * anglePtNorm))
    {
      angle -= M_PI;

      // rotate the point to compensate inclination of the end normal
      Transformation rotate(CGAL::ROTATION, sin(angle), cos(angle));
      ptVertPlane = rotate(Point(ptVertPlane.x(), -ptVertPlane.y()));
    }
    else
    {
      // rotate the point to compensate inclination of the end normal
      Transformation rotate(CGAL::ROTATION, sin(angle), cos(angle));
      ptVertPlane = rotate(ptVertPlane);
    }
  }

  // shift the point to compensate the origin of the end landmark
  return(Point_3(
    ptVertPlane.x() + m_crossSections.back()->ctrLinePtOut().x(),
    pt.y(),
    ptVertPlane.y() + m_crossSections.back()->ctrLinePtOut().y()
  ));
}

// ****************************************************************************
// Load the coordinates of the points at which the transfer functions are 
// computed

bool Acoustic3dSimulation::setTFPointsFromCsvFile(string fileName)
{
  ofstream log("log.txt", ofstream::app);
  log << "Start transfer function points extraction" << endl;

  ifstream inputFile(fileName);
  double coord[3];
  char separator(';');
  string line, coordStr;
  int cnt;

  if (!inputFile.is_open())
  {
    log << "Cannot open " << fileName << endl;
    log.close();
    return(false);
  }
  else if (inputFile.peek() != ifstream::traits_type::eof())
  {
    m_simuParams.tfPoint.clear();

    while (getline(inputFile, line))
    {
      stringstream coordLine(line);
      cnt = 0;
      while (getline(coordLine, coordStr, separator) && (cnt < 3))
      {
        if (coordStr.size() > 0)
        {
          try
          {
            coord[cnt] = stod(coordStr);
            cnt++;
          }
          catch (const std::invalid_argument& ia) {
            log << ia.what() << endl;
          }
        }
      }
      if (cnt < 3)
      {
        log << "Error: Fail to read coordinates" << endl;
      }
      else
      {
        m_simuParams.tfPoint.push_back(Point_3(coord[0], coord[1], coord[2]));
      }
    }

    if (m_simuParams.tfPoint.size() > 0)
    {
      log << "Points extracted: " << endl;
      for (auto it : m_simuParams.tfPoint)
      {
        log << it.x() << "  " << it.y() << "  " << it.z() << endl;
      }
      log.close();
      return(true);
    }
    else
    {
      log.close();
      return(false);
    }
  }
  else
  {
    log.close();
    return(false);
  }
}

// ****************************************************************************
// Compute the Rayleigh-Sommerfeld integral to compute radiated pressure

void Acoustic3dSimulation::RayleighSommerfeldIntegral(vector<Point_3> points,
  Eigen::VectorXcd& radPress, double freq, int radSecIdx)
{
  int nbPts(points.size());
  double quadPtCoord[3][2]{ {1. / 6., 1. / 6.}, {2. / 3., 1. / 6.}, {1. / 6., 2. / 3.} };
  double quadPtWeight = 1. / 3.;
  vector<Point> gaussPts;
  vector<double> areaFaces;
  double r, k(2.*M_PI*freq/m_simuParams.sndSpeed), scaling;

  // get scaling
  scaling = m_crossSections[radSecIdx]->scaleOut();

  // get velocity mode amplitude (v_x = j * q / w / rho)
  auto Vm = m_crossSections[radSecIdx]->Qout();

  radPress = Eigen::VectorXcd::Zero(nbPts);

  switch (m_simuParams.integrationMethodRadiation)
  {
    //////////////
    // DISCRETE
    /////////////

  case DISCRETE:
  {
    // determine the  spacing of the grid
    double gridDensity(15.);
    double spacing(sqrt(m_crossSections[radSecIdx]->area())
      / gridDensity);
    double dS(pow(spacing, 2));

    // generate the grid
    Polygon_2 contour(m_crossSections[radSecIdx]->contour());
    vector<Point> cartGrid;
    Point pt;
    double xmin(contour.bbox().xmin());
    double ymin(contour.bbox().ymin());
    int nx(ceil((contour.bbox().xmax() - xmin) / spacing));
    int ny(ceil((contour.bbox().ymax() - ymin) / spacing));
    for (int i(0); i < nx; i++)
    {
      for (int j(0); j < ny; j++)
      {
        pt = Point(xmin + i * spacing, ymin + j * spacing);
        if (contour.has_on_bounded_side(pt))
        {
          cartGrid.push_back(pt);
        }
      }
    }

    // Interpolate the propagation modes on the cartesian grid
    Matrix intCartGrid(m_crossSections[radSecIdx]->interpolateModes(cartGrid));

    // loop over the integration points
    for (int c(0); c < cartGrid.size(); c++)
    {
      // loop over the transverse modes
      for (int m(0); m < m_crossSections[radSecIdx]->numberOfModes(); m++)
      {
        // loop over points
        for (int p(0); p < nbPts; p++)
        {
          r = sqrt(CGAL::squared_distance(points[p],
            Point_3(0., cartGrid[c].x(), cartGrid[c].y())));
          radPress(p) -= Vm(m) * intCartGrid(c, m) * exp(1i * k * scaling * r) * dS / scaling / r;
        }
      }
    }
    break;
  }

    //////////
    // GAUSS
    //////////

    // compute Guauss integral
  case GAUSS:
    {
      // get quadrature points
      gaussPointsFromMesh(gaussPts, areaFaces,
        m_crossSections[radSecIdx]->triangulation());

      // interpolate modes
      Matrix interpolatedModes = m_crossSections[radSecIdx]->
        interpolateModes(gaussPts);

      // scale the position of the integration points
      Transformation3 scale(CGAL::SCALING, 1. / scaling);
      for (int i(0); i < points.size(); i++)
      {
        points[i] = scale(points[i]);
      }
      
      // loop over the faces of the mesh
      for (int f(0); f < areaFaces.size(); f++)
      {
        // loop over the modes
        for (int m(0); m < m_crossSections[radSecIdx]->numberOfModes(); m++)
        {
          // loop over points
          for (int p(0); p < nbPts; p++)
          {
            // loop over Gauss integration points
            for (int g(0); g < 3; g++)
            {
              // compute distance r between gauss point and the point
              r = sqrt(CGAL::squared_distance(points[p],
                Point_3(0., gaussPts[f * 3 + g].x(), gaussPts[f * 3 + g].y())));

              // compute integral
              radPress(p) -= areaFaces[f] * quadPtWeight *
                Vm(m) * interpolatedModes(f * 3 + g, m) * exp(-1i * k * scaling * r) / scaling / r;
            }
          }
        }
      }
      break;
    }
  }

  radPress /= 2 * M_PI;
}

// ****************************************************************************
// **************************************************************************
// accessors

int Acoustic3dSimulation::sectionNumber() const { return((int)m_crossSections.size()); }
double Acoustic3dSimulation::soundSpeed() const { return(m_simuParams.sndSpeed); }
CrossSection2d * Acoustic3dSimulation::crossSection(int csIdx) const
{
  return m_crossSections[csIdx].get();
}
double Acoustic3dSimulation::meshDensity() const { return m_meshDensity; }
double Acoustic3dSimulation::maxCutOnFreq() const { return m_simuParams.maxCutOnFreq; }
int Acoustic3dSimulation::numIntegrationStep() const { return m_simuParams.numIntegrationStep; }

// **************************************************************************
// Private functions.
// **************************************************************************

Point Acoustic3dSimulation::ctrLinePtOut(Point ctrLinePtIn, 
  Vector normalIn, double circleArcAngle, double curvatureRadius, double length)
{
  if (length > 0.)
  {
    Point Pt = ctrLinePtIn;
    Vector N(normalIn);
    double theta;
    if (abs(circleArcAngle) < MINIMAL_DISTANCE)
    {
      theta = -M_PI / 2.;
      Transformation rotate(CGAL::ROTATION, sin(theta), cos(theta));
      Transformation translate(CGAL::TRANSLATION, length * rotate(N));
      return(translate(Pt));
    }
    else
    {

      theta = abs(circleArcAngle) / 2.;

      if ((signbit(curvatureRadius) && !signbit(curvatureRadius * circleArcAngle))
        || (!signbit(curvatureRadius) && signbit(curvatureRadius * circleArcAngle)))
      {
        Transformation rotate(CGAL::ROTATION, sin(M_PI / 2. - theta),
          cos(theta - M_PI / 2.));
        Transformation translate(CGAL::TRANSLATION,
          -2. * curvatureRadius * sin(theta) * rotate(-N));
        return(translate(Pt));
      }
      else
      {
        Transformation rotate(CGAL::ROTATION, sin(theta - M_PI / 2.),
          cos(theta - M_PI / 2.));
        Transformation translate(CGAL::TRANSLATION,
          2. * curvatureRadius * sin(theta) * rotate(N));
        return(translate(Pt));
      }
    }
  }
  else
  {
    return(ctrLinePtIn);
  }
}

// **************************************************************************
// Create contours polygons and surface indexes lists from the upper and lower 
// profiles, and the corresponding upper and lower surface indexes
// generated by the articulatory models

void Acoustic3dSimulation::createContour(double inputUpProf[VocalTract::NUM_PROFILE_SAMPLES],
  double inputLoProf[VocalTract::NUM_PROFILE_SAMPLES], 
  int upperProfileSurface[VocalTract::NUM_PROFILE_SAMPLES],
  int lowerProfileSurface[VocalTract::NUM_PROFILE_SAMPLES],
  vector<double> &areas, vector<double> &spacing, 
  vector< Polygon_2> &contours, vector<vector<int>> &surfacesIdx)
{
  int idxContour(0);
  double temporaryUpProf[VocalTract::NUM_PROFILE_SAMPLES];
  std::fill_n(temporaryUpProf, VocalTract::NUM_PROFILE_SAMPLES, VocalTract::INVALID_PROFILE_SAMPLE);
  double temporaryLoProf[VocalTract::NUM_PROFILE_SAMPLES];
  std::fill_n(temporaryLoProf, VocalTract::NUM_PROFILE_SAMPLES, VocalTract::INVALID_PROFILE_SAMPLE);
  double tempArea(0.);
  Polygon_2 tempPoly;
  vector<int> tempSufIdx;
  tempSufIdx.reserve(3 * VocalTract::NUM_PROFILE_SAMPLES);
  double dist;
  int nIntermPts;
  Vector vecNextPt;
  Vector vecInsertPt;
  double alpha;
  bool toNewSurf(false);
  bool toNewSurfTeeth(true);

  // identify the samples between two contours as invalid samples
  for (int i(1); i < VocalTract::NUM_PROFILE_SAMPLES - 1; i++)
  {
    if ((inputUpProf[i - 1] == inputLoProf[i - 1]) && (inputUpProf[i + 1] == inputLoProf[i + 1]))
    {
      inputUpProf[i] = VocalTract::INVALID_PROFILE_SAMPLE;
      inputLoProf[i] = VocalTract::INVALID_PROFILE_SAMPLE;
    }
  }

  // initialize the first elements of the temporary profiles
  temporaryUpProf[0] = inputUpProf[0];
  temporaryLoProf[0] = inputLoProf[0];

  // create the meshes corresponding to the potentially multiple closed contours
  for (int i(1); i < VocalTract::NUM_PROFILE_SAMPLES; i++)
  {
    // store samples in the temporary profiles
    temporaryUpProf[i] = inputUpProf[i];
    temporaryLoProf[i] = inputLoProf[i];

    // compute area
    tempArea += 0.5 * (inputUpProf[i - 1] + inputUpProf[i] - inputLoProf[i - 1] - inputLoProf[i]) *
      VocalTract::PROFILE_SAMPLE_LENGTH;

    // if the contour ends, create the corresponding mesh and compute the
    // corresponding modes
    if ((inputUpProf[i - 1] != inputLoProf[i - 1])
      && (inputUpProf[i] == inputLoProf[i])
      && (inputUpProf[min(i + 1, VocalTract::NUM_PROFILE_SAMPLES - 1)]
        == inputLoProf[min(i + 1, VocalTract::NUM_PROFILE_SAMPLES - 1)]))
    {
      areas.push_back(tempArea);
      spacing.push_back(sqrt(tempArea) / m_meshDensity);

      // *********************************************************************
      // create the polygon corresponding to the upper part of the contour
      // *********************************************************************

      int idxBig;
      for (int p(0); p < (i + 1); p++)
      {
        if (temporaryUpProf[p] != VocalTract::INVALID_PROFILE_SAMPLE)
        {
          idxBig = p;
          for (int pt(idxBig); pt < (i + 1); pt++)
          {
            // insert new point in the polygon
            tempPoly.push_back(Point(pt * VocalTract::PROFILE_SAMPLE_LENGTH -
              VocalTract::PROFILE_LENGTH / 2, temporaryUpProf[pt]));
            tempSufIdx.push_back(upperProfileSurface[pt]);

            // check if it is necessary to add an intermediate point
            if ((pt != (VocalTract::NUM_PROFILE_SAMPLES - 1)) &&
              (temporaryUpProf[pt + 1] != VocalTract::INVALID_PROFILE_SAMPLE))
            {
              // compute the distance with the next point if it exists
              dist = sqrt(pow((temporaryUpProf[pt] - temporaryUpProf[pt + 1]), 2) +
                pow(VocalTract::PROFILE_SAMPLE_LENGTH, 2));

              // compute the number of necessary subdivision of contour segment
              nIntermPts = (int)floor(dist / VocalTract::PROFILE_SAMPLE_LENGTH / 2.) + 1;

              // if the next surface is different from the previous one
              if (upperProfileSurface[pt] !=
                upperProfileSurface[min(pt + 1, VocalTract::NUM_PROFILE_SAMPLES)])
                //upperProfileSurface[min(pt + 1, 96)])
              {
                toNewSurf = !toNewSurf;

                // if the exited surface is a  tooth
                if ((upperProfileSurface[pt] == 0) || (upperProfileSurface[pt] == 1))
                {
                  toNewSurf = toNewSurfTeeth;
                }
                // if the next surface is a tooth
                else if ((upperProfileSurface[min(pt + 1, VocalTract::NUM_PROFILE_SAMPLES)] == 0)
                  || (upperProfileSurface[min(pt + 1, VocalTract::NUM_PROFILE_SAMPLES)] == 1))
                //else if ((upperProfileSurface[min(pt + 1, 96)] == 0)
                //  || (upperProfileSurface[min(pt + 1, 96)] == 1))
                {
                  // flip this value
                  toNewSurfTeeth = !toNewSurfTeeth;
                  toNewSurf = toNewSurfTeeth;
                }
              }

              // if necessary, add intermediate points
              if (nIntermPts > 1)
              {
                // next point on the upper profile
                vecNextPt = Vector(Point(0., 0.), Point((pt + 1) * VocalTract::PROFILE_SAMPLE_LENGTH -
                  VocalTract::PROFILE_LENGTH / 2, temporaryUpProf[pt + 1]));

                for (int n(1); n < nIntermPts; n++)
                {
                  alpha = 1. / (double)(nIntermPts - n + 1);
                  vecInsertPt = alpha * vecNextPt +
                    (1. - alpha) * Vector(Point(0., 0.), *(tempPoly.vertices_end() - 1));

                  // add point to the upper profile
                  tempPoly.push_back(Point(vecInsertPt.x(), vecInsertPt.y()));
                  if (toNewSurf)
                  {
                    tempSufIdx.push_back(upperProfileSurface[min(pt + 1, VocalTract::NUM_PROFILE_SAMPLES)]);
                    //tempSufIdx.push_back(upperProfileSurface[min(pt + 1, 96)]);
                  }
                  else
                  {
                    tempSufIdx.push_back(tempSufIdx.back());
                  }
                }
              }
            }
          }
          break;
        }
      }


      toNewSurf = true;

      // *********************************************************************
      // create the polygon corresponding to the lower part of the contour
      // *********************************************************************

      for (int p(i - 1); p > idxBig; p--)
      {
        vecNextPt = Vector(Point(0., 0.), Point(p * VocalTract::PROFILE_SAMPLE_LENGTH -
          VocalTract::PROFILE_LENGTH / 2, temporaryLoProf[p]));

        // compute the distance with the next point if it exists
        dist = sqrt(pow(vecNextPt.y() - (tempPoly.vertices_end() - 1)->y(), 2) +
          pow(VocalTract::PROFILE_SAMPLE_LENGTH, 2));

        // compute the number of necessary subdivision of contour segment
        nIntermPts = (int)floor(dist / VocalTract::PROFILE_SAMPLE_LENGTH / 2.) + 1;

        // if the next surface is different from the previous one
        if (tempSufIdx.back() != lowerProfileSurface[p])
        {
          toNewSurf = !toNewSurf;

          // if the exited surface is a  tooth
          if ((tempSufIdx.back() == 0) || (tempSufIdx.back() == 1))
          {
            toNewSurf = toNewSurfTeeth;
          }
          // if the next surface is a tooth
          else if ((lowerProfileSurface[p] == 0) || (lowerProfileSurface[p] == 1))
          {
            // flip this value
            toNewSurfTeeth = !toNewSurfTeeth;
            toNewSurf = toNewSurfTeeth;
          }
        }

        // if necessary, add intermediate points
        if (nIntermPts > 1)
        {

          for (int n(1); n < nIntermPts; n++)
          {
            alpha = 1. / (double)(nIntermPts - n + 1);
            vecInsertPt = alpha * vecNextPt +
              (1. - alpha) * Vector(Point(0., 0.), *(tempPoly.vertices_end() - 1));

            // add point to the upper profile
            tempPoly.push_back(Point(vecInsertPt.x(), vecInsertPt.y()));
            if (toNewSurf)
            {
              tempSufIdx.push_back(lowerProfileSurface[p]);
            }
            else
            {
              tempSufIdx.push_back(tempSufIdx.back());
            }
          }
        }

        // insert the next point
        tempPoly.push_back(Point(vecNextPt.x(), vecNextPt.y()));
        tempSufIdx.push_back(lowerProfileSurface[p]);
      }

      // check if it is necessary to add intermediary points in the last intervall
      vecNextPt = Vector(Point(0., 0.), Point(idxBig * VocalTract::PROFILE_SAMPLE_LENGTH -
        VocalTract::PROFILE_LENGTH / 2, temporaryLoProf[idxBig]));

      // compute the distance with the next point if it exists
      dist = sqrt(pow(vecNextPt.y() - (tempPoly.vertices_end() - 1)->y(), 2) +
        pow(VocalTract::PROFILE_SAMPLE_LENGTH, 2));

      // compute the number of necessary subdivision of contour segment
      nIntermPts = (int)floor(dist / VocalTract::PROFILE_SAMPLE_LENGTH / 2.) + 1;

      // if the next surface is different from the previous one
      if (tempSufIdx.back() != lowerProfileSurface[idxBig])
      {
        toNewSurf = !toNewSurf;

        // if the exited surface is a  tooth
        if ((tempSufIdx.back() == 0) || (tempSufIdx.back() == 1))
        {
          toNewSurf = toNewSurfTeeth;
        }
        // if the next surface is a tooth
        else if ((lowerProfileSurface[idxBig] == 0) || (lowerProfileSurface[idxBig] == 1))
        {
          // flip this value
          toNewSurfTeeth = !toNewSurfTeeth;
          toNewSurf = toNewSurfTeeth;
        }
      }

      // if necessary, add intermediate points
      if (nIntermPts > 1)
      {
        for (int n(1); n < nIntermPts; n++)
        {
          alpha = 1. / (double)(nIntermPts - n + 1);
          vecInsertPt = alpha * vecNextPt +
            (1. - alpha) * Vector(Point(0., 0.), *(tempPoly.vertices_end() - 1));

          // add point to the upper profile
          tempPoly.push_back(Point(vecInsertPt.x(), vecInsertPt.y()));
          if (toNewSurf)
          {
            tempSufIdx.push_back(lowerProfileSurface[idxBig]);
          }
          else
          {
            tempSufIdx.push_back(tempSufIdx.back());
          }
        }
      }

      // Add the created contour and the corresponding surface indexes
      // to the contour list and the surfaces index list
      contours.push_back(tempPoly);
      surfacesIdx.push_back(tempSufIdx);

      // reinitialize the temporary profiles and area
      tempArea = 0.;
      std::fill_n(temporaryUpProf, VocalTract::NUM_PROFILE_SAMPLES, VocalTract::INVALID_PROFILE_SAMPLE);
      std::fill_n(temporaryLoProf, VocalTract::NUM_PROFILE_SAMPLES, VocalTract::INVALID_PROFILE_SAMPLE);
      tempPoly.clear();
      tempSufIdx.clear();

      idxContour++;
    }
  }
}

// **************************************************************************
// Create contours polygons and surface indexes lists from the upper and lower 
// profiles, and the corresponding upper and lower surface indexes
// generated by the articulatory models avoiding multiple contours

void Acoustic3dSimulation::createUniqueContour(double inputUpProf[VocalTract::NUM_PROFILE_SAMPLES],
  double inputLoProf[VocalTract::NUM_PROFILE_SAMPLES],
  int upperProfileSurface[VocalTract::NUM_PROFILE_SAMPLES],
  int lowerProfileSurface[VocalTract::NUM_PROFILE_SAMPLES],
  vector<double>& areas, vector<double>& spacing,
  vector< Polygon_2>& contours, vector<vector<int>>& surfacesIdx)
{
  int idxContour(0);
  double temporaryUpProf[VocalTract::NUM_PROFILE_SAMPLES];
  std::fill_n(temporaryUpProf, VocalTract::NUM_PROFILE_SAMPLES, VocalTract::INVALID_PROFILE_SAMPLE);
  double temporaryLoProf[VocalTract::NUM_PROFILE_SAMPLES];
  std::fill_n(temporaryLoProf, VocalTract::NUM_PROFILE_SAMPLES, VocalTract::INVALID_PROFILE_SAMPLE);
  double tempArea(0.);
  Polygon_2 tempPoly;
  vector<int> tempSufIdx;
  tempSufIdx.reserve(3 * VocalTract::NUM_PROFILE_SAMPLES);
  double dist;
  int nIntermPts;
  Vector vecNextPt;
  Vector vecInsertPt;
  double alpha;
  bool toNewSurf(false);
  bool toNewSurfTeeth(true);

  const double MINIMAL_DISTANCE(0.05);
  const double HALF_MINIMAL_DISTANCE(0.025);

  // identify the beginning and end of the contour
  bool firstSample(true);
  bool invalidSample, invalidPrevSample;
  int idxStart, idxStop;
  for (int i(1); i < VocalTract::NUM_PROFILE_SAMPLES - 1; i++)
  {
    invalidPrevSample = (inputUpProf[i - 1] == inputLoProf[i - 1]);
    invalidSample = (inputUpProf[i] == inputLoProf[i]);

    // detect the first sample of the contour
    if (invalidPrevSample && !invalidSample && firstSample)
    {
      idxStart = i-1;
      firstSample = false;
    }

    // detect the last sample
    if (!invalidPrevSample && invalidSample)
    {
      idxStop = i;
    }
  }

  // mark samples before the beginning as invalid
  for (int i(0); i < idxStart; i++)
  {
    inputUpProf[i] = VocalTract::INVALID_PROFILE_SAMPLE;
    inputLoProf[i] = VocalTract::INVALID_PROFILE_SAMPLE;
  }

  // mark sample after the end as invalid
  for (int i(idxStop + 1); i < VocalTract::NUM_PROFILE_SAMPLES - 1; i++)
  {
    inputUpProf[i] = VocalTract::INVALID_PROFILE_SAMPLE;
    inputLoProf[i] = VocalTract::INVALID_PROFILE_SAMPLE;
  }

  // add spacing to identical samples inside the contour
  for (int i(idxStart+1); i < idxStop; i++)
  {
    if (abs(inputUpProf[i] - inputLoProf[i]) < MINIMAL_DISTANCE)
    {
      inputUpProf[i] += HALF_MINIMAL_DISTANCE;
      inputLoProf[i] -= HALF_MINIMAL_DISTANCE;
    }
  }

  // initialize the first elements of the temporary profiles
  temporaryUpProf[0] = inputUpProf[0];
  temporaryLoProf[0] = inputLoProf[0];

  // create the meshes corresponding to the potentially multiple closed contours
  for (int i(1); i < VocalTract::NUM_PROFILE_SAMPLES; i++)
  {
    // store samples in the temporary profiles
    temporaryUpProf[i] = inputUpProf[i];
    temporaryLoProf[i] = inputLoProf[i];

    // compute area
    tempArea += 0.5 * (inputUpProf[i - 1] + inputUpProf[i] - inputLoProf[i - 1] - inputLoProf[i]) *
      VocalTract::PROFILE_SAMPLE_LENGTH;

    // if the contour ends, create the corresponding mesh and compute the
    // corresponding modes
    if ((inputUpProf[i - 1] != inputLoProf[i - 1])
      && (inputUpProf[i] == inputLoProf[i])
      && (inputUpProf[min(i + 1, VocalTract::NUM_PROFILE_SAMPLES - 1)]
        == inputLoProf[min(i + 1, VocalTract::NUM_PROFILE_SAMPLES - 1)]))
    {
      areas.push_back(tempArea);
      spacing.push_back(sqrt(tempArea) / m_meshDensity);

      // *********************************************************************
      // create the polygon corresponding to the upper part of the contour
      // *********************************************************************

      int idxBig;
      for (int p(0); p < (i + 1); p++)
      {
        if (temporaryUpProf[p] != VocalTract::INVALID_PROFILE_SAMPLE)
        {
          idxBig = p;
          for (int pt(idxBig); pt < (i + 1); pt++)
          {
            // insert new point in the polygon
            tempPoly.push_back(Point(pt * VocalTract::PROFILE_SAMPLE_LENGTH -
              VocalTract::PROFILE_LENGTH / 2, temporaryUpProf[pt]));
            tempSufIdx.push_back(upperProfileSurface[pt]);

            // check if it is necessary to add an intermediate point
            if ((pt != (VocalTract::NUM_PROFILE_SAMPLES - 1)) &&
              (temporaryUpProf[pt + 1] != VocalTract::INVALID_PROFILE_SAMPLE))
            {
              // compute the distance with the next point if it exists
              dist = sqrt(pow((temporaryUpProf[pt] - temporaryUpProf[pt + 1]), 2) +
                pow(VocalTract::PROFILE_SAMPLE_LENGTH, 2));

              // compute the number of necessary subdivision of contour segment
              nIntermPts = (int)floor(dist / VocalTract::PROFILE_SAMPLE_LENGTH / 2.) + 1;

              // if the next surface is different from the previous one
              if (upperProfileSurface[pt] !=
                upperProfileSurface[min(pt + 1, VocalTract::NUM_PROFILE_SAMPLES)])
                //upperProfileSurface[min(pt + 1, 96)])
              {
                toNewSurf = !toNewSurf;

                // if the exited surface is a  tooth
                if ((upperProfileSurface[pt] == 0) || (upperProfileSurface[pt] == 1))
                {
                  toNewSurf = toNewSurfTeeth;
                }
                // if the next surface is a tooth
                else if ((upperProfileSurface[min(pt + 1, VocalTract::NUM_PROFILE_SAMPLES)] == 0)
                  || (upperProfileSurface[min(pt + 1, VocalTract::NUM_PROFILE_SAMPLES)] == 1))
                  //else if ((upperProfileSurface[min(pt + 1, 96)] == 0)
                  //  || (upperProfileSurface[min(pt + 1, 96)] == 1))
                {
                  // flip this value
                  toNewSurfTeeth = !toNewSurfTeeth;
                  toNewSurf = toNewSurfTeeth;
                }
              }

              // if necessary, add intermediate points
              if (nIntermPts > 1)
              {
                // next point on the upper profile
                vecNextPt = Vector(Point(0., 0.), Point((pt + 1) * VocalTract::PROFILE_SAMPLE_LENGTH -
                  VocalTract::PROFILE_LENGTH / 2, temporaryUpProf[pt + 1]));

                for (int n(1); n < nIntermPts; n++)
                {
                  alpha = 1. / (double)(nIntermPts - n + 1);
                  vecInsertPt = alpha * vecNextPt +
                    (1. - alpha) * Vector(Point(0., 0.), *(tempPoly.vertices_end() - 1));

                  // add point to the upper profile
                  tempPoly.push_back(Point(vecInsertPt.x(), vecInsertPt.y()));
                  if (toNewSurf)
                  {
                    tempSufIdx.push_back(upperProfileSurface[min(pt + 1, VocalTract::NUM_PROFILE_SAMPLES)]);
                    //tempSufIdx.push_back(upperProfileSurface[min(pt + 1, 96)]);
                  }
                  else
                  {
                    tempSufIdx.push_back(tempSufIdx.back());
                  }
                }
              }
            }
          }
          break;
        }
      }


      toNewSurf = true;

      // *********************************************************************
      // create the polygon corresponding to the lower part of the contour
      // *********************************************************************

      for (int p(i - 1); p > idxBig; p--)
      {
        vecNextPt = Vector(Point(0., 0.), Point(p * VocalTract::PROFILE_SAMPLE_LENGTH -
          VocalTract::PROFILE_LENGTH / 2, temporaryLoProf[p]));

        // compute the distance with the next point if it exists
        dist = sqrt(pow(vecNextPt.y() - (tempPoly.vertices_end() - 1)->y(), 2) +
          pow(VocalTract::PROFILE_SAMPLE_LENGTH, 2));

        // compute the number of necessary subdivision of contour segment
        nIntermPts = (int)floor(dist / VocalTract::PROFILE_SAMPLE_LENGTH / 2.) + 1;

        // if the next surface is different from the previous one
        if (tempSufIdx.back() != lowerProfileSurface[p])
        {
          toNewSurf = !toNewSurf;

          // if the exited surface is a  tooth
          if ((tempSufIdx.back() == 0) || (tempSufIdx.back() == 1))
          {
            toNewSurf = toNewSurfTeeth;
          }
          // if the next surface is a tooth
          else if ((lowerProfileSurface[p] == 0) || (lowerProfileSurface[p] == 1))
          {
            // flip this value
            toNewSurfTeeth = !toNewSurfTeeth;
            toNewSurf = toNewSurfTeeth;
          }
        }

        // if necessary, add intermediate points
        if (nIntermPts > 1)
        {
          for (int n(1); n < nIntermPts; n++)
          {
            alpha = 1. / (double)(nIntermPts - n + 1);
            vecInsertPt = alpha * vecNextPt +
              (1. - alpha) * Vector(Point(0., 0.), *(tempPoly.vertices_end() - 1));

            // add point to the upper profile
            tempPoly.push_back(Point(vecInsertPt.x(), vecInsertPt.y()));
            if (toNewSurf)
            {
              tempSufIdx.push_back(lowerProfileSurface[p]);
            }
            else
            {
              tempSufIdx.push_back(tempSufIdx.back());
            }
          }
        }

        // insert the next point
        tempPoly.push_back(Point(vecNextPt.x(), vecNextPt.y()));
        tempSufIdx.push_back(lowerProfileSurface[p]);
      }

      // check if it is necessary to add intermediary points in the last intervall
      vecNextPt = Vector(Point(0., 0.), Point(idxBig * VocalTract::PROFILE_SAMPLE_LENGTH -
        VocalTract::PROFILE_LENGTH / 2, temporaryLoProf[idxBig]));

      // compute the distance with the next point if it exists
      dist = sqrt(pow(vecNextPt.y() - (tempPoly.vertices_end() - 1)->y(), 2) +
        pow(VocalTract::PROFILE_SAMPLE_LENGTH, 2));

      // compute the number of necessary subdivision of contour segment
      nIntermPts = (int)floor(dist / VocalTract::PROFILE_SAMPLE_LENGTH / 2.) + 1;

      // if the next surface is different from the previous one
      if (tempSufIdx.back() != lowerProfileSurface[idxBig])
      {
        toNewSurf = !toNewSurf;

        // if the exited surface is a  tooth
        if ((tempSufIdx.back() == 0) || (tempSufIdx.back() == 1))
        {
          toNewSurf = toNewSurfTeeth;
        }
        // if the next surface is a tooth
        else if ((lowerProfileSurface[idxBig] == 0) || (lowerProfileSurface[idxBig] == 1))
        {
          // flip this value
          toNewSurfTeeth = !toNewSurfTeeth;
          toNewSurf = toNewSurfTeeth;
        }
      }

      // if necessary, add intermediate points
      if (nIntermPts > 1)
      {
        for (int n(1); n < nIntermPts; n++)
        {
          alpha = 1. / (double)(nIntermPts - n + 1);
          vecInsertPt = alpha * vecNextPt +
            (1. - alpha) * Vector(Point(0., 0.), *(tempPoly.vertices_end() - 1));

          // add point to the upper profile
          tempPoly.push_back(Point(vecInsertPt.x(), vecInsertPt.y()));
          if (toNewSurf)
          {
            tempSufIdx.push_back(lowerProfileSurface[idxBig]);
          }
          else
          {
            tempSufIdx.push_back(tempSufIdx.back());
          }
        }
      }

      // Add the created contour and the corresponding surface indexes
      // to the contour list and the surfaces index list
      contours.push_back(tempPoly);
      surfacesIdx.push_back(tempSufIdx);

      // reinitialize the temporary profiles and area
      tempArea = 0.;
      std::fill_n(temporaryUpProf, VocalTract::NUM_PROFILE_SAMPLES, VocalTract::INVALID_PROFILE_SAMPLE);
      std::fill_n(temporaryLoProf, VocalTract::NUM_PROFILE_SAMPLES, VocalTract::INVALID_PROFILE_SAMPLE);
      tempPoly.clear();
      tempSufIdx.clear();

      idxContour++;
    }
  }
}

//*****************************************************************************

void Acoustic3dSimulation::removeDuplicatedPoints(Polygon_2& contour)
{
  auto it = contour.vertices_begin();
  const double TOLERANCE(1e-4);

  while (it != contour.vertices_end() - 1)
  {
    if (max(abs(it->x() - (it + 1)->x()),
      abs(it->y() - (it + 1)->y()))
      < TOLERANCE)
    {
      contour.erase(it + 1);
    }
    else
    {
      it++;
    }
  }
}

//*****************************************************************************
// Merge several contours contained in a vector replacing them by the 
// convex hull of all their points

void Acoustic3dSimulation::mergeContours(vector<Polygon_2>& vecPoly, 
  vector<vector<int>> &surfaceIdx)
{
  Polygon_2 poly, hull;
  ofstream log("log.txt", ofstream::app);

  for (int j(0); j < vecPoly.size(); j++)
  {
    for (int k(0); k < vecPoly[j].size(); k++)
    {
      poly.push_back(vecPoly[j][k]);
    }
  }

  CGAL::convex_hull_2(poly.begin(), poly.end(), back_inserter(hull));

  // replace the multiple contours by their convex hull
  vecPoly.clear();
  vecPoly.push_back(hull);

  // replace the surface indexes by zeros
  vector<int> tmpIdx(hull.size(), 0);
  surfaceIdx.clear();
  surfaceIdx.push_back(tmpIdx);

  log << "Multiple contours merged" << endl;
  log.close();
}

//*****************************************************************************
// Substitute a contour by its convex hull
// this is used to avoid multiple junction segments 

void Acoustic3dSimulation::makeContourConvexHull(Polygon_2& poly, vector<int>& surfaceIdx)
{
  ofstream log("log.txt", ofstream::app);

  Polygon_2 hull;

  CGAL::convex_hull_2(poly.begin(), poly.end(), back_inserter(hull));

  poly = hull;

  // replace the surface indexes by zeros
  surfaceIdx.clear();
  surfaceIdx.assign(hull.size(), 0);

  log << "Contour substituted by its convex hull" << endl;
  log.close();
}

//*****************************************************************************
// Determine the radius of curvatur, the angles of start and end of the 
// section and the necessary shift of the contour
//
// It is necessary to shift the contour because the radius obtained at the
// start and the end of the section are not exactely the same, so the 
// contour needs to be shifted to be at the average radius

void Acoustic3dSimulation::getCurvatureAngleShift(Point2D P1, Point2D P2, 
  Point2D N1, Point2D N2, double& radius, double& angle, double& shift)
{
  // inputs:
  //
  //  P1      centerline point of section 1
  //  P2      centerline point of section 2
  //  N1      centerline normal of section 1
  //  N2      centerline normal of section 2
  //
  // outputs:
  //
  //  radius    radius of circle arc joining sec 1 & 2
  //  angle    angle between sections 1 & 2
  //  shift    distance necessary to shift P2 along N2
  //        so that it is on the same circle arc as P1

  double radii[2], angles[2];

  // compute the radius of the circle arc whose center
  // is the intersection point of the normals and which
  // passes by P1, the point belonging to section i
  radii[0] = ((P2.y - P1.y) * N2.x - (P2.x - P1.x) * N2.y) /
    (N2.x * N1.y - N2.y * N1.x);

  // compute the radius of the circle arc whose center
  // is the intersection point of the normals and which
  // passes by P2, the point belonging to the next section i+1
  radii[1] = ((P2.y - P1.y) * N1.x - (P2.x - P1.x) * N1.y) /
    (N2.x * N1.y - N2.y * N1.x);

  // compute the angle corresponding to the position of P1
  angles[0] = fmod(atan2(N1.y, N1.x) + 2. * M_PI, 2. * M_PI);
  angles[1] = fmod(atan2(N2.y, N2.x) + 2. * M_PI, 2. * M_PI);

  // compute the average radius
  radius = radii[0];

  // compute the vertical shift to apply to the contour
  shift = radii[0] - radius;

  // compute the signed angle between both angular positions
  angle = angles[1] - angles[0];
  if ((2. * M_PI - abs(angle)) < abs(angle))
  {
    if (signbit(angle))
    {
      angle = 2. * M_PI - abs(angle);
    }
    else
    {
      angle = abs(angle) - 2. * M_PI;
    }
  }
}

//*****************************************************************************
// Extract the contours, the surface indexes, the centerline and the normals
// from the VocalTractLab geometry

void Acoustic3dSimulation::extractContours(VocalTract* tract, vector<vector<Polygon_2>>& contours,
  vector<vector<vector<int>>>& surfaceIdx, vector<Point2D>& centerLine, vector<Point2D>& normals)
{
  // for cross profile data extraction
  double upperProfile[VocalTract::NUM_PROFILE_SAMPLES];
  double lowerProfile[VocalTract::NUM_PROFILE_SAMPLES];
  int upperProfileSurface[VocalTract::NUM_PROFILE_SAMPLES];
  int lowerProfileSurface[VocalTract::NUM_PROFILE_SAMPLES];
  Tube::Articulator articulator;

  // for contour data
  vector<double> areas, spacings;
  vector<Polygon_2> tmpContours;
  vector<vector<int>> tmpSurfacesIdx;


  for (int i(0); i < VocalTract::NUM_CENTERLINE_POINTS; i++)
  {
    // extract the data of the cross-section
    tract->getCrossProfiles(tract->centerLine[i].point, tract->centerLine[i].normal,
      upperProfile, lowerProfile, upperProfileSurface, lowerProfileSurface, true, articulator);

    // create the corresponding contours
    tmpContours.clear();
    tmpSurfacesIdx.clear();
    // FIXME: createUniqueContour is used to avoid creating branches,
    // but in the future, if handling branches becomes possible, 
    // createContour should be used instead
    createUniqueContour(upperProfile, lowerProfile, upperProfileSurface,
      lowerProfileSurface, areas, spacings, tmpContours, tmpSurfacesIdx);

    contours.push_back(tmpContours);
    surfaceIdx.push_back(tmpSurfacesIdx);
    centerLine.push_back(tract->centerLine[i].point);
    normals.push_back(tract->centerLine[i].normal);
  }
}

//*****************************************************************************
// Extract the contours, the surface indexes, the centerline and the normals
// from a csv file 

bool Acoustic3dSimulation::extractContoursFromCsvFile(
  vector<vector<Polygon_2>>& contours, vector<vector<vector<int>>>& surfaceIdx,
  vector<Point2D>& centerLine, vector<Point2D>& normals,
  vector<pair<double, double>>& scalingFactors, bool simplifyContours)
{
  //******************************************************************
  // Extract the centerline, centerline normals and the contours
  // from the csv file
  //******************************************************************

  vector<Polygon_2> tmpCont;
  vector<int> tmpIdx;
  vector<vector<int>> tmpVecIdx;
  string line, coordX, coordY;
  char separator(';');
  Cost cost;                // for contour simplification
  int idxCont(0);
  Point2D ctlPt, normalVec;
  pair<double, double> scalings;
  double x, y;
  bool abort(false);
  ifstream geoFile(m_geometryFile);

  ofstream log("log.txt", ofstream::app);

  //*************************************************************
  // Lambda expression to convert from string to double failsafe
  //*************************************************************

  auto strToDouble = [&](const string& str, double& result)
  {
    try
    {
      result = stod(str);
    }
    catch (const std::invalid_argument& ia)
    {
      log << "Warning: " << ia.what() << " could not convert string \"" 
        << str << "\" to number" << endl;
      if (str != "\r")
      {
        abort = true;
      }
    }
  };

  //*************************************************************

  // check if the file is opened
  if (!geoFile.is_open())
  {
    log << "Cannot open " << m_geometryFile << endl;  
    log.close();
    return false;
  }
  // check if the file is empty
  else if (geoFile.peek() != ifstream::traits_type::eof())
  {
    while (getline(geoFile, line))
    {
      // extract the line corresponding to the x components
      stringstream lineX(line);

      // extract the line corresponding to the y components
      if (!getline(geoFile, line)) { abort = true; break; };
      stringstream lineY(line);

      // extract the centerline point
      if (!getline(lineX, coordX, separator)) { abort = true; break; };
      if (!getline(lineY, coordY, separator)) { abort = true; break; };
      strToDouble(coordX, ctlPt.x);
      strToDouble(coordY, ctlPt.y);

      // extract the normal to the centerline
      if (!getline(lineX, coordX, separator) || abort) { abort = true; break; };
      if (!getline(lineY, coordY, separator) || abort) { abort = true; break; };
      strToDouble(coordX, normalVec.x);
      strToDouble(coordY, normalVec.y);
      normalVec.normalize();

      // extract the scaling factors
      if (!getline(lineX, coordX, separator) || abort) { abort = true; break; };
      if (!getline(lineY, coordY, separator) || abort) { abort = true; break; };
      strToDouble(coordX, scalings.first);
      strToDouble(coordY, scalings.second);

      // initialize contours
      tmpCont.clear();
      tmpCont.push_back(Polygon_2());

      // extract contour
      while (getline(lineX, coordX, separator) && (coordX.length() > 0))
      {
        if (!getline(lineY, coordY, separator)) { abort = true; break; };
        strToDouble(coordX, x);
        strToDouble(coordY, y);
        if (abort) { break; }
        tmpCont.back().push_back(Point(x, y));
      }
      if (abort) { break; }
      
      // check if there is at least 3 points in each contour
      for (int i(0); i < tmpCont.size(); i++)
      {
        if (tmpCont[i].size() < 3) 
        {
          abort = true;
          break; 
        }
      }
      if (abort) { break; }

      // if nothing failed before, add the centerline, normal and scaling factors
      centerLine.push_back(ctlPt);
      normals.push_back(normalVec);
      scalingFactors.push_back(scalings);

      // remove the last point if it is identical to the first point
      auto itFirst = tmpCont.back().vertices_begin();
      auto itLast = tmpCont.back().vertices_end() - 1;
      if (*itFirst == *itLast)
      {
        tmpCont.back().erase(itLast);
      }

      // if requested, simplify the contour removing points which are close
      if (simplifyContours && (tmpCont.back().size() > 10))
      {
        tmpCont.back() = CGAL::Polyline_simplification_2::simplify(
          tmpCont.back(), cost, Stop(0.5));
      }

      // Add the contour to the contour list
      contours.push_back(tmpCont);

      // generate the surface indexes (all zero since there is no clue 
      // about the surface type)
      tmpIdx.clear();
      tmpIdx.assign(tmpCont.back().size(), 0);
      tmpVecIdx.clear();
      tmpVecIdx.push_back(tmpIdx);
      surfaceIdx.push_back(tmpVecIdx);

      log << "Contour " << idxCont << " extracted" << endl;
      idxCont++;
    }
    // at least two contours must be given to create a proper geometry
    if (contours.size() < 2) { abort = true; }
    if (abort)
    {
      log << "Importation failed" << endl;
      log.close();
      return false;
    }
    else
    {
      log << "Importation successful" << endl;
      log.close();
      return true;
    }
  }
  else
  {
  log.close();
    return false;
  }
}

//*************************************************************************
// Create segments adding intermediate 0 length segments where 
// one of the segment's contour is not exactely contained in the other

bool Acoustic3dSimulation::createCrossSections(VocalTract* tract,
  bool createRadSection)
{
  // Ensure m_geometryFile is convertible to const char*
  const std::string& geoFile = m_geometryFile; 
  std::cout << "[A3DS_DEBUG_CREATE_CS] createCrossSections ENTRY - m_geometryImported: " << m_geometryImported 
            << ", m_geometryFile: " << (geoFile.empty() ? "EMPTY" : geoFile.c_str())
            << ", tract_ptr: " << tract << std::endl;

  const double MINIMAL_AREA(0.15);

  //*******************************************
  // Extract the contours and the centerline
  //*******************************************

  // variables for contour extraction
  vector<vector<Polygon_2>> contours;
  vector<vector<vector<int>>> surfaceIdx;
  vector<Point2D> centerLine;
  vector<Point2D> normals;
  vector<pair<double, double>> vecScalingFactors;
  vector<double> totAreas;
  vector<array<double, 4>> bboxes;
  array<double, 4> arrayZeros = { 0., 0., 0., 0. };
  Vector shiftVec;

  ofstream ofs;
  ofstream log("log.txt", ofstream::app);
  log << "Start cross-section creation" << endl;


  if (m_geometryImported)
  {
    std::cout << "[A3DS_DEBUG_CREATE_CS] Path: CSV Geometry. m_geometryFile: " << (geoFile.empty() ? "EMPTY" : geoFile.c_str()) << std::endl;
    log << "[A3DS_DEBUG_CREATE_CS] Path: CSV Geometry. m_geometryFile: " << (geoFile.empty() ? "EMPTY" : geoFile.c_str()) << std::endl;
    if ( !extractContoursFromCsvFile(contours, surfaceIdx, centerLine, 
      normals, vecScalingFactors, true))
    {
      // CSV导入失败的日志（如果extractContoursFromCsvFile内部没有充分日志的话）
      std::cout << "[A3DS_DEBUG_CREATE_CS] extractContoursFromCsvFile FAILED." << std::endl;
      log << "[A3DS_DEBUG_CREATE_CS] extractContoursFromCsvFile FAILED." << std::endl;
      // log.close(); // 通常由调用者管理日志流生命周期
      return false; 
    }
  }
  else // m_geometryImported is false
  {
    std::cout << "[A3DS_DEBUG_CREATE_CS] Path: VocalTract Geometry. m_geometryImported is false. CSV IS REQUIRED. ABORTING GEOMETRY FROM TRACT." << std::endl;
    log << "[A3DS_DEBUG_CREATE_CS] Path: VocalTract Geometry. m_geometryImported is false. CSV IS REQUIRED. ABORTING GEOMETRY FROM TRACT." << std::endl;
    // Cleanup or set error state if needed
    m_crossSections.clear(); // 清理可能存在的旧截面数据
    std::cout << "[A3DS_DEBUG_CREATE_CS] createCrossSections EXIT - returning false (CSV required but m_geometryImported is false)." << std::endl;
    // log.close(); // 同上，让调用者管理
    return false; // 强制失败，因为我们需要CSV

    /* // Original code for VocalTract path - now disabled
    std::cout << "[A3DS_DEBUG_CREATE_CS] Path: VocalTract Geometry. tract_ptr: " << tract << ". Calling tract->getCrossProfiles() and tract->getCrossSection()." << std::endl;
    log << "[A3DS_DEBUG_CREATE_CS] Path: VocalTract Geometry. tract_ptr: " << tract << ". Calling tract->getCrossProfiles() and tract->getCrossSection()." << std::endl;
    if (tract == NULL)
    {
      std::cerr << "[A3DS_ERROR] createCrossSections: tract is NULL in VocalTract geometry path!" << std::endl;
      log << "[A3DS_ERROR] createCrossSections: tract is NULL in VocalTract geometry path!" << std::endl;
      return false;
    }
    extractContours(tract, contours, surfaceIdx, centerLine, normals);
    */
  }


  // initialize max bounding box
  m_maxCSBoundingBox.first = Point2D(0., 0.);
  m_maxCSBoundingBox.second = Point2D(0., 0.);

  for (int i(0); i < contours.size(); i++)
  {
    totAreas.push_back(0.);
    bboxes.push_back(arrayZeros);
    for (auto cont : contours[i]) {
      totAreas.back() += abs(cont.area());

      bboxes.back()[0] = min(bboxes.back()[0], cont.bbox().xmin());
      bboxes.back()[1] = max(bboxes.back()[1], cont.bbox().xmax());
      bboxes.back()[2] = min(bboxes.back()[2], cont.bbox().ymin());
      bboxes.back()[3] = max(bboxes.back()[3], cont.bbox().ymax());
    }

    // recenter the centerline for curved geometries
    if (m_simuParams.curved)
    {
      shiftVec = Vector(0., -(bboxes.back()[2] + bboxes.back()[3]) / 2.);
      Transformation translate(CGAL::TRANSLATION, shiftVec);
      for (int j(0); j < contours[i].size(); j++)
      {
        contours[i][j] = transform(translate, contours[i][j]);
      }
      centerLine[i].x -= shiftVec.y() * normals[i].x;
      centerLine[i].y -= shiftVec.y() * normals[i].y;
      bboxes.back()[2] += shiftVec.y();
      bboxes.back()[3] += shiftVec.y();
    }

    m_maxCSBoundingBox.first.x = min(bboxes.back()[0],
      m_maxCSBoundingBox.first.x);
    m_maxCSBoundingBox.first.y = min(bboxes.back()[2],
      m_maxCSBoundingBox.first.y);
    m_maxCSBoundingBox.second.x = max(bboxes.back()[1],
      m_maxCSBoundingBox.second.x);
    m_maxCSBoundingBox.second.y = max(bboxes.back()[3],
      m_maxCSBoundingBox.second.y);
  }

  //*********************************************************
  // Create lambda expression to compute the scaling factors
  //*********************************************************

  double prevCurvRadius, prevAngle, length;

  auto getScalingFactor = [&](int idx1, int idx2)
  {
    double scaling, shift;
    array<double, 4> bb1(bboxes[idx1]), bb2(bboxes[idx2]);

    double scalingArea(sqrt(max(MINIMAL_AREA, totAreas[idx2]) / max(MINIMAL_AREA, totAreas[idx1])));

    if ((totAreas[idx1] < MINIMAL_AREA) || (totAreas[idx2] < MINIMAL_AREA)
      || (m_contInterpMeth == AREA))
    {
      scaling = 0.999 * scalingArea;
    }
    else
    {
      // compute the distance between the center points of the 2 contours at the junctions
      Point ptOut(ctrLinePtOut(Point(centerLine[idx1].x, centerLine[idx1].y),
        Vector(normals[idx1].x, normals[idx1].y), prevAngle, prevCurvRadius, length));
      Vector vec(ptOut, Point(centerLine[idx2].x, centerLine[idx2].y));
      shift = -CGAL::scalar_product(vec, Vector(normals[idx2].x, normals[idx2].y));

      double meanX((abs(bb1[0]) + abs(bb1[1])
        + abs(bb2[0]) + abs(bb2[1])));

      double meanY((abs(bb1[2]) + abs(bb1[3])
        + abs(bb2[2]) + abs(bb2[3] + 2. * shift)));

      if (meanX > meanY)
      {
        scaling = min(bb2[0] / bb1[0], bb2[1] / bb1[1]);
      }
      else
      {
        scaling = min((bb2[2] + shift) / bb1[2], (bb2[3] + shift) / bb1[3]);
      }

      scaling = 0.999 * min(scalingArea, scaling);
    }

    return(scaling);
  };

  //**********************************************************************

  // variables for cross-section creation
  double curvRadius, angle, shift, area, radius;
  double prevScalingFactors[2] = { 1., 1. };
  double scalingFactors[2] = { 1., 1. };
  double array1[2] = { 1., 1. };
  vector<int> tmpPrevSection, prevSecInt, listNextCont, tmpSurf;
  vector<vector<int>> prevSections, intSurfacesIdx;
  int secIdx(0), intSecIdx(0), nextSecIdx, nbCont(contours.size());
  vector<Polygon_2> intContours;
  Pwh_list_2 intersections;
  bool sidePrev, side;
  Vector ctlShift;

  //**********************************************************************
  // For straight geometries make the geometry straight
  //**********************************************************************

  vector<double> vecLengths;
  vecLengths.reserve(centerLine.size() - 1);

  if (!m_simuParams.curved)
  {
    // compute the distance between the centerline points
    for (int i(1); i < centerLine.size(); i++)
    {
      vecLengths.push_back(centerLine[i - 1].getDistanceFrom(centerLine[i]));
    }

    // move the centerline points on a straight line and put all the normals 
    // similar upward
    length = 0.;
    centerLine[0] = Point2D(0., 0.);
    normals[0] = Point2D(0., 1.);
    for (int i(1); i < centerLine.size(); i++)
    {
      length += vecLengths[i - 1];
      centerLine[i] = Point2D(length, 0.);
      normals[i] = Point2D(0., 1.);
    }
  }

  //**********************************************************************
  // Add an intermediate centerline point and normal before the last ones
  //**********************************************************************

  centerLine.push_back(centerLine.back());
  normals.push_back(normals.back());
  int lastCtl(centerLine.size() - 1);

  getCurvatureAngleShift(centerLine[lastCtl - 2], centerLine[lastCtl], 
    normals[lastCtl - 2], normals[lastCtl], curvRadius, angle, shift);

  Point pt(Point(centerLine.back().x, centerLine.back().y));
  Vector N(Vector(normals.back().x, normals.back().y));

  // get the sign of the curvature radius
  double signCurvRadius((0. < curvRadius) - (curvRadius < 0.));

  if (abs(angle) > MINIMAL_DISTANCE)
  {
    angle /= 4.;
    if ((signbit(curvRadius) && !signbit(curvRadius * angle))
      || (!signbit(curvRadius) && signbit(curvRadius * angle)))
    {
      Transformation rotate(CGAL::ROTATION, sin(M_PI / 2. - signCurvRadius * abs(angle)),
        cos(M_PI / 2. - signCurvRadius * abs(angle)));
      Transformation translateInit(CGAL::TRANSLATION,
        -2. * abs(curvRadius) * sin(signCurvRadius * abs(angle)) * rotate(N));
      pt = translateInit(pt);
    }
    else
    {
      Transformation rotate(CGAL::ROTATION, sin(signCurvRadius* abs(angle) - M_PI / 2.),
        cos(signCurvRadius* abs(angle) - M_PI / 2.));
      Transformation translateInit(CGAL::TRANSLATION,
        -2. * abs(curvRadius) * sin(signCurvRadius * abs(angle)) * rotate(N));
      pt = translateInit(pt);
    }

    centerLine[lastCtl - 1].x = pt.x();
    centerLine[lastCtl - 1].y = pt.y();

    angle *= -2.;
    Transformation rotateN(CGAL::ROTATION, sin(angle), cos(angle));
    N = rotateN(N);
    normals[lastCtl - 1].x = N.x();
    normals[lastCtl - 1].y = N.y();
  }
  else
  {
    Vector tr((centerLine[lastCtl - 2].x - centerLine[lastCtl].x)/2.,
      (centerLine[lastCtl - 2].y - centerLine[lastCtl].y)/2.);
    Transformation translateInit(CGAL::TRANSLATION, tr);

    pt = translateInit(pt);
    centerLine[lastCtl - 1].x = pt.x();
    centerLine[lastCtl - 1].y = pt.y();
  }

  //*******************************************
  // Create the cross-sections
  //*******************************************

  // clear cross-sections before 
  m_crossSections.clear();

  // compute the curvatur parameters of the first cross-section
  getCurvatureAngleShift(centerLine[0], centerLine[1], normals[0], normals[1],
    prevCurvRadius, prevAngle, shift);

  // initialize the previous section index list
  for (int c(0); c < contours[0].size(); c++){prevSections.push_back(tmpPrevSection);}

  // initialise the scaling factors
  if (m_simuParams.varyingArea)
  {
    switch (m_contInterpMeth)
    {
    case AREA: case BOUNDING_BOX:
      prevScalingFactors[0] = 1.;
      prevScalingFactors[1] = getScalingFactor(0, 1);
      break;
    case FROM_FILE:
      prevScalingFactors[0] = vecScalingFactors[0].first;
      prevScalingFactors[1] = vecScalingFactors[0].second;
      break;
    }
  }

  // create the cross-sections
  for (int i(1); i < nbCont; i++)
  {
    //**********************************
    // Create previous cross-sections
    //**********************************

    length = centerLine[i-1].getDistanceFrom(centerLine[i]);

    // compute the scaling factors
    if (m_simuParams.varyingArea)
    {
      if (i < (nbCont - 2))
      {
        switch (m_contInterpMeth)
        {
        case AREA: case BOUNDING_BOX:
          scalingFactors[0] = 1.;
          scalingFactors[1] = getScalingFactor(i, i + 1);
          break;
        case FROM_FILE:
          scalingFactors[0] = vecScalingFactors[i].first;
          scalingFactors[1] = vecScalingFactors[i].second;
          break;
        }
      }
      else if (i == nbCont - 2)
      {
        switch (m_contInterpMeth)
        {
        case AREA: case BOUNDING_BOX:
          scalingFactors[0] = 1.;
          scalingFactors[1] = getScalingFactor(i, i + 1);
          break;
        case FROM_FILE:
          scalingFactors[0] = vecScalingFactors[i].first;
          scalingFactors[1] = vecScalingFactors[i].second;
          break;
        }
      }
      else if (i == nbCont - 1)
      {
        switch (m_contInterpMeth)
        {
        case AREA: case BOUNDING_BOX:
          scalingFactors[0] = getScalingFactor(i - 1, i);
          scalingFactors[1] = 1.;
          break;
        case FROM_FILE:
          scalingFactors[0] = vecScalingFactors[i].first;
          scalingFactors[1] = vecScalingFactors[i].second;
        }
      }
    }

    // loop over the created contours
    for (int c(0); c < contours[i-1].size(); c++)
    {
      area = abs(contours[i - 1][c].area());
      addCrossSectionFEM(area, sqrt(area)/m_meshDensity, contours[i - 1][c],
        surfaceIdx[i-1][c], length, centerLine[i - 1], normals[i - 1], 
        prevScalingFactors);

      // set the connections with the previous cross-sections if necessary
      if (prevSections[c].size() > 0)
      {
        for (int cn(0); cn < prevSections[c].size(); cn++)
        {
          m_crossSections[secIdx]->
            setPreviousSection(prevSections[c][cn]);
        }
      }

      // set the curvature radius
      m_crossSections[secIdx]->setCurvatureRadius(prevCurvRadius);

      // set the curvature angle
      m_crossSections[secIdx]->setCurvatureAngle(prevAngle);

      secIdx++;
    }

    //******************************************
    // extract current cross-section parameters
    //******************************************

    // compute the curvatur parameters of the section
    getCurvatureAngleShift(centerLine[i], centerLine[i+1], normals[i], normals[i+1],
      curvRadius, angle, shift);

    // if the area is equal to the minimal area, the contours
    // are defined as the scaled previous contours
    //
    // Compute the sum of the areas of all the contours of the slice
    // FIXME: normally already computed
    area = 0.;
    for (auto cont : contours[i]) { area += abs(cont.area());
    }
    if (area <= MINIMAL_AREA)
    {
      // copy the previous contours 
      contours[i] = contours[i-1];
      surfaceIdx[i] = surfaceIdx[i-1];
      scalingFactors[0] = 1.;
      scalingFactors[1] = 1.;

      // compute the distance between the center points of the 2 centerline points at the junctions
      Point ptOut(ctrLinePtOut(Point(centerLine[i - 1].x, centerLine[i - 1].y),
        Vector(normals[i - 1].x, normals[i - 1].y), prevAngle, prevCurvRadius, length));
      Vector vec(ptOut, Point(centerLine[i].x, centerLine[i].y));

      // shift the centerline point
      centerLine[i].x -= vec.x(); 
      centerLine[i].y -= vec.y(); 

      //scale the contours
      Transformation scale(CGAL::SCALING, prevScalingFactors[1]);
      for (int j(0); j < contours[i].size(); j++)
      {
        contours[i][j] = transform(scale, contours[i][j]);
      }
    }

    //**********************************
    // Create intermediate 0 length
    // sections if necessary
    //**********************************

    // FIXME A particular case of all points of one polygon beeing
    // Either outside or inside but an edge cutting a part of the other
    // polygon is not taken into account
    //
    // This can be solved by applying the check to the other polygon
    // if no intersection is found
    // 
    // However, this is not very likely to be found and it is more likely 
    // when the polygons have few points

    // clear intermediate contour list
    intContours.clear();
    intSurfacesIdx.clear();
    intersections.clear();
    prevSecInt.clear();
    listNextCont.clear();
    prevSections.clear();
    intSecIdx = 0;

    // loop over the contours of the current cross-section
    for (int c(0); c < contours[i].size(); c++)
    {
      // clean the temporary previous section list
      tmpPrevSection.clear();

      // extract the contour and scale it
      Transformation scale(CGAL::SCALING, scalingFactors[0]);
      Polygon_2 cont(transform(scale, contours[i][c]));

      // loop over the contours of the previous cross-section
      for (int cp(0); cp < contours[i-1].size(); cp++)
      {
        // extract the previous contour and scale it
        Transformation scale(CGAL::SCALING, prevScalingFactors[1]);
        ctlShift = Vector(Point(centerLine[i].x, centerLine[i].y),
          m_crossSections.back()->ctrLinePtOut());

        Transformation translate(CGAL::TRANSLATION,
          Vector(0., ctlShift* m_crossSections.back()->normalOut()));
        Polygon_2 prevCont(transform(translate, transform(scale, contours[i - 1][cp])));

        if (!similarContours(cont, prevCont, MINIMAL_DISTANCE_DIFF_POLYGONS))
        {

          // Check if the current and previous contour intersect:
          // check if the first point of the previous contour
          // is on the bounded side of the current contour
          auto itP = prevCont.begin();
          sidePrev = cont.has_on_bounded_side(*itP);

          // loop over the points of the previous contour
          for (; itP != prevCont.end(); itP++)
          {
            side = cont.has_on_bounded_side(*itP);

            // if the previous point and the next point are on
            // different sides
            if (side != sidePrev)
            {
              // compute the intersections of both contours
              intersections.clear();
              CGAL::intersection(prevCont, cont, back_inserter(intersections));

              // loop over the intersection polygons created
              for (auto pol = intersections.begin();
                pol != intersections.end(); pol++)
              {


                // add the corresponding previous section index
                // to the previous section index list for the 
                // intermediate section which will be created
                prevSecInt.push_back(secIdx - contours[i - 1].size() + cp);

                // add the corresponding index of the next contour
                listNextCont.push_back(c);

                // add the index of the intermediate section 
                // which will be created from this contour 
                // to the previous section index list
                tmpPrevSection.push_back(secIdx + intSecIdx);

                // remove the duplicated points if some have been created
                Polygon_2 outerBoundary(pol->outer_boundary());
                removeDuplicatedPoints(outerBoundary);

                // add it the intermediate contour list
                intContours.push_back(outerBoundary);

                // create a surface index vector (the value does not
                // mater since they are not used after)
                tmpSurf.clear();
                tmpSurf.assign(intContours.back().size(), 0);
                intSurfacesIdx.push_back(tmpSurf);

                intSecIdx++;
              }
              break;
            }
            else
            {
              sidePrev = side;
            }
          }
          // if no intersection has been found, check if one contour is 
          // completely contained inside the other
          if ((sidePrev == side) &&
            CGAL::do_intersect(contours[i][c], contours[i - 1][cp]))
          {
            tmpPrevSection.push_back(secIdx - contours[i - 1].size() + cp);
          }
        }
        else
        {
          tmpPrevSection.push_back(secIdx - contours[i - 1].size() + cp);
        }
      }
      // add the list of previous section to connect to the 
      // current section
      prevSections.push_back(tmpPrevSection);
    }

    // set the next section indexes to the previous section 
    nextSecIdx = secIdx + intSecIdx; // index of the first next section
    for (int c(0); c < prevSections.size(); c++)
    {
      if (prevSections[c].size() > 0)
      {
        // loop over the previous sections
        for (int cp(0); cp < prevSections[c].size(); cp++)
        {
          // if the section is not an intermediate one
          if (prevSections[c][cp] < secIdx)
          {
            m_crossSections[prevSections[c][cp]]->setNextSection(
              nextSecIdx + c
            );
          }
        }
      }
    }

    // if intermediate contours have been created, add corresponding
    // cross-sections
    if (intContours.size() != 0)
    {
      for (int c(0); c < intContours.size(); c++) {

        area = abs(intContours[c].area());
        addCrossSectionFEM(area, sqrt(area)/m_meshDensity, intContours[c],
          intSurfacesIdx[c], 0., centerLine[i], normals[i], array1);

        // define as a junction section
        m_crossSections[secIdx]->setJunctionSection(true);

        // set the previous section index
        m_crossSections[secIdx]->setPreviousSection(prevSecInt[c]);

        // set this section as the next section of its previous one
        m_crossSections[prevSecInt[c]]->setNextSection(secIdx);

        // set the next section
        m_crossSections[secIdx]->setNextSection(nextSecIdx +
          listNextCont[c]);

        secIdx++;
      }
    }

    // set current cross-section as previous cross-section
    std::copy(begin(scalingFactors), end(scalingFactors), begin(prevScalingFactors));
    prevCurvRadius = curvRadius;
    prevAngle = angle;
  }

  //********************************
  // create last cross-sections
  //********************************

  radius = 0.;  // initalise the radius of the radiation cross-section
  tmpPrevSection.clear(); // the list of section connected to the radiation section
  for (int c(0); c < contours.back().size(); c++)
  {
    // add the index of the created cross-section to the list of cross-sections
    // to connect to the radiation cross-section
    tmpPrevSection.push_back(secIdx);

    area = abs(contours.back()[c].area());
    length = centerLine.rbegin()[1].getDistanceFrom(centerLine.back());

    addCrossSectionFEM(area, sqrt(area)/m_meshDensity, contours.back()[c],
      surfaceIdx.back()[c], length, centerLine.rbegin()[1], normals.rbegin()[1],
      prevScalingFactors);

    // set the connections with the previous cross-sections if necessary
    if (prevSections[c].size() > 0)
    {
      for (int cn(0); cn < prevSections[c].size(); cn++)
      {
        m_crossSections[secIdx]->
          setPreviousSection(prevSections[c][cn]);
      }
    }

    // set the curvature radius
    m_crossSections[secIdx]->setCurvatureRadius(prevCurvRadius);

    // set the curvature angle
    m_crossSections[secIdx]->setCurvatureAngle(prevAngle);

    // set the radius of the radiation cross-section so that the last 
    // cross-sections are contained inside
    radius = max(radius, max({ contours.back()[c].bbox().xmax(),
      contours.back()[c].bbox().ymax(),
      abs(contours.back()[c].bbox().xmin()),
      abs(contours.back()[c].bbox().ymin()) }));

    secIdx++;
  }

  //***************************************************************
  // Create radiation cross-section
  //***************************************************************

  if (createRadSection)
  {
    log << "Create radiation cross-section" << endl;

    double PMLThickness = radius;
    radius *= 2.1;

    addCrossSectionRadiation(centerLine.back(), normals.back(), 
      radius, PMLThickness);

    // Connect with the previous cross-sections
    for (int i(0); i < tmpPrevSection.size(); i++)
    {
      m_crossSections[secIdx]->setPreviousSection(tmpPrevSection[i]);
      m_crossSections[tmpPrevSection[i]]->setNextSection(secIdx);
    }
  }

  //***************************************************************
  // Compute the XZ sagittal plane bounding box
  //***************************************************************

  updateBoundingBox();

  //**********************************************************************
  // Check if the noise source index corresponds to an existing segment
  //**********************************************************************

  if (m_idxSecNoiseSource >= m_crossSections.size())
  {
    m_idxSecNoiseSource = max(0, (int)m_crossSections.size() - 1);
  }

  //***************************************************************
  // Export data
  //***************************************************************

  //// print cross-sections parameters in log file
  //for (int i(0); i < m_crossSections.size(); i++)
  //{
  //  log << "Section " << i << endl;
  //  log << *m_crossSections[i] << endl;
  //}

  //ofs.open("sec.txt");
  //// extract parameters of the sections
  //for (int s(0); s < m_crossSections.size(); s++)
  //{
  //  if (m_crossSections[s]->length() > 0.)
  //  {
  //    // entrance features
  //    ofs << m_crossSections[s]->ctrLinePt().x << "  "
  //      << m_crossSections[s]->ctrLinePt().y << "  "
  //      << m_crossSections[s]->normal().x << "  "
  //      << m_crossSections[s]->normal().y << "  "
  //      << m_crossSections[s]->curvRadius() << "  "
  //      << m_crossSections[s]->circleArcAngle() << endl;

  //    // exist features
  //    ofs << m_crossSections[s]->ctrLinePtOut().x() << "  "
  //      << m_crossSections[s]->ctrLinePtOut().y() << "  "
  //      << m_crossSections[s]->normalOut().x() << "  "
  //      << m_crossSections[s]->normalOut().y() << "  "
  //      << m_crossSections[s]->curvRadius() << "  "
  //      << m_crossSections[s]->circleArcAngle() << endl;
  //  }
  //}
  //ofs.close();

  //// export the cross-section lengths
  //ofs.open("lengths.txt");
  //for (int i(0); i < m_crossSections.size(); i++)
  //{
  //  if (m_crossSections[i]->length() != 0)
  //  {
  //    ofs << m_crossSections[i]->length() << endl;
  //  }
  //}
  //ofs.close();

  std::cout << "[A3DS_DEBUG_CREATE_CS] createCrossSections EXIT" << std::endl;
  return true;
  log.close();
}

//*************************************************************************
// Update the bounding box in the sagittal plane (X, Z)

void Acoustic3dSimulation::updateBoundingBox()
{
  pair<Point2D, Point2D> bboxXZ;
  Point pt;

  // initialize bounding box
  bboxXZ.first = Point2D(0., 0.);
  bboxXZ.second = Point2D(0., 0.);

  // lambda expression to update the bounding box
  auto updateBbox = [&]()
  {
    bboxXZ.first.x = min(bboxXZ.first.x, pt.x());
    bboxXZ.first.y = min(bboxXZ.first.y, pt.y());
    bboxXZ.second.x = max(bboxXZ.second.x, pt.x());
    bboxXZ.second.y = max(bboxXZ.second.y, pt.y());
  };

  for (int i(0); i < m_crossSections.size(); i++)
  {
    if (m_crossSections[i]->contour().size() > 0)
    {
      auto bbox = m_crossSections[i]->contour().bbox();

      // pt in min
      Transformation translateMinIn(CGAL::TRANSLATION,
        m_crossSections[i]->scaleIn() * bbox.ymin() * m_crossSections[i]->normalIn());
      pt = translateMinIn(m_crossSections[i]->ctrLinePtIn());
      updateBbox();

      // pt in max
      Transformation translateMaxIn(CGAL::TRANSLATION,
        m_crossSections[i]->scaleIn() * bbox.ymax() * m_crossSections[i]->normalIn());
      pt = translateMaxIn(m_crossSections[i]->ctrLinePtIn());
      updateBbox();

      // pt out min
      Transformation translateMinOut(CGAL::TRANSLATION,
        m_crossSections[i]->scaleOut() * bbox.ymin() * m_crossSections[i]->normalOut());
      pt = translateMinOut(m_crossSections[i]->ctrLinePtOut());
      updateBbox();

      // pt out max
      Transformation translateMaxOut(CGAL::TRANSLATION,
        m_crossSections[i]->scaleOut() * bbox.ymax() * m_crossSections[i]->normalOut());
      pt = translateMaxOut(m_crossSections[i]->ctrLinePtOut());
      updateBbox();
    }
  }

  m_simuParams.bbox[0] = Point(bboxXZ.first.x, bboxXZ.first.y);
  m_simuParams.bbox[1] = Point(bboxXZ.second.x, bboxXZ.second.y);
}

//*************************************************************************
// Set the bounding box to sepcified values

void Acoustic3dSimulation::setBoundingBox(pair<Point2D, Point2D> &bbox)
{
  m_simuParams.bbox[0] = Point(bbox.first.x, bbox.first.y);
  m_simuParams.bbox[1] = Point(bbox.second.x, bbox.second.y);
}

//*************************************************************************

bool Acoustic3dSimulation::importGeometry(VocalTract* tract)
{
  std::cout << "[Acoustic3dSim_DEBUG] importGeometry ENTRY - m_reloadGeometry: " << m_reloadGeometry << ", m_geometryImported: " << m_geometryImported << ", m_geometryFile: " << m_geometryFile << std::endl;

  if (m_reloadGeometry)
  {
    ofstream log("log.txt", ofstream::app);
    log << "[Acoustic3dSim_DEBUG] importGeometry: m_reloadGeometry is true." << std::endl;

    auto start = std::chrono::system_clock::now();

    std::cout << "[Acoustic3dSim_DEBUG] importGeometry: About to call createCrossSections." << std::endl;
    if (createCrossSections(tract, false))
    {
      log << "Geometry successfully imported" << endl;
      std::cout << "[Acoustic3dSim_DEBUG] importGeometry: createCrossSections returned true." << std::endl;

      auto end = std::chrono::system_clock::now();
      std::chrono::duration<double> elapsed_seconds = end - start;

      log << "Time import geometry " << elapsed_seconds.count() << endl;

      m_reloadGeometry = false;

      log.close();
      std::cout << "[Acoustic3dSim_DEBUG] importGeometry EXIT - returning true (geometry loaded/reloaded)." << std::endl;
      return true;
    }
    else
    {
      log << "Importation failed" << endl;
      std::cout << "[Acoustic3dSim_DEBUG] importGeometry: createCrossSections returned false." << std::endl;
      m_reloadGeometry = false; 

      log.close();
      std::cout << "[Acoustic3dSim_DEBUG] importGeometry EXIT - returning false (importation failed)." << std::endl;
      return false;
    }
  }
  else
  {
    std::cout << "[Acoustic3dSim_DEBUG] importGeometry EXIT - returning true (m_reloadGeometry was false)." << std::endl;
    return true;
  }
}

//*************************************************************************
// Interpolate linearly the transfer function

complex<double> Acoustic3dSimulation::interpolateTransferFunction(double freq, int idxPt
  , enum tfType type)
{
  complex<double> interpolatedTF;
  double freqSteps((double)SAMPLING_RATE/2./(double)m_numFreqPicture);
  double tf[2];
  int idxFreqs[2];
  int maxIdxFreq(m_tfFreqs.size() - 1);

  Eigen::MatrixXcd *inputTf; 
  switch (type)
  {
  case GLOTTAL:
    inputTf = &m_glottalSourceTF;
    break;
  case NOISE:
    inputTf = &m_noiseSourceTF;
    break;
  case INPUT_IMPED:
    {
      inputTf = &m_planeModeInputImpedance;
      idxPt = 0;
      break;
    }
  }
  
  // Check if a transfer function have been computed
  if ((inputTf->rows() > 0) && (m_tfFreqs.size() > 0))
  {
    // Make sure that the index of the point corresponds to an actual point
    idxPt = min((int)inputTf->cols(), idxPt);
    // Check if the frequency is in the interval of frequencies computed
    if (freq >= m_tfFreqs[0] && freq <= m_tfFreqs.back())
    {
      // find the indexes before and after the frequency at which the interpolation is done
      idxFreqs[0] = (int)(freq / freqSteps);
      idxFreqs[1] = min(maxIdxFreq, idxFreqs[0] + 1);

      tf[0] = log10(abs((*inputTf)(idxFreqs[0], idxPt)));
      tf[1] = log10(abs((*inputTf)(idxFreqs[1], idxPt)));

      // interpolate the transfer function
      interpolatedTF = pow(10., tf[0] + ((tf[1] - tf[0]) * 
         (freq - idxFreqs[0] * freqSteps)) / freqSteps);
    }
    else
    {
      interpolatedTF = complex<double>(NAN, NAN);
    }
  }
  else
  {
    interpolatedTF = complex<double>(NAN, NAN);
  }

  return(interpolatedTF);
}

//*************************************************************************

void Acoustic3dSimulation::interpolateTransferFunction(vector<double>& freq, int idxPt, 
  enum tfType type, vector<complex<double>>& interpolatedValues)
{
  interpolatedValues.clear();
  interpolatedValues.reserve(freq.size());

  for (auto f : freq)
  {
    interpolatedValues.push_back(
      interpolateTransferFunction(f, idxPt, type)
    );
  }
}

//*************************************************************************
// Interpolate the acoustic field

double Acoustic3dSimulation::interpolateAcousticField(Point querryPt)
{
  double field;

  // check if the point is inside the bounding box
  if ((querryPt.x() > m_simuParams.bbox[0].x()) &&
    (querryPt.x() < m_simuParams.bbox[1].x()) &&
    (querryPt.y() > m_simuParams.bbox[0].y()) &&
    (querryPt.y() < m_simuParams.bbox[1].y()))
  {
    // find the neighboring points
    double dx(1. / (double)m_simuParams.fieldResolution);

    int iMin(min((int)m_field.rows() - 2,
      (int)floor((querryPt.y() - m_simuParams.bbox[0].y()) / dx)));
    int iMax(min((int)m_field.rows() - 1,
      (int)ceil((querryPt.y() - m_simuParams.bbox[0].y()) / dx)));
    int jMin(min((int)m_field.cols() - 2,
      (int)floor((querryPt.x() - m_simuParams.bbox[0].x()) / dx)));
    int jMax(min((int)m_field.cols() - 1,
      (int)ceil((querryPt.x() - m_simuParams.bbox[0].x()) / dx)));

    field = abs((m_field(iMin, jMin) + m_field(iMin, jMax)
      + m_field(iMax, jMin) + m_field(iMax, jMax)) / 4.);
  }
  else
  {
    field = -1.;
  }

  return field;
}

//*************************************************************************
// Interpolate the acoustic field

void Acoustic3dSimulation::interpolateAcousticField(Vec &coordX, Vec &coordY, Matrix &field)
{
  auto start = std::chrono::system_clock::now();
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> time = end - end;

  int nx(coordX.size());
  int ny(coordY.size());
  int iMin, jMin;
  double dx(1. / (double)m_simuParams.fieldResolutionPicture);
  complex<double> interpolatedField;

  field.resize(ny, nx);

  for (int i(0); i < ny; i++)
  {
    for (int j(0); j < nx; j++)
    {
      if ((coordX(j) > m_simuParams.bboxLastFieldComputed[0].x()) &&
        (coordX(j) < m_simuParams.bboxLastFieldComputed[1].x()) &&
        (coordY(i) > m_simuParams.bboxLastFieldComputed[0].y()) &&
        (coordY(i) < m_simuParams.bboxLastFieldComputed[1].y()))
      {
        iMin = min((int)m_field.rows() - 2,
          (int)floor((coordY(i) - m_simuParams.bboxLastFieldComputed[0].y()) / dx));
        jMin = min((int)m_field.cols() - 2,
          (int)floor((coordX(j) - m_simuParams.bboxLastFieldComputed[0].x()) / dx));

        start = std::chrono::system_clock::now();

        interpolatedField = (m_field(iMin, jMin) + m_field(iMin, jMin + 1)
          + m_field(iMin + 1, jMin) + m_field(iMin + 1, jMin + 1)) / 4.;

        if (m_simuParams.showAmplitude)
        {
          field(i, j) = abs(interpolatedField);
        }
        else
        {
          field(i, j) = arg(interpolatedField);
        }

        end = std::chrono::system_clock::now();
        time += end - start;
      }
      else
      {
        field(i, j) = NAN;
      }
    }
  }
}

//*************************************************************************
// Export the geometry extracted as CSV file

bool Acoustic3dSimulation::exportGeoInCsv(string fileName)
{
  ofstream of(fileName);
  stringstream strX, strY;
  string separator(";");
  Point Pt;
  Vector N;
  int lastSeg(m_crossSections.size() - 1);

  if (of.is_open())
  {
    for (int i(0); i <= lastSeg; i++)
    {
      // the junction segments are
      // skipped since they are added in the geometry creation process
      if (!m_crossSections[i]->isJunction())
      {
        // for the last segment, the centerline point and normal
        // are taken at the exit
        if (i == lastSeg)
        {
          Pt = m_crossSections[i]->ctrLinePtOut();
          N = m_crossSections[i]->normalOut();
        }
        else
        {
          Pt = Point(m_crossSections[i]->ctrLinePt().x,
            m_crossSections[i]->ctrLinePt().y);
          N = Vector(m_crossSections[i]->normal().x,
            m_crossSections[i]->normal().y);
        }

        // write centerline point coordinates
        strX << Pt.x() << separator;
        strY << Pt.y() << separator;

        // write normal coordinates
        strX << N.x() << separator;
        strY << N.y() << separator;

        // write scaling factors
        strX << m_crossSections[i]->scaleIn() << separator;
        strY << m_crossSections[i]->scaleOut() << separator;

        // write contour
        for (auto pt : m_crossSections[i]->contour())
        {
          strX << m_crossSections[i]->scaleIn() * pt.x() << separator;
          strY << m_crossSections[i]->scaleIn() * pt.y() << separator;
        }

        of << strX.str() << endl << strY.str() << endl;

        strX.str("");
        strX.clear();
        strY.str("");
        strY.clear();
      }
    }
    return(true);
  }
  else
  {
  return(false);
 }
  of.close();
}

//*************************************************************************
// Export the transfer functions in a text file

bool Acoustic3dSimulation::exportTransferFucntions(string fileName, enum tfType type)
{
  ofstream log("log.txt", ofstream::app);
  log << "Export transfer function to file:" << endl;
  log << fileName << endl;

  ofstream ofs;
  ofs.open(fileName, ofstream::out | ofstream::trunc);

  for (int i(0); i < m_tfFreqs.size(); i++)
  {
    ofs << m_tfFreqs[i] << "  ";
    if (type == INPUT_IMPED)
    {
      ofs << abs(m_planeModeInputImpedance(i, 0)) << "  "
        << arg(m_planeModeInputImpedance(i, 0));
    }
    else
    {
      for (int p(0); p < m_simuParams.tfPoint.size(); p++)
      {
        switch (type)
        {
        case GLOTTAL:
          ofs << abs(m_glottalSourceTF(i, p)) << "  "
            << arg(m_glottalSourceTF(i, p)) << "  ";
          break;
        case NOISE:
          ofs << abs(m_noiseSourceTF(i, p)) << "  "
            << arg(m_noiseSourceTF(i, p)) << "  ";
          break;
        }
      }
    }
    ofs << endl;
  }
  ofs.close();

  log.close();

  // FIXME: Check if the file have been successfully opened
  return true;
}

//*************************************************************************
// Export the acoustic field in a text file

bool Acoustic3dSimulation::exportAcousticField(string fileName)
{
  ofstream log("log.txt", ofstream::app);
  log << "Export acoustic field to file:" << endl;
  log << fileName << endl;

  ofstream ofs;
  ofs.open(fileName, ofstream::out | ofstream::trunc);

  stringstream txtField;
  if (m_simuParams.showAmplitude)
  {
    txtField << m_field.cwiseAbs();
  }
  else
  {
    txtField << m_field.array().arg();
  }
  ofs << regex_replace(txtField.str(), regex("-nan\\(ind\\)"), "nan");

  ofs.close();
  log.close();

  // FIXME: Check if the file have been successfully opened
  return true;
}

//*************************************************************************
// Interpolate the radiation and admittance matrices with splines

void Acoustic3dSimulation::preComputeRadiationMatrices(int nbRadFreqs, int idxRadSec) {

  // initialize coefficient structures
  initCoefInterpRadiationMatrices(nbRadFreqs, idxRadSec);

  // loop over a few frequencies
  for (int i(0); i < nbRadFreqs; i++)
  {
    addRadMatToInterpolate(nbRadFreqs, idxRadSec, i);
  }

  computeInterpCoefRadMat(nbRadFreqs, idxRadSec);
}

//*************************************************************************

void Acoustic3dSimulation::initCoefInterpRadiationMatrices(int nbRadFreqs, int idxRadSec)
{
  int mn(m_crossSections[idxRadSec]->numberOfModes());
  m_radiationFreqs.clear();
  m_radiationFreqs.reserve(nbRadFreqs);

  // initialize coefficient structures
  //        a       |        b       |        c       |       d       
  // Zr  Zi  Ir  Ii | Zr  Zi  Ir  Ii | Zr  Zi  Ir  Ii | Zr  Zi  Ir  Ii    
  // 0   1   2   3  | 4   5   6   7  | 8   9   10  11 | 12  13  14  15
  m_radiationMatrixInterp.clear();
  for (int m(0); m < 16; m++)
  {
    m_radiationMatrixInterp.push_back(vector<vector<vector<double>>>());
    for (int i(0); i < mn; i++) {

      m_radiationMatrixInterp.back().push_back(vector<vector<double>>());

      for (int j(0); j < mn; j++)
      {
        m_radiationMatrixInterp.back().back().push_back(vector<double>());
        m_radiationMatrixInterp.back().back().back().reserve(nbRadFreqs);
      }
    }
  }
}

//*************************************************************************

void Acoustic3dSimulation::addRadMatToInterpolate(int nbRadFreqs, int idxRadSec, int idxRadFreq)
{
  double radFreqSteps((double)SAMPLING_RATE / 2. / (double)(nbRadFreqs - 1));
  double freq(max(500., (double)idxRadFreq * radFreqSteps));
  int mn(m_crossSections[idxRadSec]->numberOfModes());
  Eigen::MatrixXcd radImped(mn, mn), radAdmit(mn, mn);

  ofstream log("log.txt", ofstream::app);

  m_radiationFreqs.push_back(freq);

  radiationImpedance(radImped, freq, 15., idxRadSec);
  radAdmit = radImped.inverse();

  // Create first spline coefficient
  for (int m(0); m < mn; m++)
  {
    for (int n(0); n < mn; n++)
    {
      m_radiationMatrixInterp[0][m][n].push_back(radImped(m, n).real());
      m_radiationMatrixInterp[1][m][n].push_back(radImped(m, n).imag());
      m_radiationMatrixInterp[2][m][n].push_back(radAdmit(m, n).real());
      m_radiationMatrixInterp[3][m][n].push_back(radAdmit(m, n).imag());
    }
  }

  log << "Freq " << freq << " Hz " << idxRadFreq + 1 << " over " << nbRadFreqs << endl;

  log.close();
}

//*************************************************************************

void Acoustic3dSimulation::computeInterpCoefRadMat(int nbRadFreqs, int idxRadSec)
{
  vector<double> stepRadFreqs;
  stepRadFreqs.reserve(nbRadFreqs - 1);
  int mn(m_crossSections[idxRadSec]->numberOfModes());
  vector<double>* a, * b, * c, * d;
  Matrix A(Matrix::Zero(nbRadFreqs - 2, nbRadFreqs - 2)), imped(mn, mn);
  Eigen::VectorXd R(Eigen::VectorXd::Zero(nbRadFreqs - 2)), B;

  for (int i(0); i < nbRadFreqs - 1; i++)
  {
    stepRadFreqs.push_back(m_radiationFreqs[i + 1] - m_radiationFreqs[i]);
  }

  // compute the spline coefficients
  for (int m(0); m < 4; m++)
  {
    for (int i(0); i < mn; i++)
    {
      for (int j(0); j < mn; j++)
      {
        A.setZero(nbRadFreqs - 2, nbRadFreqs - 2);
        R.setZero(nbRadFreqs - 2);

        a = &m_radiationMatrixInterp[m][i][j];
        b = &m_radiationMatrixInterp[m + 4][i][j];
        c = &m_radiationMatrixInterp[m + 8][i][j];
        d = &m_radiationMatrixInterp[m + 12][i][j];

        // build matrice A and vector R to solve the equation A * c = R
        // in order to find the coefficient c of the spline
        A(0, 0) = 2 * (stepRadFreqs[0] + stepRadFreqs[1]);
        A(0, 1) = stepRadFreqs[1];
        R(0) = 3. * ((*a)[2] - (*a)[1]) / stepRadFreqs[1]
          - 3 * ((*a)[1] - (*a)[0]) / stepRadFreqs[0];

        for (int f(1); f < nbRadFreqs - 3; f++)
        {
          A(f, f - 1) = stepRadFreqs[f];
          A(f, f) = 2. * (stepRadFreqs[f] + stepRadFreqs[f + 1]);
          A(f, f + 1) = stepRadFreqs[f + 1];
          R(f) = 3. * ((*a)[f + 2] - (*a)[f + 1]) / stepRadFreqs[f + 1]
            - 3. * ((*a)[f + 1] - (*a)[f]) / stepRadFreqs[f];
        }

        A(nbRadFreqs - 3, nbRadFreqs - 4) = stepRadFreqs[nbRadFreqs - 3];
        A(nbRadFreqs - 3, nbRadFreqs - 3) = 2. * (stepRadFreqs[nbRadFreqs - 3] + stepRadFreqs[nbRadFreqs - 2]);
        R(nbRadFreqs - 3) = 3. * ((*a)[nbRadFreqs - 1] - (*a)[nbRadFreqs - 2])
          / stepRadFreqs[nbRadFreqs - 2]
          - 3. * ((*a)[nbRadFreqs - 2] - (*a)[nbRadFreqs - 3])
          / stepRadFreqs[nbRadFreqs - 3];

        // compute c coefficient
        B = A.householderQr().solve(R);
        (*c).push_back(0.);
        for (int f(0); f < B.rows(); f++)
        {
          (*c).push_back(B(f));
        }
        (*c).push_back(0.);

        // compute b coefficient
        for (int f(0); f < nbRadFreqs - 1; f++)
        {
          (*b).push_back(((*a)[f + 1] - (*a)[f])
            / stepRadFreqs[f] - stepRadFreqs[f] *
            ((*c)[f + 1] + 2. * (*c)[f]) / 3.);
          (*d).push_back(((*c)[f + 1] - (*c)[f]) / 3. / stepRadFreqs[f]);
        }
      }
    }
  }
  m_simuParams.radImpedPrecomputed = true;
}

//*************************************************************************
// Interpolate the radiation impedance matrix at a given frequency

void Acoustic3dSimulation::interpolateRadiationImpedance(Eigen::MatrixXcd& imped, 
   double freq, int idxRadSec)
{
  // find the index corresponding to the coefficient to use for this frequency
  int nbRadFreqs(m_radiationMatrixInterp[0][0][0].size());
  int idx(nbRadFreqs - 2);
  while ((m_radiationFreqs[idx] > freq) && (idx > 0)) { idx--; }
  idx = max(0, idx);

  // get number of modes of the radiating section
  int mn(m_crossSections[idxRadSec]->numberOfModes());

  imped.setZero(mn, mn);
  for (int m(0); m < mn; m++)
  {
    for (int n(0); n < mn; n++)
    {
      imped(m, n) = complex<double>(m_radiationMatrixInterp[0][m][n][idx] +
        m_radiationMatrixInterp[4][m][n][idx] * (freq - m_radiationFreqs[idx]) +
        m_radiationMatrixInterp[8][m][n][idx] * pow(freq - m_radiationFreqs[idx], 2) +
        m_radiationMatrixInterp[12][m][n][idx] * pow(freq - m_radiationFreqs[idx], 3), 
        (m_radiationMatrixInterp[1][m][n][idx] +
        m_radiationMatrixInterp[5][m][n][idx] * (freq - m_radiationFreqs[idx]) +
        m_radiationMatrixInterp[9][m][n][idx] * pow(freq - m_radiationFreqs[idx], 2) +
        m_radiationMatrixInterp[13][m][n][idx] * pow(freq - m_radiationFreqs[idx], 3)));
    }
  }
}

//*************************************************************************
// Interpolate the radiation admittance matrix at a given frequency

void Acoustic3dSimulation::interpolateRadiationAdmittance(Eigen::MatrixXcd& admit, 
  double freq, int idxRadSec)
{
  // find the index corresponding to the coefficient to use for this frequency
  int nbRadFreqs(m_radiationMatrixInterp[0][0][0].size());
  int idx(nbRadFreqs - 2);
  while ((m_radiationFreqs[idx] > freq) && (idx > 0)) { idx--; }
  idx = max(0, idx);

  // get number of modes of the radiating section
  int mn(m_crossSections[idxRadSec]->numberOfModes());

  admit.setZero(mn, mn);
  for (int m(0); m < mn; m++)
  {
    for (int n(0); n < mn; n++)
    {
      admit(m, n) = complex<double>(m_radiationMatrixInterp[2][m][n][idx] +
        m_radiationMatrixInterp[6][m][n][idx] * (freq - m_radiationFreqs[idx]) +
        m_radiationMatrixInterp[10][m][n][idx] * pow(freq - m_radiationFreqs[idx], 2) +
        m_radiationMatrixInterp[14][m][n][idx] * pow(freq - m_radiationFreqs[idx], 3),
        (m_radiationMatrixInterp[3][m][n][idx] +
        m_radiationMatrixInterp[7][m][n][idx] * (freq - m_radiationFreqs[idx]) +
        m_radiationMatrixInterp[11][m][n][idx] * pow(freq - m_radiationFreqs[idx], 2) +
        m_radiationMatrixInterp[15][m][n][idx] * pow(freq - m_radiationFreqs[idx], 3)));
    }
  }
}

//*************************************************************************
// Compute the radiation impedance according to Blandin et al 2019
// Multimodal radiation impedance of a waveguide with arbitrary
// cross - sectional shape terminated in an infinite baffle

void Acoustic3dSimulation::radiationImpedance(Eigen::MatrixXcd& imped, double freq, 
  double gridDensity, int idxRadSec)
{
  int mn(m_crossSections[idxRadSec]->numberOfModes());

  imped = Eigen::MatrixXcd::Zero(mn, mn);
  Eigen::MatrixXcd integral2(mn, mn);

  //******************************
  // generate cartesian grid
  //******************************

  // very good precision is obtained with gridDensity = 30
  //  good precision is obtained with gridDensity = 15;
  double scaling(m_crossSections[idxRadSec]->scaleOut());
  double spacing(sqrt(m_crossSections[idxRadSec]->area()) 
    / gridDensity);

  //Transformation scale(CGAL::SCALING, scaling);
  Polygon_2 contour(m_crossSections[idxRadSec]->contour());
  vector<Point> cartGrid;
  Point pt;
  double xmin(contour.bbox().xmin());
  double ymin(contour.bbox().ymin());
  int nx(ceil((contour.bbox().xmax() - xmin) / spacing));
  int ny(ceil((contour.bbox().ymax() - ymin) / spacing));
  for (int i(0); i < nx; i++)
  {
    for (int j(0); j < ny; j++)
    {
      pt = Point(xmin + i * spacing, ymin + j * spacing);
      if (contour.has_on_bounded_side(pt))
      {
        cartGrid.push_back(pt);
      }
    }
  }

  // Interpolate the propagation modes on the cartesian grid
  Matrix intCartGrid(m_crossSections[idxRadSec]->interpolateModes(cartGrid));

  // loop over the points of the cartesian grid
  for (int c(0); c < cartGrid.size(); c++)
  {

    //******************************
    // generate polar grid
    //******************************

    //FIXME: don't work for lines intersecting several times the polygon

    int numDirections;
    double angleSpacing, direction;
    vector<Point> polGrid;
    vector<double> radius;
    int cnt, nbPts(0);
    double r, sumH;
    Point ptToAdd;

    // get center point from the cartesian grid
    pt = cartGrid[c];

    // estimate the ratio [number of direction] / [number of point]
    numDirections = 50;
    angleSpacing = 2. * M_PI / (double)numDirections;
    for (int i(0); i < numDirections; i++)
    {
      direction = (double)i * angleSpacing - M_PI;
      cnt = 0;
      r = (0.5 + (double)cnt) * spacing;
      ptToAdd = Point(r * cos(direction) + pt.x(), r * sin(direction) + pt.y());
      while (contour.has_on_bounded_side(ptToAdd))
      {
        nbPts++;
        cnt++;
        r = (0.5 + (double)cnt) * spacing;
        ptToAdd = Point(r * cos(direction) + pt.x(), r * sin(direction) + pt.y());
      }
    }

    // Rough estimate of the number of needed directions
    numDirections = cartGrid.size() * numDirections / nbPts;

    // generate angles of the polar grid
    angleSpacing = 2. * M_PI / (double)numDirections;
    for (int i(0); i < numDirections; i++)
    {
      direction = (double)i * angleSpacing - M_PI;

      // generate the points of the polar grid for each direction
      cnt = 0;
      r = (0.5 + (double)cnt) * spacing;
      ptToAdd = Point(r * cos(direction) + pt.x(), r * sin(direction) + pt.y());
      while (contour.has_on_bounded_side(ptToAdd))
      {
        polGrid.push_back(ptToAdd);
        radius.push_back(r);
        cnt++;
        r = (0.5 + (double)cnt) * spacing;
        ptToAdd = Point(r * cos(direction) + pt.x(), r * sin(direction) + pt.y());
      } 
    }

    // interpolate the polar grid
    Matrix intPolGrid(m_crossSections[idxRadSec]->interpolateModes(polGrid));

    //******************************
    // Compute first integral
    //******************************

    sumH = 0;
    integral2.setZero();

    // loop over the points of the polar grid
    for (int p(0); p < polGrid.size(); p++)
    {
      sumH += radius[p];

      // loop over the modes
      for (int m(0); m < mn; m++)
      {
        // loop over the modes
        for (int n(0); n < mn; n++)
        {
          integral2(m, n) += intPolGrid(p, m) * intCartGrid(c, n) *
            exp(-1i * 2. * M_PI * freq * scaling * radius[p] / m_simuParams.sndSpeed);
        }
      }
    }

    imped += - integral2 / sumH / 2. / M_PI / cartGrid.size() / scaling;
  }

  imped *= pow(m_crossSections[idxRadSec]->area(),2);
}

//*************************************************************************
// get the radiation impedance 

void Acoustic3dSimulation::getRadiationImpedanceAdmittance(Eigen::MatrixXcd& imped,
  Eigen::MatrixXcd& admit, double freq, int idxRadSec)
{
  if (m_simuParams.radImpedPrecomputed)
  {
    interpolateRadiationImpedance(imped, freq, idxRadSec);
    interpolateRadiationAdmittance(admit, freq, idxRadSec);
  }
  else
  {
    radiationImpedance(imped, freq, m_simuParams.radImpedGridDensity, idxRadSec);
    admit = imped.inverse();
  }
}
