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

#include "CrossSection2d.h"
#include "Constants.h"
#include "Tube.h"
#include <iostream>
#include <Eigen/Core>
#include <chrono>    // to get the computation time
#include <ctime>  
#include <algorithm>

// for boost
#include <boost/math/special_functions/bessel.hpp>
#include <boost/math/quadrature/gauss.hpp>
#define cyl_bessel_j boost::math::cyl_bessel_j

// for Eigen
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <unsupported/Eigen/MatrixFunctions>

// for CGAL
#include <CGAL/lloyd_optimize_mesh_2.h>
#include <CGAL/Aff_transformation_2.h>
// for interpolation
#include <CGAL/squared_distance_2.h>
#include <CGAL/natural_neighbor_coordinates_2.h>
#include <CGAL/interpolation_functions.h>
#include <CGAL/point_generators_2.h>
#include <CGAL/algorithm.h>
#include <CGAL/Origin.h>

// typedef for eigen
typedef Eigen::MatrixXd Matrix;
typedef Eigen::Triplet<complex<double>> Triplet;
typedef Eigen::SparseMatrix<complex<double>> SparseMatC;

// typedef for CGAL
typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Delaunay_mesh_vertex_base_with_info_2<unsigned int, K>    Vb;
typedef CGAL::Delaunay_mesh_face_base_2<K> Fb;
typedef CGAL::Triangulation_data_structure_2<Vb, Fb> Tds;
typedef CGAL::Exact_intersections_tag                     Itag;
typedef CGAL::Constrained_Delaunay_triangulation_2<K, Tds, Itag> CDT;
typedef CGAL::Delaunay_triangulation_2<K>             Delaunay_triangulation;
typedef CGAL::Delaunay_mesh_size_criteria_2<CDT> Criteria;
typedef CGAL::Delaunay_mesher_2<CDT, Criteria> Mesher;

typedef CDT::Point                    Point;
typedef CGAL::Point_3<K>                Point_3;
typedef CGAL::Vector_2<K>                Vector;
typedef CDT::Triangulation::Finite_faces_iterator    Finite_faces_iterator;

typedef CGAL::Polygon_2<K>                Polygon_2;
typedef CGAL::Aff_transformation_2<K>        Transformation;

// for the interpolation
typedef K::FT                      Coord_type;
typedef K::Vector_2                    Vector;
typedef std::vector< std::pair<Point, Coord_type> >    Coordinate_vector;
typedef std::map<Point, Coord_type, K::Less_xy_2>    Point_value_map;
typedef CGAL::Data_access<Point_value_map>        Value_access;

// ****************************************************************************
// Initialize constants
// ****************************************************************************

const double NON_SENS_VALUE = 1e14;


// ****************************************************************************
// Independant function
// ****************************************************************************

// ****************************************************************************
// Return the distance between a point and a poloygon 

double distancePolygon(Polygon_2 poli, Point ptToTest)
{
  Point2D vecEdge;
  Point2D vecEdgeVert;
  double scalarProd;
  Point2D distVec;
  double distEdge(NON_SENS_VALUE);

  // loop over the edges of the contour
  for (auto it = poli.edges_begin(); it != poli.edges_end(); ++it)
  {

    // define the vector corresponding to the edge
    vecEdge.set(it->point(1).x() - it->point(0).x(), it->point(1).y() - it->point(0).y());

    // define the vector connecting the edge to the tested point
    vecEdgeVert.set(it->point(0).x() - ptToTest.x(), it->point(0).y() - ptToTest.y());


    // compute the scalar product between  the edge vector and the 
    // contour to vertex vector
    scalarProd = scalarProduct(vecEdge, vecEdgeVert);

    // normalise the scalar product with the norm of the edge vector
    scalarProd = -scalarProd / pow(vecEdge.magnitude(), 2);

    // check if the projection of the vertex lies on the contour segment 
    if (scalarProd < 0)
    {
      scalarProd = 0;
    }
    else if (scalarProd > 1)
    {
      scalarProd = 1;
    }

    // compute the vector linking the projection to the vertex
    distVec.set(vecEdge.x * scalarProd + vecEdgeVert.x,
      vecEdge.y * scalarProd + vecEdgeVert.y);

    // compute the minimal distance between the vertex and the vector
    distEdge = min(distEdge, distVec.magnitude());
  }
  return distEdge *= pow(-1.0, (double)poli.has_on_bounded_side(ptToTest));
}

// ****************************************************************************
// Bring back on  the boundary a point which is outside of a polygon

Point bringBackPointInsideContour(Polygon_2 poli, Point pt, double spacing)
{
  double deltaXGrad(sqrt(MINIMAL_DISTANCE) * spacing);
  double distCont(distancePolygon(poli, pt));
  CGAL::Vector_2<K> gradVec((distancePolygon(poli, Point(pt.x() + deltaXGrad, pt.y()))
    - distCont) / deltaXGrad,
    (distancePolygon(poli, Point(pt.x(), pt.y() + deltaXGrad))
      - distCont) / deltaXGrad);
  return pt -= ((MINIMAL_DISTANCE + distCont) * gradVec);
}

// ****************************************************************************
// Compute the n first zeros of the derivative of the Bessel function Jv 

void BesselJDerivativeZero(int v, int n, map<double, pair<int,int>> &zeros)
{
  using namespace boost::math;
  using namespace boost::math::tools;

  double estimatedZero, minVal, maxVal, res;

  // parameters for McMahon expansion
  double b, mu{ 4. * pow((double)(v),2) };

  // precision for Newton Raphson
  int digits = numeric_limits<double>::digits; 
  digits = static_cast<int>(digits * 0.6);

  // define the derivative of the besselfunction of which one wants 
  // to find the zero and its derivative
  auto function = [&](double z) {
    double dJv = 0.5 * (cyl_bessel_j(v - 1, z) - cyl_bessel_j(v + 1, z));
    double d2Jv = 0.25 * (cyl_bessel_j(v - 2, z) -
      2. * cyl_bessel_j(v, z) + cyl_bessel_j(v + 2, z));
    return make_pair(dJv, d2Jv);
  };

  // loop over orders
  for (int i(1); i <= n; i++)
  {
    if ((v == 0) && (i == 1))
    {
      zeros.insert({ 0., {0,0} });
    }
    else
    {
      // estimate the value of the zeros using McMahon's aymptotic expansion
      // https://dlmf.nist.gov/10.21#vi
      b = ((double)(i) + 0.5 * (double)v - 0.75) * M_PI;
      estimatedZero = b - (mu + 3.) / 8. / b -
        4. * (7 * mu * mu + 82. * mu - 9.) / 3. / pow(8. * b, 3) -
        32. * (83. * mu * mu * mu + 2075. * mu * mu - 3039 * mu + 3537) / 15. / pow(8. * b, 5) -
        64. * (6949 * mu * mu * mu * mu + 296492. * mu * mu * mu - 1248002. * mu * mu + 7414380. * mu - 5853627.)
        / 105. / pow(8. * b, 7);

      minVal = estimatedZero -0.5;
      maxVal = estimatedZero +0.5;


      res = newton_raphson_iterate(function, estimatedZero, minVal,
        maxVal, digits);

      zeros.insert({ res, {v, i - 1} });
    }
  }
}

// ****************************************************************************
/// Constructors
// ****************************************************************************

CrossSection2d::CrossSection2d()
{
  m_ctrLinePt = Point2D(0., 0.);
  m_normal = Point2D(0., 1.);
  m_modesNumber = 0;
  setZdir(-1);
  setYdir(-1);
  setQdir(1);
  setPdir(1);
}

CrossSection2d::CrossSection2d(Point2D ctrLinePt, Point2D normal)
  : m_ctrLinePt(ctrLinePt),
  m_normal(normal),
  m_modesNumber(0)
{
  setZdir(-1);
  setYdir(-1);
  setQdir(1);
  setPdir(1);
}

CrossSection2dFEM::CrossSection2dFEM(Point2D ctrLinePt, Point2D normal,
  double area, double spacing, Polygon_2 contour, vector<int> surfacesIdx,
double inLength, double scalingFactors[2])
  : CrossSection2d(ctrLinePt, normal),
  m_length(inLength),
  m_junctionSection(false),
  m_spacing(spacing),
  m_contour(contour),
  m_surfaceIdx(surfacesIdx)
  
  {
    for (int i(0); i < 2; i++) { m_scalingFactors[i] = scalingFactors[i]; }
    m_area = area;
    // compute perimeter
    m_perimeter = 0.;
    for (auto it = m_contour.edges_begin();
      it != m_contour.edges_end(); it++)
    {
      m_perimeter += sqrt(it->squared_length());
    }
    m_areaProfile = LINEAR;
    m_curvatureRadius = 0.;
    m_circleArcAngle = 0.;
  }

CrossSection2dRadiation::CrossSection2dRadiation(
  Point2D ctrLinePt, Point2D normal, double radius, double PMLThickness)
  :CrossSection2d(ctrLinePt, normal),
  m_radius(radius),
  m_PMLThickness(PMLThickness)
  {
  m_area = M_PI * pow(radius, 2);
  }

// ****************************************************************************
/// Destructor.
// ****************************************************************************

CrossSection2d::~CrossSection2d(){}
CrossSection2dFEM::~CrossSection2dFEM() {}

// ****************************************************************************
// Define if the cross-section is a junction between two cross-sections

void CrossSection2dFEM::setJunctionSection(bool junction)
{ m_junctionSection = junction; }

// ****************************************************************************
// Add the index of a previous section

void CrossSection2d::setPreviousSection(int prevSec)
{
  m_previousSections.push_back(prevSec);
}

// ****************************************************************************
// Add the index of a next section

void CrossSection2d::setNextSection(int nextSec)
{
  m_nextSections.push_back(nextSec);
}

// ****************************************************************************
// Set the radius of curvature of the section

void CrossSection2dFEM::setCurvatureRadius(double radius) {
  m_curvatureRadius = radius;
}

// ****************************************************************************
// Set the angle of curvature of the section

void CrossSection2dFEM::setCurvatureAngle(double angle) {
  m_circleArcAngle = angle;
}

// ****************************************************************************
/// build meshes
// ****************************************************************************

// ****************************************************************************
// A simple mesh without constraint

void CrossSection2dFEM::buildMesh()
{
  int idx;
  m_points.clear();
  m_triangles.clear();
  m_meshContourSeg.clear();
  m_meshContourSeg.reserve(2 * m_contour.size());
  array<double, 2> pts;
  array<int, 3> tempTri;
  array<int, 2> tempSeg;

  m_mesh.clear();
  m_mesh.insert_constraint(m_contour.vertices_begin(), m_contour.vertices_end(), true);

  Mesher mesher(m_mesh);

  mesher.set_criteria(Criteria(0.125, m_spacing));

  mesher.refine_mesh();

  CGAL::lloyd_optimize_mesh_2(m_mesh,
    CGAL::parameters::max_iteration_number = 10);

  // store the point coordinates and attribute indexes to the vertexes
  idx = 0;
  for (auto it = m_mesh.finite_vertices_begin();
    it != m_mesh.finite_vertices_end(); ++it)
  {
    pts[0] = it->point().x();
    pts[1] = it->point().y();
    m_points.push_back(pts);
    it->info() = idx;
    idx++;
  }

  for (auto it = m_mesh.constrained_edges_begin(); it != m_mesh.constrained_edges_end(); it++)
  {
    // store the indexes of the points of the mesh which form the segments
    // of its contour
    tempSeg[0] = it->first->vertex(it->first->cw(it->second))->info();
    tempSeg[1] = it->first->vertex(it->first->ccw(it->second))->info();
    m_meshContourSeg.push_back(tempSeg);
  }

  // remove the faces which lies outside of the contour and extract the 
  // triangles indexes
  for (Finite_faces_iterator it = m_mesh.finite_faces_begin();
    it != m_mesh.finite_faces_end(); ++it)
  {
    // if the face is outside of the contour remove it
    if (!it->is_in_domain())
    {
      m_mesh.delete_face(it);
    }
    else
    {
      // otherwise extract the corresponding point indexes
      for (int v(0); v < 3; v++)
      {
        tempTri[v] = it->vertex(v)->info();
      }
      m_triangles.push_back(tempTri);
    }
  }
}

// ****************************************************************************
/// Modes computation
// ****************************************************************************

void CrossSection2dFEM::computeModes(struct simulationParameters simuParams)
{
  // declare variables
  bool different;
  int numVert, numSurf, numTri, idxM, idxN, ptIdx, idx, meshContSurfIdx;
  Matrix mass, massY, stiffness, stiffnessY, B, C, E, DN;
  vector<Matrix> R, KR2, RY, DR;
  double signFirstMode, faceArea, dist, oldDist, segLength, maxWaveNumber;
  Point2D midSeg;

  // elements for isoparametric mapping and quadrature integration
  double quadPtCoord[3][2]{ {1. / 6., 1. / 6.}, {2. / 3., 1. / 6.}, {1. / 6., 2. / 3.} };
  double quadPtWeight = 1. / 3.;
  double S[3][3];
  for (int i(0); i < 3; i++)
  {
    S[i][0] = 1. - quadPtCoord[i][0] - quadPtCoord[i][1];
    S[i][1] = quadPtCoord[i][0];
    S[i][2] = quadPtCoord[i][1];
  }
  double dSdr[3]{ -1., 1., 0. };
  double dSds[3]{ -1., 0., 1. };
  double J[2][2];
  double detJ, quadPtWeightDetJ, Xrs[3], Yrs[3], dSdx[3], dSdy[3];

  // **************************************************************************
  // Build matrices necessary to compute the multimodal matrices 
  // accounting for the wall losses DR and K2R
  // **************************************************************************

  // initialize the matrices
  numVert = (int)m_mesh.number_of_vertices();
  mass = Matrix::Zero(numVert, numVert);
  massY = Matrix::Zero(numVert, numVert);
  stiffness = Matrix::Zero(numVert, numVert);
  stiffnessY = Matrix::Zero(numVert, numVert);
  B = Matrix::Zero(numVert, numVert);
  numTri = (int)m_mesh.number_of_faces();

  // determine the number of different surfaces on the contours
  numSurf = 1;
  R.clear();
  R.push_back(Matrix::Zero(numVert, numVert));
  RY.clear();
  RY.push_back(Matrix::Zero(numVert, numVert));
  m_surfIdxList.clear();
  m_surfIdxList.push_back(m_surfaceIdx[0]);
  
  // loop over surface indexes of the contour
  for (ptIdx = 1; ptIdx < m_surfaceIdx.size(); ptIdx++)
  {
    // check if the surface type have already been registered
    different = true;
    for (int j(0); j < m_surfIdxList.size(); j++)
    {
      if (m_surfIdxList[j] == m_surfaceIdx[ptIdx])
      {
        different = false;
        break;
      }
    }

    // if not add it to the list of surface types and initialize a new submatrix
    if (different) { 
      m_surfIdxList.push_back(m_surfaceIdx[ptIdx]);
      R.push_back(Matrix::Zero(numVert, numVert));
      RY.push_back(Matrix::Zero(numVert, numVert));
      numSurf++;
    }
  }

  // loop over the portions of the contour of the mesh
  Point2D closestPt;
  for (int s(0); s < m_meshContourSeg.size(); s++)
  {
    // compute midle of the segment
    midSeg = Point2D(0.5 * (m_points[m_meshContourSeg[s][0]][0] +
      m_points[m_meshContourSeg[s][1]][0]),
      0.5 * (m_points[m_meshContourSeg[s][0]][1] +
        m_points[m_meshContourSeg[s][1]][1]));

    // determine the surface corresponding to the segment
    auto ct = m_contour.begin();
    ct->x();
    ptIdx = 0;
    closestPt = Point2D(ct->x(), ct->y());

    // distance between the first point of the contour and the midle of the segment
    oldDist = sqrt(pow(ct->x() - midSeg.x,2) + pow(ct->y() - midSeg.y, 2));

    // loop over points of the contour
    for (idx = 1, ct = m_contour.begin()+1; ct != m_contour.end()-1; idx++, ct++)
    {
      // distance between the first point of the contour and the midle of the segment
      dist = sqrt(pow(ct->x() - midSeg.x, 2) + pow(ct->y() - midSeg.y, 2));
      if (dist < oldDist)
      {
        oldDist = dist;
        ptIdx = idx;
        closestPt = Point2D(ct->x(), ct->y());
      }
    }
    meshContSurfIdx = m_surfaceIdx[ptIdx];

    // determine the index of the submatrix to which add the elements corresponding
    // to the current segment
    for (idx = 0; idx < m_surfIdxList.size(); idx++)
    {
      if (meshContSurfIdx == m_surfIdxList[idx])
      {
        break;
      }
    }

    // compute the length of the segment
    segLength = sqrt(pow(m_points[m_meshContourSeg[s][0]][0] -
      m_points[m_meshContourSeg[s][1]][0], 2) +
      pow(m_points[m_meshContourSeg[s][0]][1] -
        m_points[m_meshContourSeg[s][1]][1], 2));

    // loop over points of the segment
    for (int j(0); j < 2; j++)
    {
      // loop over points of the segment
      for (int k(0); k < 2; k++)
      {
        idxM = m_meshContourSeg[s][j];
        idxN = m_meshContourSeg[s][k];

        R[idx](idxM, idxN) += (1. + (double)(j == k)) * segLength / 6.;

        if ((j == k) && (k == 0))
        {
          // L*(Yn + 3Ym)/12
          RY[idx](idxM, idxN) += segLength * (m_points[m_meshContourSeg[s][k]][1] +
            3 * m_points[m_meshContourSeg[s][j]][1]) / 12.;
        }
        else if ((j == k) && (k == 1))
        {
          // L*(3Yn + Ym)/12
          RY[idx](idxM, idxN) += segLength * (3 * m_points[m_meshContourSeg[s][k]][1] +
            m_points[m_meshContourSeg[s][j]][1]) / 12.;
        }
        else
        {
          // L*(Yn + Ym)/12
          RY[idx](idxM, idxN) += segLength * ( m_points[m_meshContourSeg[s][k]][1] +
            m_points[m_meshContourSeg[s][j]][1]) / 12.;
        }
      }
    }
  }

  //ofstream matrixR;
  //matrixR.open("matrixR.txt");
  //for (int i(0); i < R.size(); i++)
  //{
  //  matrixR << R[i] << endl;
  //}
  //matrixR.close();

  //ofstream matrixRY;
  //matrixRY.open("matrixRY.txt");
  //for (int i(0); i < RY.size(); i++)
  //{
  //  matrixRY << RY[i] << endl;
  //}
  //matrixRY.close();

// **************************************************************************
// Build matrices necessary to solve Finite Element problem (Mass and Stiffness)
// And matrices necessary to compute multimodal matrices (massY, stiffnessY and B)
// **************************************************************************

  // loop over the faces
  for (CDT::Finite_faces_iterator it = m_mesh.finite_faces_begin() ;
    it != m_mesh.finite_faces_end(); ++it)
  {
    faceArea = 0.5 * abs(it->vertex(0)->point().x() *
      (it->vertex(1)->point().y() - it->vertex(2)->point().y())
      + it->vertex(1)->point().x() *
      (it->vertex(2)->point().y() - it->vertex(0)->point().y())
      + it->vertex(2)->point().x() *
      (it->vertex(0)->point().y() - it->vertex(1)->point().y()));

    // compute the Jacobian of the isoparametric transformation
    J[0][0] = 0.; J[0][1] = 0.; J[1][0] = 0.; J[1][1] = 0.;
    for (int p(0); p < 3; p++)
    {
      J[0][0] += (it->vertex(p)->point().x()) * dSdr[p];
      J[0][1] += (it->vertex(p)->point().y()) * dSdr[p];
      J[1][0] += (it->vertex(p)->point().x()) * dSds[p];
      J[1][1] += (it->vertex(p)->point().y()) * dSds[p];
    }
    detJ = J[0][0] * J[1][1] - J[0][1] * J[1][0];
    quadPtWeightDetJ = quadPtWeight * detJ / 2.;

    // compute the partial derivative dS/dx and dS/dy
    for (int p(0); p < 3; p++)
    {
      dSdx[p] = (J[1][1] * dSdr[p] - J[0][1] * dSds[p]) / detJ;
      dSdy[p] = (J[0][0] * dSds[p] - J[1][0] * dSdr[p]) / detJ;
    }

    // compute the x and y coordinate of the quadrature points
    // loop over quadrature points
    for (int q(0); q < 3; q++) { Xrs[q] = 0.;  Yrs[q] = 0.; }
    for (int q(0); q < 3; q++)
    {
      // loop over face points
      for (int p(0); p < 3; p++)
      {
        Xrs[q] += (it->vertex(p)->point().x()) * S[q][p];
        Yrs[q] += (it->vertex(p)->point().y()) * S[q][p];
      }
    }

    // loop over points of the face
    for (int j(0); j < 3; j++)
    {
      // loop over points of the face
      for (int k(0); k < 3; k++)
      {
        idxM = it->vertex(j)->info();
        idxN = it->vertex(k)->info();

        // compute mass matrix
        mass(idxM, idxN) += (1. + (int)(j == k)) * faceArea / 12;

          
        // loop over quadrature points
        for (int q(0); q < 3; q++)
        {
          // compute the matrix necessary to build matrix C (almost identical to mass matrix)
          massY(idxM, idxN) += Yrs[q] * S[q][j] * S[q][k] * quadPtWeightDetJ;

          // compute the matrix necessary to build matrix D (almost identical to stiffness matrix)
          stiffnessY(idxM, idxN) += Yrs[q] * (dSdx[j] * dSdx[k] + dSdy[j] * dSdy[k]) * quadPtWeightDetJ;

          // compute matrix necessary to build matrix E
          B(idxM, idxN) += (Xrs[q] * S[q][j] * dSdx[k] + Yrs[q] * S[q][j] * dSdy[k]) * quadPtWeightDetJ;
        }

        // compute stiffness matrix
        stiffness(idxM, idxN) += ((
          it->vertex((j + 1) % 3)->point().y() -      // bm
          it->vertex((j + 2) % 3)->point().y()) *      // bm
          (it->vertex((k + 1) % 3)->point().y() -      // bn
          it->vertex((k + 2) % 3)->point().y()) +      // bn
          (it->vertex((j + 2) % 3)->point().x() -      // cm
          it->vertex((j + 1) % 3)->point().x()) *      // cm
          (it->vertex((k + 2) % 3)->point().x() -      // cn
          it->vertex((k + 1) % 3)->point().x())      // cn
          ) / faceArea / 4;
      }
    }
  }

  // **************************************************************************
  // Compute modes
  // **************************************************************************

  // solve eigen problem
  Eigen::GeneralizedSelfAdjointEigenSolver<Matrix> eigenSolver(stiffness, mass);

  m_eigenFreqs.clear();
  if (m_modesNumber == 0)
  // extract the eigenfrequencies lower than the maximal
  // cut-on frequency and determine the number of modes
  {
    idx = 0;
    maxWaveNumber = pow(2 * M_PI * simuParams.maxCutOnFreq / simuParams.sndSpeed, 2);
    while ((eigenSolver.eigenvalues()[idx] < maxWaveNumber)
      && (idx < eigenSolver.eigenvalues().size()))
    {
      m_eigenFreqs.push_back(sqrt(eigenSolver.eigenvalues()[idx]) * simuParams.sndSpeed / 2. / M_PI);
      idx++;
    }
    m_modesNumber = idx;
  }
  else
  // Extract the eigen frequencies of the number of modes previously specified
  {
    m_eigenFreqs.reserve(m_modesNumber);
    for (int i(0); i < m_modesNumber; i++)
    {
      m_eigenFreqs.push_back(sqrt(eigenSolver.eigenvalues()[i])* simuParams.sndSpeed / 2. / M_PI);
    }
  }

  m_maxAmplitude.clear();
  m_maxAmplitude.reserve(m_modesNumber);
  m_minAmplitude.clear();
  m_minAmplitude.reserve(m_modesNumber);

  // set the first eigen frequency to 0 since it cannot have other value
  m_eigenFreqs[0] = 0.;

  // get the sign of the first mode to set them all positiv later
  if (eigenSolver.eigenvectors().col(0)[0] > 0)
  {
    signFirstMode = 1.;
  }
  else
  {
    signFirstMode = -1.;
  }

  // extract modes matrix
  m_modes = eigenSolver.eigenvectors().block(0, 0, numVert , m_modesNumber );
  for (int m(0); m < m_modesNumber; m++)
  {
    // multiply all modes by the sign of the first mode
    m_modes.col(m) *= signFirstMode;
    // get maximal amplitude of modes
    m_maxAmplitude.push_back(m_modes.col(m).maxCoeff());
    // get minimal amplitude of modes
    m_minAmplitude.push_back(m_modes.col(m).minCoeff());
  }

  // **************************************************************************
  // Compute multimodal matrices
  // **************************************************************************

  // compute matrices C, DN and E
  m_C = Matrix::Zero(m_modesNumber, m_modesNumber);
  m_DN = Matrix::Zero(m_modesNumber, m_modesNumber);
  m_E = Matrix::Zero(m_modesNumber, m_modesNumber);
  for (int m(0); m < m_modesNumber; m++)
  {
    for (int n(0); n < m_modesNumber; n++)
    {
      m_C(m, n) = (m_modes.col(m)).transpose() * massY * m_modes.col(n);
      m_DN(m,n) = (m_modes.col(m)).transpose() * stiffnessY * m_modes.col(n);
      m_E(m,n) = (m_modes.col(m)).transpose() * B * m_modes.col(n);
    }
  }

  // compute matrices DR and KR2
  m_DR.clear(); 
  m_KR2.clear();
  // loop over surfaces
  for (int s(0); s < m_surfIdxList.size(); s++)
  {
    m_DR.push_back(Matrix::Zero(m_modesNumber, m_modesNumber));
    m_KR2.push_back(Matrix::Zero(m_modesNumber, m_modesNumber));
    for (int m(0); m < m_modesNumber; m++)
    {
      for (int n(0); n < m_modesNumber; n++)
      {
        m_DR.back()(m,n) = (m_modes.col(m)).transpose() * RY[s] * m_modes.col(n);
        m_KR2.back()(m,n) = (m_modes.col(m)).transpose() * R[s] * m_modes.col(n);
      }
    }
  }

  // **************************************************************************
  // Print mesh, modes and matrices in text files
  // **************************************************************************

  //ofstream points;
  //points.open("points.txt");
  //for (int p(0); p < numVert; p++)
  //{
  //  for (int c(0); c < 2; c++)
  //  {
  //    points << m_points[me][p][c] << "  ";
  //  }
  //  points << endl;
  //}
  //points.close();

  //ofstream triangles;
  //triangles.open("triangles.txt");
  //for (int t(0); t < m_triangles[me].size(); t++)
  //{
  //  for (int p(0); p < 3; p++)
  //  {
  //    triangles << m_triangles[me][t][p] + 1 << "  ";
  //  }
  //  triangles << endl;
  //}
  //triangles.close();

  //// print the indexes of the segments of the contour of the mesh
  //ofstream seg;
  //seg.open("seg.txt");
  //for (int s(0); s < m_meshContourSeg[me].size(); s++)
  //{
  //  seg << m_meshContourSeg[me][s][0] << "  " << m_meshContourSeg[me][s][1] << endl;
  //}
  //seg.close();

  //ofstream mode;
  //mode.open("mode.txt", ofstream::app);
  //mode << modes << endl << endl;
  //for (int i(0); i < modes.rows(); i++)
  //{
  //  for (int j(0); j < modes.cols(); j++)
  //  {
  //    mode << modes(i, j) << "\t";
  //  }
  //  mode << endl;
  //}
  //mode << endl << endl;
  //mode.close();

  //ofstream matrixC;
  //matrixC.open("matrixC.txt", ofstream::app);
  //matrixC << C << endl << endl;
  //matrixC.close();

/*    ofstream matrixDN;
  matrixDN.open("matrixDN.txt", ofstream::app);
  matrixDN << DN << endl << endl;
  matrixDN.close();  */  
    
  //ofstream matrixDR;
  //matrixDR.open("matrixDR.txt");
  //for (int s(0); s < DR.size(); s++)
  //{
  //  matrixDR << DR[s] << endl << endl;
  //}
  //matrixDR.close();

  //ofstream matrixE;
  //matrixE.open("matrixE.txt", ofstream::app);
  //matrixE << E << endl << endl;
  //matrixE.close();

  //ofstream matrixKR2;
  //matrixKR2.open("matrixKR2.txt");
  //for (int s(0); s < KR2.size(); s++)
  //{
  //  matrixKR2 << KR2[s] << endl << endl;
  //}
  //matrixKR2.close();
}

// **************************************************************************
// Return the zeros of the derivative of the Bessel functions of the 
// first kind Jn, as well as order the corresponding Bessel function 
// and a boolean indicating if the mode is degenerated or not
//
// The zeros are from the work of Curtis and Beattie (1957)
// More precise and more zero could be obtained using boost library

void CrossSection2dRadiation::setBesselParam(struct simulationParameters simuParams)
{
  int mu(0), estimateModeNumber, nZeros(30);
  double fc;
  map<double, pair<int, int>> zeros;

  using namespace boost::math;

  // determine the number of order of Bessel function necessary
  do
  {
    BesselJDerivativeZero(mu, 1, zeros);
    fc = simuParams.sndSpeed * zeros.rbegin()->first / 2. / M_PI / m_radius;
    mu++;
    
  } while (fc < simuParams.maxCutOnFreq/2.);
  // remove last mu because it corresponds to a zero superior to the 
  // maximal cut-off frequency
  mu--;
  zeros.erase(zeros.rbegin()->first);

  // compute the zeros of the Bessel derivatives
  for (int i(0); i <= mu; i++)
  {
    BesselJDerivativeZero(i, nZeros, zeros);
  }

  // estimate the number of modes from the maximal cut-on frequency 
  // as (BesselZero^2) / 7
  estimateModeNumber = zeros.size()*2;

  // reserve necessary space 
  m_BesselZeros.reserve(estimateModeNumber);
  m_BesselOrder.reserve(estimateModeNumber);
  m_degeneration.reserve(estimateModeNumber);

  // set the parameters
  for (auto z : zeros)
  {
    if (z.second.first == 0)
    {
      m_BesselZeros.push_back(z.first);
      m_BesselOrder.push_back(z.second.first);
      m_degeneration.push_back(false);
      m_normModes.push_back(1. / (m_radius * sqrt(M_PI) *
        cyl_bessel_j<int, double>(0, m_BesselZeros.back())));
    }
    else
    {
      m_BesselZeros.push_back(z.first);
      m_BesselOrder.push_back(z.second.first);
      m_degeneration.push_back(false);
      m_normModes.push_back(sqrt(2 / (M_PI * (1 - pow(m_BesselOrder.back() / m_BesselZeros.back(), 2)))) /
        m_radius / cyl_bessel_j<int, double>(m_BesselOrder.back(), m_BesselZeros.back()));

      m_BesselZeros.push_back(z.first);
      m_BesselOrder.push_back(-z.second.first);
      m_degeneration.push_back(true);
      m_normModes.push_back(m_normModes.back());
    }
  }

  m_modesNumber = m_BesselZeros.size();
}

// **************************************************************************
// Compute the mode parameters and propagation matrice of a radiation section

void CrossSection2dRadiation::computeModes(struct simulationParameters simuParams)
{
  using namespace boost::math;

  complex<double> Q1, Q2, avAl(20. * exp(1i * M_PI / 4.)), CPML, DPML;
  int m(0), n(0);

  setBesselParam(simuParams);

  //***********************************************
  // Define functions to integrate
  //***********************************************

  // expression of the parameters alpha and beta 
  // caracterising the PML
  auto alpha = [&, this](const double& r)
  {
    complex<double> al;
    if (r >= (m_radius - m_PMLThickness))
    {
      al = 1. + 2. * (avAl - 1.) * (r - (m_radius -
        m_PMLThickness)) / m_PMLThickness;
    }
    else
    {
      al = (1., 0.);
    }
    return al;
  };
  auto beta = [&, this](const double& r)
  {
    complex<double> bet;
    if (r >= (m_radius - m_PMLThickness))
    {
      bet = 1. + (avAl - 1.) * pow(r - (m_radius -
        m_PMLThickness), 2) / r / m_PMLThickness;
    }
    else
    {
      bet = (1., 0.);
    }
    return bet;
  };

  // expression of the term to integrate in CPML
  auto integral1 = [&, this](const double& r)
  {
    complex<double> al = alpha(r);
    complex<double> bet = beta(r);

    // return the value of the expression to integrate
    return (al * bet - 1.) *
      cyl_bessel_j(m_BesselOrder[m], r * m_BesselZeros[m] / m_radius) *
      cyl_bessel_j(m_BesselOrder[n], r * m_BesselZeros[n] / m_radius) * r;
  };

  // expression of the first term to integrate in DPML
  auto integral21 = [&, this](const double& r)
  {
    complex<double> al = alpha(r);
    complex<double> bet = beta(r);

    // return the value of the expression to integrate
    return (bet / al - 1.) * (0.25 *
      (cyl_bessel_j(m_BesselOrder[m] - 1, r * m_BesselZeros[m] / m_radius) -
      cyl_bessel_j(m_BesselOrder[m] + 1, r * m_BesselZeros[m] / m_radius))*
      (cyl_bessel_j(m_BesselOrder[n] - 1, r * m_BesselZeros[n] / m_radius) -
      cyl_bessel_j(m_BesselOrder[n] + 1, r * m_BesselZeros[n] / m_radius))
       * r);
  };

  // expression of the second term to integrate in DPML
  auto integral22 = [&, this](const double& r)
  {
    complex<double> al = alpha(r);
    complex<double> bet = beta(r);

    // return the value of the expression to integrate
    return (al / bet - 1.) * (
      cyl_bessel_j(m_BesselOrder[m], r * m_BesselZeros[m] / m_radius) *
      cyl_bessel_j(m_BesselOrder[n], r * m_BesselZeros[n] / m_radius)
       / r);
  };

  //***********************************************
  // Build matrices CPML and DPML
  //***********************************************

  vector<Triplet> tripletCPML, tripletDPML;

  // Compute matrices CPML and DPML
  for (; m < m_modesNumber; m++)
  {
    n = 0;
    for (; n < m_modesNumber; n++)
    {
      if (m_BesselOrder[m] == m_BesselOrder[n])
      {
        // compute the matrices CPML
        // Attempting to match a 4-argument overload if 3-argument one is problematic
        double err1;
        auto int1_real = [&](double rr){ return std::real(integral1(rr)); };
        auto int1_imag = [&](double rr){ return std::imag(integral1(rr)); };
        double q1_real = boost::math::quadrature::gauss<double, 15>::integrate(int1_real,
          m_radius - m_PMLThickness, m_radius, &err1);
        double q1_imag = boost::math::quadrature::gauss<double, 15>::integrate(int1_imag,
          m_radius - m_PMLThickness, m_radius, &err1);
        Q1 = complex<double>(q1_real, q1_imag);

        CPML = (double)(m == n) + m_normModes[m] * m_normModes[n] * 
          (1. + (double)(0 == m_BesselOrder[m])) * M_PI * Q1;
        tripletCPML.push_back(Triplet(m, n, CPML));

        // compute the matrices DPML
        double error_estimate_Q1_integral21;
        double error_estimate_Q2_integral22;
        auto int21_real = [&](double rr){ return std::real(integral21(rr)); };
        auto int21_imag = [&](double rr){ return std::imag(integral21(rr)); };
        auto int22_real = [&](double rr){ return std::real(integral22(rr)); };
        auto int22_imag = [&](double rr){ return std::imag(integral22(rr)); };
        double q21_real = boost::math::quadrature::gauss<double, 15>::integrate(int21_real,
          m_radius - m_PMLThickness, m_radius, &error_estimate_Q1_integral21);
        double q21_imag = boost::math::quadrature::gauss<double, 15>::integrate(int21_imag,
          m_radius - m_PMLThickness, m_radius, &error_estimate_Q1_integral21);
        Q1 = complex<double>(q21_real, q21_imag);
        double q22_real = boost::math::quadrature::gauss<double, 15>::integrate(int22_real,
          m_radius - m_PMLThickness, m_radius, &error_estimate_Q2_integral22);
        double q22_imag = boost::math::quadrature::gauss<double, 15>::integrate(int22_imag,
          m_radius - m_PMLThickness, m_radius, &error_estimate_Q2_integral22);
        Q2 = complex<double>(q22_real, q22_imag);

        DPML = (double)(m == n) * pow(m_BesselZeros[m] / m_radius, 2) + 
          m_normModes[m] * m_normModes[n]  *
          (1. + (double)(0 == m_BesselOrder[m])) * M_PI * (
            m_BesselZeros[m] * m_BesselZeros[n] * Q1 / pow(m_radius, 2)
            + pow(m_BesselOrder[m], 2) * Q2);
        tripletDPML.push_back(Triplet(m, n, DPML));
      }
    }
  }

  m_CPML.resize(m_modesNumber, m_modesNumber);
  m_CPML.setFromTriplets(tripletCPML.begin(), tripletCPML.end());
  m_DPML.resize(m_modesNumber, m_modesNumber);
  m_DPML.setFromTriplets(tripletDPML.begin(), tripletDPML.end());

  Eigen::SparseLU< SparseMatC> inv;
  inv.compute(m_CPML);
  Eigen::ComplexEigenSolver<Eigen::MatrixXcd> eig;
  try {
    Eigen::MatrixXcd mat(inv.solve(m_DPML));
    eig.compute(mat);
  }
  catch(const exception &e){  }

  m_eigVal = eig.eigenvalues();
  m_eigVec = eig.eigenvectors();
  m_invEigVec = m_eigVec.inverse();
}

// **************************************************************************
// Select some modes and remove the other ones

void CrossSection2dFEM::selectModes(vector<int> modesIdx)
{
  int nPt = m_modes.rows();
  m_modesNumber = modesIdx.size();
  Matrix tmpModes(nPt, m_modesNumber);
  vector<double> tmpEigenFreqs;
  tmpEigenFreqs.reserve(m_modesNumber);
  Matrix tmpC(m_modesNumber, m_modesNumber),
    tmpDN(Matrix::Zero(m_modesNumber, m_modesNumber)),
    tmpE(Matrix::Zero(m_modesNumber, m_modesNumber));
  vector<Matrix> tmpKR2;
  for (int i(0); i < m_KR2.size(); i++)
  {
    tmpKR2.push_back(Matrix::Zero(m_modesNumber, m_modesNumber));
  }
  int m(0), n(0);

  for (auto i : modesIdx)
  {
    for (int j(0); j < nPt; j++)
    {
      tmpModes(j, n) = m_modes(j, i);
    }
    n++;

    tmpEigenFreqs.push_back(m_eigenFreqs[i]);
  }
  m_eigenFreqs = tmpEigenFreqs;
  m_modes = tmpModes;

  for (auto i : modesIdx)
  {
    n = 0;
    for (auto j : modesIdx)
    {
      tmpC(m, n) = m_C(i, j);
      tmpDN(m, n) = m_DN(i, j);
      tmpE(m, n) = m_E(i, j);
      for (int k(0); k < m_KR2.size(); k++)
      {
        tmpKR2[k](m, n) = m_KR2[k](i, j);
      }
      n++;
    }
    m++;
  }
  m_C = tmpC;
  m_DN = tmpDN;
  m_E = tmpE;
  m_KR2 = tmpKR2;
}

// **************************************************************************
// Interpolate the propagation modes
Matrix CrossSection2dFEM::interpolateModes(vector<Point> pts)
{
  int numPts = pts.size();
  Matrix interpolation(numPts, m_modesNumber);
  vector<Point> points;
  vector<Point_value_map> values;
  values.reserve(m_modesNumber);
  Point_value_map tempValues;
  Delaunay_triangulation T;
  Coord_type l_value;
  vector< pair< Point, Coord_type > > coords;
  Coord_type norm;
  bool ptNotFound(false);

  ofstream log;
  log.open("log.txt", ofstream::app);

  // insert triangulation points
  int numTriPts(m_points.size());
  for (int i(0); i < numTriPts; ++i)
  {
    points.push_back(Point(m_points[i][0], m_points[i][1]));
    T.insert(points.back());
  }

  // create the point values maps
  for (int m(0); m < m_modesNumber; m++)
  {
    tempValues.clear();
    for (int i(0); i < numTriPts; ++i)
    {
      tempValues.insert(make_pair(points[i], m_modes(i, m)));
    }
    values.push_back(tempValues);
  }

  // insert points to interpolate
  for (int i(0); i < numPts; i++)
  {
    // it happens sometimes that the points of the intersection polygon lie slightly 
    // outside of the contour, in this case bring it back on the boundary
    if (m_contour.has_on_unbounded_side(pts[i]))
    {
      pts[i] = bringBackPointInsideContour(m_contour, pts[i], m_spacing);
    }
  }

  // interpolate the field
  for (int i(0); i < numPts; ++i)
  {
    // check if the point is inside the contour
    if (m_contour.has_on_unbounded_side(pts[i]))
    {
      log << "Error interpolating: point outside of the contour" << endl;
      interpolation.row(i).setConstant(NAN); 
      ptNotFound = true;
    }
    else
    {
      //Coordinate_vector:
      coords.clear();
      auto res = CGAL::natural_neighbor_coordinates_2(T, pts[i], std::back_inserter(coords));
      norm = res.second;

      for (int m(0); m < m_modesNumber; ++m)
      {
        //linear interpolant:
        l_value = CGAL::linear_interpolation(coords.begin(), coords.end(),
          norm, Value_access(values[m]));
        interpolation(i,m) = (double)l_value;
      }
    }
  }

  //// if a point have not been found export the points and contour
  //if (ptNotFound)
  //{
  //  ofstream os("points.txt");
  //  for (int i(0); i < pts.size(); i++)
  //  {
  //    os << pts[i].x() << "  " << pts[i].y() << endl;
  //  }
  //  os.close();
  //  os.open("cont.txt");
  //  auto it = m_contour.begin();
  //  for (; it != m_contour.end(); it++)
  //  {
  //    os << it->x() << "  " << it->y() << endl;
  //  }
  //  it = m_contour.begin();
  //  os << it->x() << "  " << it->y() << endl;
  //  os.close();
  //}

  log.close();
  
  return interpolation;
}

// **************************************************************************
// Interpolate the propagation modes with a scaling of the mesh
Matrix CrossSection2d::interpolateModes(vector<Point> pts, double scaling)
{
  Transformation scale(CGAL::SCALING, scaling);
  for (int i(0); i < pts.size(); i++) {
    pts[i] = scale(pts[i]);
  }
  return(interpolateModes(pts));
}

// **************************************************************************
// Interpolate the propagation modes with a scaling and a translation of the mesh

Matrix CrossSection2d::interpolateModes(vector<Point> pts, double scaling, Vector translation)
{
  Transformation scale(CGAL::SCALING, scaling);
  Transformation translate(CGAL::TRANSLATION, translation);
  for (int i(0); i < pts.size(); i++) {
    pts[i] = scale(translate(pts[i]));
  }
  return(interpolateModes(pts));
}

// **************************************************************************
// Compute mode amplitude for the radiation cross-section

Matrix CrossSection2dRadiation::interpolateModes(vector<Point> pts)
{
  double r, t;
  int numPts = pts.size();
  Matrix interpolation(numPts, m_modesNumber);

  using namespace boost::math;

  // Loop over the points
  for (int p(0); p < numPts; p++)
  {
    // compute the radial polar coordinate of the point
    //
    // relative position assumed
    r = sqrt(pow(pts[p].x(), 2) + pow(pts[p].y(), 2));

    // check if the point is inside the cross-section
    if (r > m_radius)
    {
      // If it is outside set amplitude to nan
      interpolation.row(p).setConstant(NAN);
    }
    else
    {
      // compute the angular polar coordinate of the point
      t = atan2(pts[p].y(), pts[p].x());

      // loop over the modes
      for (int m(0); m < m_modesNumber; m++)
      {
        if (m_degeneration[m])
        {
          interpolation(p, m) = m_normModes[m] *
            cyl_bessel_j(m_BesselOrder[m], r * m_BesselZeros[m] / m_radius) *
            sin(m_BesselOrder[m] * t);
        }
        else
        {
          interpolation(p, m) = m_normModes[m] *
            cyl_bessel_j(m_BesselOrder[m], r * m_BesselZeros[m] / m_radius) *
            cos(m_BesselOrder[m] * t);
        }
      }
    }
    
  }
  return interpolation;
}

// **************************************************************************
// Functions to set and get impedance, and admittance
void CrossSection2d::setZin(Eigen::MatrixXcd imped) { 
  if (m_impedance.size() == 0)
  {
    m_impedance.push_back(imped);
  }
  else
  {
    if (Zdir() == 1) { m_impedance.back() = imped; }
    else { m_impedance[0] = imped; }
  }
}

void CrossSection2d::setZout(Eigen::MatrixXcd imped) {
  if (m_impedance.size() == 0)
  {
    m_impedance.push_back(imped);
  }
  else
  {
    if (Zdir() == 1) { m_impedance[0] = imped; }
    else { m_impedance.back() = imped; }
  }
}

void CrossSection2d::setYin(Eigen::MatrixXcd admit) {
  if (m_admittance.size() == 0)
  {
    m_admittance.push_back(admit);
  }
  else
  {
    if (Ydir() == 1) { m_admittance.back() = admit; }
    else { m_admittance[0] = admit; }
  }
}

void CrossSection2d::setYout(Eigen::MatrixXcd admit) {
  if (m_admittance.size() == 0)
  {
    m_admittance.push_back(admit);
  }
  else
  {
    if (Ydir() == 1) { m_admittance[0] = admit; }
    else { m_admittance.back() = admit; }
  }
}

// **************************************************************************
// Compute characteristic impedance

void CrossSection2dFEM::characteristicImpedance(
  Eigen::MatrixXcd& characImped,
  double freq, struct simulationParameters simuParams)
{
  characImped = Eigen::MatrixXcd::Zero(m_modesNumber, m_modesNumber);
  double k(2 * M_PI * freq / simuParams.sndSpeed);
  complex<double> diag;

  switch (simuParams.propMethod) {
  // Magnus scheme
  case MAGNUS:
    // loop over the modes
    for (int i(0); i < m_modesNumber; i++)
    {
      diag = 1./sqrt(complex<double>(pow(2 * M_PI * m_eigenFreqs[i] / simuParams.sndSpeed, 2) - pow(k, 2)));
      characImped(i, i) = diag;
    }
    break;
  case STRAIGHT_TUBES:
    // loop over the modes
    for (int i(0); i < m_modesNumber; i++)
    {
      diag = (simuParams.volumicMass * 2 * M_PI * freq) /
        sqrt(complex<double>(pow(k, 2) - pow(2 * M_PI * m_eigenFreqs[i] / simuParams.sndSpeed, 2)))
        / m_area;
      characImped(i, i) = diag;
    }
    break;
  }
}

void CrossSection2dRadiation::characteristicImpedance(
  Eigen::MatrixXcd& characImped, double freq, struct simulationParameters simuParams)
{
  double k2(pow(2 * M_PI * freq / simuParams.sndSpeed,2));
  SparseMatC Id(m_modesNumber, m_modesNumber);
  Id.setIdentity();
  complex<double> j(0., 1.);

  characImped = m_eigVec * ((j*(
    k2 * Eigen::VectorXd::Ones(m_modesNumber) - m_eigVal
    ).cwiseSqrt()).cwiseInverse()).asDiagonal()
    * m_invEigVec;
}

// **************************************************************************
// Compute characteristic admittance

void CrossSection2dFEM::characteristicAdmittance(
  Eigen::MatrixXcd& admit, double freq, struct simulationParameters simuParams)
{
  admit = Eigen::MatrixXcd::Zero(m_modesNumber, m_modesNumber);
  double k(2 * M_PI * freq / simuParams.sndSpeed);
  complex<double> diag;

  switch (simuParams.propMethod)
  {
    // for Riccati method
  case MAGNUS:
    // loop over the modes
    for (int i(0); i < m_modesNumber; i++)
    {
      diag = sqrt(complex<double>(pow(2 * M_PI * m_eigenFreqs[i] / simuParams.sndSpeed, 2)  - pow(k, 2)));
      admit(i, i) = diag;
    }
    break;
    // for straight waveguide method
  case STRAIGHT_TUBES:
    // loop over the modes
    for (int i(0); i < m_modesNumber; i++)
    {
      diag = sqrt(complex<double>(pow(k, 2) - pow(2 * M_PI * m_eigenFreqs[i] / simuParams.sndSpeed, 2)))
        * m_area / (simuParams.volumicMass * 2 * M_PI * freq);

      admit(i, i) = diag;
    }
    break;
  }
}

void CrossSection2dRadiation::characteristicAdmittance(
  Eigen::MatrixXcd& admit, double freq, struct simulationParameters simuParams)
{
  double k2(pow(2 * M_PI * freq / simuParams.sndSpeed,2));

  admit = m_invEigVec * ((k2 * Eigen::VectorXd::Ones(m_modesNumber) +
    m_eigVal).cwiseSqrt()).asDiagonal() * m_eigVec;
}

// **************************************************************************
// Compute wall admittance

complex<double> CrossSection2dFEM::getWallAdmittance(
  struct simulationParameters simuParams, double freq)
{
  if (simuParams.wallLosses)
  {
    return(-simuParams.percentageLosses*(-simuParams.volumicMass * simuParams.sndSpeed *
      (1. / (complex<double>(Tube::STANDARD_WALL_RESISTANCE_CGS,
        2. * M_PI * freq * Tube::STANDARD_WALL_MASS_CGS -
        Tube::STANDARD_WALL_STIFFNESS_CGS / 2. / M_PI / freq)
        / m_perimeter / m_length))));
  }
  else
  {
    return(complex<double>(0., 0.));
  }
}

// **************************************************************************
// Compute boundary specific admittance for visco thermal losses

void CrossSection2dFEM::getSpecificBndAdm(struct simulationParameters simuParams, double freq, 
  Eigen::VectorXcd& bndSpecAdm)
{
  if (simuParams.viscoThermalLosses)
  {
    bndSpecAdm.resize(m_modesNumber);
    bndSpecAdm.setZero();
    double k(2. * M_PI * freq / simuParams.sndSpeed);
    for (int m(0); m < m_modesNumber; m++)
    {
      bndSpecAdm(m) = simuParams.percentageLosses * (
        ((1. - pow(2 * M_PI * m_eigenFreqs[m] / simuParams.sndSpeed, 2) / pow(k, 2)) *
        simuParams.viscousBndSpecAdm + simuParams.thermalBndSpecAdm) * sqrt(freq));
    }
  }
  else if (simuParams.constantWallImped)
  {
    bndSpecAdm.resize(m_modesNumber);
    bndSpecAdm.setConstant(simuParams.percentageLosses * simuParams.wallAdmit);
  }
  else
  {
    bndSpecAdm.resize(m_modesNumber);
    bndSpecAdm.setZero();
  }
}

// **************************************************************************
// Return the curvature

double CrossSection2dFEM::curvature(bool curved) 
{ 
  if (curved) { return  1. / m_curvatureRadius; }
  else { return 0.; }
}

// **************************************************************************
// Set the area variation profile type
void CrossSection2dFEM::setAreaVariationProfileType(enum areaVariationProfile profile)
{
  m_areaProfile = profile;
}

// **************************************************************************
// scaling function

double CrossSection2dFEM::scaling(double tau)
{
  switch (m_areaProfile)
  {
  case LINEAR:
    return (m_scalingFactors[1] - m_scalingFactors[0]) * tau
      + m_scalingFactors[0];
    break;
  case GAUSSIAN:
    return (1. + 0.75*exp(-pow(0.3*(tau - 0.5),2)/2./pow(0.04,2)));
    break;
  case ELEPHANT:
    return (0.25*(1 + 9.*pow(tau, 2) - 6.*pow(tau, 3))); // l = [0.25 1]
    break;
  }
}

// **************************************************************************
// derivative of the scaling function
 
double CrossSection2dFEM::scalingDerivative(double tau)
{
  // compute the length of the section
  double al;
  if (abs(m_circleArcAngle) < MINIMAL_DISTANCE)
  {
      al = m_length;
  }
  else
  {
      al = abs(m_circleArcAngle) * abs(m_curvatureRadius);
  }

  switch (m_areaProfile)
  {
  case LINEAR:
    return((m_scalingFactors[1] - m_scalingFactors[0])/ al); 
    break;
  case GAUSSIAN:
    return(-0.75*0.09*(tau - 0.5)*
       exp(-pow(0.3*(tau - 0.5),2)/2./pow(0.04,2))
       /pow(0.04, 2)/30.);
    break;
  case ELEPHANT:
    return (9.*tau*(1. - tau)/16.95/2.); // l = [0.25 1]
  }
}

// **************************************************************************
// Propagate impedance, admittance, pressure or velocity using the 
// order 2 or 4 Magnu-Moebius scheme
void CrossSection2dFEM::propagateMagnus(Eigen::MatrixXcd Q0, struct simulationParameters simuParams,
  double freq, double direction, enum physicalQuantity quant, std::chrono::duration<double> *time)
{
  // track time
  auto startTot = std::chrono::system_clock::now();

  int numX(simuParams.numIntegrationStep);
  int mn(m_modesNumber);
  double al(length()); // arc length
  double dX; // (direction * al / (double)(numX - 1));
  double curv(curvature(simuParams.curved));
  double k(2 * M_PI * freq / simuParams.sndSpeed);
  double tau, l0, l1, dl0, dl1;
  // parameters of the coefficient l evolution
  Eigen::MatrixXcd A0(2 * mn, 2 * mn), A1(2 * mn, 2 * mn), omega(2 * mn, 2 * mn);
  Eigen::MatrixXcd K2(Eigen::MatrixXcd::Zero(mn, mn));
  complex<double> wallAdmittance;
  Eigen::VectorXcd bndSpecAdm(Eigen::VectorXcd::Zero(mn));

  if (m_length == 0.)
  {
    switch (quant) {
    case IMPEDANCE:
      m_impedance.clear();
      m_impedance.push_back(Q0);
      break;
    case ADMITTANCE:
      m_admittance.clear();
      m_admittance.push_back(Q0);
      break;
    case PRESSURE:
      m_acPressure.clear();
      m_acPressure.push_back(Q0);
      break;
    case VELOCITY:
      m_axialVelocity.clear();
      m_axialVelocity.push_back(Q0);
      break;
    }
  }
  else
  {
    switch (quant) {
    case IMPEDANCE:
      m_impedance.clear();
      m_impedance.reserve(numX);
      m_impedance.push_back(Q0);
      dX = -al / (double)(numX - 1);
      break;
    case ADMITTANCE:
      m_admittance.clear();
      m_admittance.reserve(numX);
      m_admittance.push_back(Q0);
      dX = -al / (double)(numX - 1);
      break;
    case PRESSURE:
      m_acPressure.clear();
      m_acPressure.reserve(numX);
      m_acPressure.push_back(Q0);
      dX = al / (double)(numX - 1);
      break;
    case VELOCITY:
      m_axialVelocity.clear();
      m_acPressure.reserve(numX);
      m_axialVelocity.push_back(Q0);
      dX = al / (double)(numX - 1);
      break;
    }

    // compute wall admittance
    wallAdmittance = getWallAdmittance(simuParams, freq);

    // compute boundary specific admittance
    getSpecificBndAdm(simuParams, freq, bndSpecAdm);

    // compute matrix KR2
    Eigen::MatrixXcd KR2(Eigen::MatrixXcd::Zero(mn, mn));
    for (int s(0); s < m_KR2.size(); s++)
    {
      KR2 += m_KR2[s] * bndSpecAdm.asDiagonal() + wallAdmittance * m_KR2[s];
    }

    // track time
    auto start = std::chrono::system_clock::now();
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_preComp, matricesMag, propag, tot;
    elapsed_preComp = end - startTot;

      // discretize X axis
      for (int i(0); i < numX - 1; i++)
      {

        switch (simuParams.orderMagnusScheme)
        {
      //****************************
      // Magnus scheme order 2
      //****************************

      case 2:

        if (direction < 0.)
        {
          tau = ((double)(numX - i) - 1.5) / (double)(numX - 1);
        }
        else
        {
          tau = ((double)(i)+0.5) / (double)(numX - 1);
        }
        l0 = scaling(tau);
        dl0 = - Ydir() * scalingDerivative(tau);

        // build matrix K2
        K2.setZero(mn, mn);
        for (int j(0); j < mn; j++)
        {
          K2(j, j) = pow(2 * M_PI * m_eigenFreqs[j] / simuParams.sndSpeed, 2) - pow(k * l0, 2);
        }
        K2 += 1i * k * l0 * KR2;

        // build matrix A0
        omega << ((dl0 / l0)* m_E),
          (Eigen::MatrixXcd::Identity(mn, mn) - curv * l0 * m_C) / pow(l0, 2),
          (K2 + curv * l0 * (m_C * pow(k * l0, 2) - m_DN
            )),
          (-(dl0 / l0) * m_E.transpose());

        start = std::chrono::system_clock::now();

        omega = (dX * omega).exp();

        end = std::chrono::system_clock::now();
        *time += end - start;
        start = std::chrono::system_clock::now();

        break;

      //****************************
      // Magnus scheme order 4
      //****************************

      case 4:

          //*******************************
          // first point of Magnus scheme
          //*******************************

          if (dX < 0.) {
            tau = ((double)(numX - i) - 1.5 + sqrt(3) / 6.) / (double)(numX - 1);
          }
          else
          {
            tau = (double)(i + 0.5 - sqrt(3) / 6.) / (double)(numX - 1);
          }
          l0 = scaling(tau);
          dl0 = scalingDerivative(tau);

          // build matrix K2
          K2.setZero(mn, mn);
          for (int j(0); j < mn; j++)
          {
            K2(j, j) = pow(2 * M_PI * m_eigenFreqs[j] / simuParams.sndSpeed, 2) - pow(k * l0, 2);
          }
          K2 += 1i * k * l0 * KR2;

          // build matrix A0
          A0 << ((dl0 / l0) * m_E),
            (Eigen::MatrixXcd::Identity(mn, mn) - curv * l0 * m_C) / pow(l0, 2),
            (K2 + curv * l0 * (m_C * pow(k * l0, 2) - m_DN)),
            (-(dl0 / l0) * m_E.transpose());

            //*******************************
            // second point of Magnus scheme
            //*******************************

          if (dX < 0.)
          {
            tau = ((double)(numX - i) - 1.5 - sqrt(3) / 6.) / (double)(numX - 1);
          }
          else
          {
            tau = (double)(i + 0.5 + sqrt(3) / 6.) / (double)(numX - 1);
          }
          l1 = scaling(tau);
          dl1 = scalingDerivative(tau);

          // build matrix K
          K2.setZero(mn, mn);
          for (int j(0); j < mn; j++)
          {
            K2(j, j) = pow(2 * M_PI * m_eigenFreqs[j] / simuParams.sndSpeed, 2) - pow(k * l1, 2);
          }
          K2 += 1i * k * l1 * KR2;
          // build matrix A1
          A1 << ((dl1 / l1) * m_E),
            (Eigen::MatrixXcd::Identity(mn, mn) - curv * l1 * m_C) / pow(l1, 2),
            (K2 + curv * l1 * (m_C * pow(k * l1, 2) - m_DN)),
            (-(dl1 / l1) * m_E.transpose());

            //*******************************
            // compute matrix omega
            //*******************************

            // tract time  
          start = std::chrono::system_clock::now();

          omega = (0.5 * dX * (A0 + A1) + sqrt(3) * pow(dX, 2) * (A1 * A0 - A0 * A1) / 12.).exp();

          // tract time
          end = std::chrono::system_clock::now();
          matricesMag += end - start;
          start = std::chrono::system_clock::now();

          break;
        }

      // compute the propagated quantity at the next point
      switch (quant)
      {
      case IMPEDANCE:
        m_impedance.push_back((omega.block(0, 0, mn, mn) * m_impedance.back() +
          omega.block(0, mn, mn, mn)) *
          ((omega.block(mn, 0, mn, mn) * m_impedance.back() +
            omega.block(mn, mn, mn, mn)).inverse()));
        break;
      case ADMITTANCE:
        m_admittance.push_back((omega.block(mn, 0, mn, mn) +
          omega.block(mn, mn, mn, mn) * m_admittance.back()) *
          ((omega.block(0, 0, mn, mn) +
            omega.block(0, mn, mn, mn) * m_admittance.back()).inverse()));
        break;
      case PRESSURE:
        m_acPressure.push_back((omega.block(0, 0, mn, mn) + omega.block(0, mn, mn, mn) *
            m_admittance[numX - 1 - i])* m_acPressure.back());
        break;
      case VELOCITY:
          m_axialVelocity.push_back((omega.block(mn, 0, mn, mn)* m_impedance[numX - 1 - i] +
            omega.block(mn, mn, mn, mn))* m_axialVelocity.back());
      }

      // track time
      end = std::chrono::system_clock::now();
      propag += end - start;
    }
  // track time
  tot = end - startTot;
  }
}

// **************************************************************************
// Propagate the admittance along the cross-section in a straight tube

void CrossSection2dFEM::propagateImpedAdmitStraight(Eigen::MatrixXcd Z0,
  Eigen::MatrixXcd Y0, double freq, struct simulationParameters simuParams,
  double prevArea, double nextArea)
{
  double volMass(simuParams.volumicMass);
  int mn(m_modesNumber);
  Eigen::MatrixXcd 
    iD2(Eigen::MatrixXcd::Zero(mn,mn)), 
    iD3(Eigen::MatrixXcd::Zero(mn, mn)), 
    D1(Eigen::MatrixXcd::Zero(mn, mn)),
    D2(Eigen::MatrixXcd::Zero(mn, mn)),
    Zc(mn, mn);
  Matrix I(Matrix::Identity(mn, mn));
  complex<double> kn, j(0., 1.), diag;
  double k(2 * M_PI * freq / simuParams.sndSpeed);
  Eigen::MatrixXcd Yc;
  characteristicAdmittance(Yc, freq, simuParams);

  ofstream log;

  if (freq == 1000.)
  {
    log.open("lgth.txt", ofstream::app);
    log << m_length << endl;
    log.close();
  }

  if (m_length == 0.)
  {
    m_admittance.push_back(Y0);
    m_impedance.push_back(Z0);
  }
  else
  {
    // set the initial admittance
    m_admittance.push_back(Y0);
    m_impedance.push_back(Z0);

    // compute propagation matrices
    // loop over modes
    for (int i(0); i < m_modesNumber; i++)
    {
      kn = sqrt(pow(k, 2) - complex<double>(pow(2 * M_PI * m_eigenFreqs[i] / simuParams.sndSpeed, 2)));

      diag = 1. / (j * sin(kn * m_length));
      iD2(i, i) = diag;
      diag = 1. / (j * tan(kn * m_length));
      iD3(i, i) = diag;

      diag = cos(kn * m_length);
      D1(i, i) = diag;
      diag = j * sin(kn * m_length);
      D2(i, i) = diag;
    }
    Zc = Yc.inverse();

    // if the junction with the previous section is a contraction
    if (m_area > prevArea)
    {
      // if the junction with the next section is a contraction
      if (nextArea > m_area)
      {
        m_admittance.push_back(iD3 * Yc - iD2 * Yc *
        (m_admittance.back() + iD3 * Yc).inverse() * iD2 * Yc);
        m_impedance.push_back(m_admittance.back().fullPivLu().inverse());
      }
      // if the junction with the next section is an expansion
      else
      {
        m_impedance.push_back(iD3 * Zc - iD2 * Zc * m_admittance.back() *
          (I + iD3*Zc * m_admittance.back()).inverse() * iD2*Zc);
        m_admittance.push_back(m_impedance.back().fullPivLu().inverse());
      }
    }
    // if the junction with the previous section is an expansion
    else
    {
      // if the junction with the next section is a contraction
      if (nextArea > m_area)
      {
        m_admittance.push_back(iD3*Yc - iD2*Yc * m_impedance.back() *
          (I + iD3 * Yc * m_impedance.back()).inverse() * iD2 * Yc);
        m_impedance.push_back(m_admittance.back().fullPivLu().inverse());
      }
      // if the junction with the next section is an expansion
      else
      {
        m_impedance.push_back(iD3 * Zc - iD2 * Zc *
          (m_impedance.back() + iD3*Zc).inverse() * iD2 * Zc);
        m_admittance.push_back(m_impedance.back().fullPivLu().inverse());
      }
    }
  }
}

void CrossSection2dRadiation::propagateImpedAdmitStraight(
  Eigen::MatrixXcd Z0,
  Eigen::MatrixXcd Y0, double freq, struct simulationParameters simuParams,
  double prevArea, double nextArea)
{
  m_impedance.push_back(Z0);
  m_admittance.push_back(Y0);
}

// **************************************************************************
// Propagate the axial velocity along the cross-section for a straight tube

void CrossSection2dFEM::propagatePressureVelocityStraight(Eigen::MatrixXcd V0,
  Eigen::MatrixXcd P0, double freq, struct simulationParameters simuParams,
  double nextArea)
{
  double volMass(simuParams.volumicMass);
  int mn(m_modesNumber);
  Eigen::MatrixXcd D1(Eigen::MatrixXcd::Zero(mn, mn)),
    D2(Eigen::MatrixXcd::Zero(mn, mn)), Zend;
  complex<double> kn, j(0., 1.), diag;
  double k(2 * M_PI * freq / simuParams.sndSpeed);
  Eigen::MatrixXcd Yc;
  characteristicAdmittance(Yc, freq, simuParams);

  if (m_length == 0.)
  {
    m_axialVelocity.push_back(V0);
    m_acPressure.push_back(P0);
  }
  else
  {
    // initalise velocity
    m_axialVelocity.push_back(V0);
    m_acPressure.push_back(P0);

    // compute propagation matrices
    // loop over modes
    for (int i(0); i < mn; i++)
    {
      kn = sqrt(pow(k, 2) - complex<double>(pow(2 * M_PI * m_eigenFreqs[i] / simuParams.sndSpeed, 2)));

      diag = cos(kn * m_length);
      D1(i, i) = diag;
      diag = j * sin(kn * m_length);
      D2(i, i) = diag;
    }

    // if the section expends
    if (nextArea > m_area)
    {
      m_axialVelocity.push_back(
      (D2 * Yc * m_impedance[0] + D1).householderQr().solve(m_axialVelocity.back()));
      m_acPressure.push_back(m_impedance[0] * m_axialVelocity.back());
    }
    // if the section contracts
    else
    {
      m_acPressure.push_back(
      (D1 + D2* Yc.inverse()*m_admittance[0]).householderQr().
        solve(m_acPressure.back()));
      m_axialVelocity.push_back(m_admittance[0] * m_acPressure.back());
    }
  }
}

// **************************************************************************

void CrossSection2dRadiation::propagatePressureVelocityStraight(
  Eigen::MatrixXcd V0,
  Eigen::MatrixXcd P0, double freq, struct simulationParameters simuParams,
  double nextArea)
{
  m_axialVelocity.push_back(V0);
  m_acPressure.push_back(P0);
}

// **************************************************************************
// compute the amplitude of the pressure modes at a given distance from the exit

void CrossSection2dRadiation::radiatePressure(double distance, double freq,
  struct simulationParameters simuParams, Eigen::MatrixXcd &pressAmp)
{
  complex<double> j(0., 1.);
  double k2(pow(2 * M_PI * freq / simuParams.sndSpeed, 2));

  Eigen::VectorXcd propa(m_modesNumber);
  // compute exponential
  for (int m(0); m < m_modesNumber; m++)
  {
    propa(m) = exp(distance * j * (k2 - m_eigVal(m)));
  }

  pressAmp = m_eigVec * propa.asDiagonal() * m_invEigVec 
    * m_acPressure[0];
}

// **************************************************************************
// Acoustic field computation
// **************************************************************************

// **************************************************************************
// Get transformed coordinate from an input cartesian point
//
// if the option useBbox is set to true, it is checked if the point lies inside 
// the bounding box of the contour instead of the contour itself

bool CrossSection2dFEM::getCoordinateFromCartesianPt(Point_3 pt, Point_3 &ptOut, bool useBbox)
{
  bool isInside(false);
  double x, y, z, sc;
  if (length() > 0.)
  {
    // center-line point
    Point ctl(m_ctrLinePt.x, m_ctrLinePt.y);

    // if there is no curvature 
    if (abs(m_circleArcAngle) < MINIMAL_DISTANCE)
    {
      //log << "No curvature" << endl;
      x = pt.x() - ctl.x();
      sc = scaling(x / length());
      y = pt.y() / sc;
      z = pt.z() / sc;
    }
    else
    {
      //log << "Curvature" << endl;
      // curvature radius
      double R(abs(m_curvatureRadius));
      // center of the circle arc
      Point C(ctl.x() + m_curvatureRadius * m_normal.x, ctl.y()
        + m_curvatureRadius * m_normal.y);
      complex<double> ptCplx(pt.x() - C.x(), pt.z() - C.y());
      complex<double> ctlCplx(ctl.x() - C.x(), ctl.y() - C.y());

      // get the x coordinate by computing the angle between the 2 complex coordinate pts
      if (((m_curvatureRadius < 0.) && (m_curvatureRadius * m_circleArcAngle > 0.))
        || ((m_curvatureRadius > 0.) && (m_curvatureRadius * m_circleArcAngle < 0.)))
      {
        x = R * fmod(arg(ctlCplx) - arg(ptCplx) + 2. * M_PI, 2. * M_PI);
      }
      else
      {
        x = R * fmod(arg(ptCplx) - arg(ctlCplx) + 2. * M_PI, 2. * M_PI);
      }

      sc = scaling(x / length());
      // compute y coordinate
      y = pt.y() / sc;
      // compute z coordinate
      if (m_curvatureRadius < 0.)
      {
        z = (abs(ptCplx) - R) / sc;
      }
      else
      {
        z = -(abs(ptCplx) - R) / sc;
      }
    }

    // check if the point is inside the section
    isInside = true;
    if ((x > length()) || (x < 0.))
    {
      x = NAN; y = NAN; z = NAN;
      isInside = false;
    }
    else if (useBbox)
    {
      auto bbox = m_contour.bbox();
      if ((y < bbox.xmin()) || (y > bbox.xmax())
        || (z < bbox.ymin()) || (z > bbox.ymax()))
      {
        x = NAN; y = NAN; z = NAN;
        isInside = false;
      }
    }
    else if (m_contour.has_on_unbounded_side(Point(y, z)))
    {
      x = NAN; y = NAN; z = NAN;
      isInside = false;
    }
  }
  else
  {
    x = NAN; y = NAN; z = NAN;
  }
  ptOut = Point_3(x, y, z);
  return(isInside);
}

// **************************************************************************
complex<double> CrossSection2dFEM::pin(Point pt)
{
  vector<Point> pts;
  pts.push_back(pt);
  return((interpolateModes(pts) * Pin())(0,0));
} 
// **************************************************************************
complex<double> CrossSection2dFEM::pout(Point pt) 
{
  vector<Point> pts;
  pts.push_back(pt);
  if (m_acPressure.size() == 0)
  {
    return((interpolateModes(pts) * Zout() * Qout())(0, 0));
  }
  else
  {
  return((interpolateModes(pts) * Pout())(0,0));
  }
}
// **************************************************************************
complex<double> CrossSection2dFEM::qin(Point pt)
{
  vector<Point> pts;
  pts.push_back(pt);
  return((interpolateModes(pts) * Qin())(0,0));
}
// **************************************************************************
complex<double> CrossSection2dFEM::qout(Point pt)
{
  vector<Point> pts;
  pts.push_back(pt);
  return((interpolateModes(pts) * Qout())(0,0));
}
// **************************************************************************
complex<double> CrossSection2dFEM::p(Point_3 pt, struct simulationParameters simuParams)
{
  return(interiorField(pt, simuParams, PRESSURE));
}

// **************************************************************************
complex<double> CrossSection2dFEM::q(Point_3 pt, struct simulationParameters simuParams)
{
  return(interiorField(pt, simuParams, VELOCITY));
}

// **************************************************************************
// Compute the pressure or the velocity inside

complex<double> CrossSection2dFEM::interiorField(Point_3 pt, struct simulationParameters simuParams,
          enum physicalQuantity quant)
{
  // get arc length
  double al(length());
  int numX(simuParams.numIntegrationStep);
  double dx = al/(double)(numX - 1); // distance btw pts

  // locate indexes of previous and following points
  int nPt(m_impedance.size()-1);
  double x_dx(pt.x() / dx);
  int idx[2] = {min((int)floor(x_dx), numX - 2),
    min((int)ceil(x_dx), numX - 1)};

  // define the interpolation point
  vector<Point> pts;
  pts.push_back(Point(pt.y(), pt.z()));

  auto correctIdxIfBackwardProp = [&idx, nPt](double dir)
  {if (dir == -1) { for (int i(0); i < 2; i++) { idx[i] = nPt - idx[i]; } }};

  // interpolate quantity
  double x_0((double)(idx[0])*dx );
  Eigen::MatrixXcd Q;
  Matrix modes;
  switch (quant)
     {
      case PRESSURE:
        // if the propagation direction is backward, reverse the indexes
        correctIdxIfBackwardProp(Pdir());
        // if the pressure amplitudes have not been computed
        if (m_acPressure.size() == 0)
        {
          int numStep(m_impedance.size());
          for (int i(0); i < numStep; i++)
          {
            m_acPressure.push_back(m_impedance[numStep - 1 -i] * 
                m_axialVelocity[i]);
          }
        }

        // interpolate the amplitudes
        Q = (pt.x() - x_0)*(m_acPressure[idx[1]] - m_acPressure[idx[0]])/dx 
        + m_acPressure[idx[0]];

      return((interpolateModes(pts) * Q)(0,0));
        break;
      case VELOCITY:
        // if the propagation direction is backward, reverse the indexes
        correctIdxIfBackwardProp(Qdir());
        if (Qdir() == -1) { for (auto it : idx) { it = nPt - it; } }
        // if the velocity have not been computed
        if (m_axialVelocity.size() == 0)
        {
          int numStep(m_admittance.size());
          for (int i(0); i < numStep; i++)
          {
            m_axialVelocity.push_back(m_admittance[numStep - 1 - i] *
                m_acPressure[i]);
          }
        }
        // interpolate the amplitudes
        Q = (pt.x() - x_0)*(m_axialVelocity[idx[1]] - m_axialVelocity[idx[0]])/dx 
        + m_axialVelocity[idx[0]];
      return((interpolateModes(pts) * Q)(0,0));
        break;
      case IMPEDANCE:
        // if the propagation direction is backward, reverse the indexes
        correctIdxIfBackwardProp(Zdir());
        // if the impedance have not been computed
        if (m_impedance.size() == 0)
        {
          int numStep(m_admittance.size());
          for (int i(0); i < numStep; i++)
          {
            m_impedance.push_back(m_admittance[i].fullPivLu().inverse());
          }
        }
        // interpolate the amplitudes
        Q = (pt.x() - x_0)*(m_impedance[idx[1]] - m_impedance[idx[0]])/dx 
        + m_impedance[idx[0]];
        modes = interpolateModes(pts);
        return((modes.completeOrthogonalDecomposition().pseudoInverse() * Q * modes)(0, 0));
        break;
      case ADMITTANCE:
        // if the propagation direction is backward, reverse the indexes
        correctIdxIfBackwardProp(Ydir());
        // if the admittance have not been computed
        if (m_admittance.size() == 0)
        {
          int numStep(m_impedance.size());
          for (int i(0); i < numStep; i++)
          {
            m_admittance.push_back(m_impedance[i].fullPivLu().inverse());
          }
        }
        // interpolate the amplitudes
        Q = (pt.x() - x_0)*(m_admittance[idx[1]] - m_admittance[idx[0]])/dx 
        + m_admittance[idx[0]];
        modes = interpolateModes(pts).transpose();
        return((modes.completeOrthogonalDecomposition().pseudoInverse() * 
              Q.transpose() * modes)(0, 0));
        break;
     }
}

// **************************************************************************

complex<double> CrossSection2dFEM::interiorField(Point_3 pt, struct simulationParameters simuParams)
{
  return(interiorField(pt, simuParams, simuParams.fieldPhysicalQuantity));
}

// **************************************************************************
// accessors

int CrossSection2d::numPrevSec() const { return m_previousSections.size(); }
int CrossSection2d::numNextSec() const { return m_nextSections.size(); }
int CrossSection2d::prevSec(int idx) const { return m_previousSections[idx]; }
int CrossSection2d::nextSec(int idx) const { return m_nextSections[idx]; }
Point2D CrossSection2d::ctrLinePt() const { return(m_ctrLinePt); }
Point CrossSection2d::ctrLinePtIn() const { return(Point(m_ctrLinePt.x, m_ctrLinePt.y)); }
Point2D CrossSection2d::normal() const { return(m_normal); }
Vector CrossSection2d::normalIn() const { return(Vector(m_normal.x, m_normal.y)); }
double CrossSection2d::area() const { return(m_area); }
//******************************************************
Eigen::MatrixXcd CrossSection2d::Zin() const
{
  if (Zdir() == 1) {return m_impedance[0];}
  else { return m_impedance.back(); }
}
//******************************************************
Eigen::MatrixXcd CrossSection2d::Zout() const
{
  if (Zdir() == 1) {return m_impedance.back();}
  else { return m_impedance[0]; }
}
//******************************************************
Eigen::MatrixXcd CrossSection2d::Yin() const
{
  if (Ydir() == 1) { return m_admittance[0]; }
  else { return m_admittance.back(); }
}
//******************************************************
Eigen::MatrixXcd CrossSection2d::Yout() const
{
  if (Ydir() == 1) {return m_admittance.back();}
  else { return m_admittance[0]; }
}
//******************************************************
Eigen::MatrixXcd  CrossSection2d::Qin() const
{
  if (Qdir() == 1) { return m_axialVelocity[0]; }
  else { return m_axialVelocity.back(); }
}
//******************************************************
Eigen::MatrixXcd CrossSection2d::Qout() const
{
  if (m_axialVelocity.size() == 0)
  {
    return Yout() * Pout();
  }
  else if (Qdir() == 1) 
  {
    return m_axialVelocity.back(); 
  }
  else 
  { return m_axialVelocity[0]; 
  }
}
//******************************************************
Eigen::MatrixXcd CrossSection2d::Pin() const
{
  if (Pdir() == 1) { return m_acPressure[0]; }
  else { return m_acPressure.back(); }
}
//******************************************************
Eigen::MatrixXcd CrossSection2d::Pout() const
{
  if (m_acPressure.size() == 0)
  {
    return Eigen::MatrixXcd();
  }
  else
  {
    if (Pdir() == 1) { return m_acPressure.back(); }
    else { return m_acPressure[0]; }
  }
}
//******************************************************
Point CrossSection2dFEM::ctrLinePtOut() const
{
  if (length() > 0.)
  {
    Point Pt = ctrLinePtIn();
    Vector N(normalIn());
    double theta;
    if (abs(m_circleArcAngle) < MINIMAL_DISTANCE)
    {
      theta = -M_PI / 2.;
      Transformation rotate(CGAL::ROTATION, sin(theta), cos(theta));
      Transformation translate(CGAL::TRANSLATION, length() * rotate(N));
      return(translate(Pt));
    }
    else
    {
      theta = abs(m_circleArcAngle) / 2.;
      
      if ((signbit(m_curvatureRadius) && !signbit(m_curvatureRadius * m_circleArcAngle))
        || (!signbit(m_curvatureRadius) && signbit(m_curvatureRadius * m_circleArcAngle)))
      {
        Transformation rotate(CGAL::ROTATION, sin(M_PI / 2. - theta),
          cos(theta - M_PI / 2.));
        Transformation translate(CGAL::TRANSLATION,
          -2. * m_curvatureRadius * sin(theta) * rotate(-N));
        return(translate(Pt));
      }
      else
      {
        Transformation rotate(CGAL::ROTATION, sin(theta - M_PI / 2.),
          cos(theta - M_PI / 2.));
        Transformation translate(CGAL::TRANSLATION,
          2. * m_curvatureRadius * sin(theta) * rotate(N));
        return(translate(Pt));
      }
    }
  }
  else
  {
    return(ctrLinePtIn());
  }
}

//******************************************************

Vector CrossSection2dFEM::normalOut() const
{
  if (length() > 0.)
  {
    double thetaN(m_circleArcAngle);
    Transformation rotateN(CGAL::ROTATION, sin(thetaN), cos(thetaN));
    return(rotateN(normalIn()));
  }
  else
  {
    return(normalIn());
  }
}

//******************************************************

double CrossSection2dFEM::length() const { 
  if (abs(m_circleArcAngle) < MINIMAL_DISTANCE)
  {
    return(m_length); 
  }
  else {
    return(abs(m_circleArcAngle) * abs(m_curvatureRadius));
  }
 }

vector<double> CrossSection2dFEM::intersectionsArea() const { return m_intersectionsArea; }
double CrossSection2dFEM::spacing() const { return m_spacing; }
int CrossSection2dFEM::numberOfVertices() const { return m_mesh.number_of_vertices(); }
int CrossSection2dFEM::numberOfFaces() const { return m_mesh.number_of_faces(); }
CDT CrossSection2dFEM::triangulation() const { return m_mesh; }
Polygon_2 CrossSection2dFEM::contour() const { return m_contour; }
bool CrossSection2dFEM::isJunction() const { return m_junctionSection; }
vector<int> CrossSection2dFEM::surfaceIdx() const { return m_surfaceIdx; }
double CrossSection2dFEM::eigenFrequency(int idxMode) const { return m_eigenFreqs[idxMode]; }
vector<array<double, 2>> CrossSection2dFEM::getPoints() const { return m_points; }
vector<array<int, 3>> CrossSection2dFEM::getTriangles() const { return m_triangles; }
Matrix CrossSection2dFEM::getModes() const { return m_modes; }
double CrossSection2dFEM::getMaxAmplitude(int idxMode) const { return m_maxAmplitude[idxMode]; }
double CrossSection2dFEM::getMinAmplitude(int idxMode) const { return m_minAmplitude[idxMode]; }
vector<Matrix> CrossSection2dFEM::getMatrixF() const { return m_F; }
Matrix CrossSection2dFEM::getMatrixGStart() const { return m_Gstart; }
Matrix CrossSection2dFEM::getMatrixGEnd() const { return m_Gend; }

// ****************************************************************************
// Operator overload
// ****************************************************************************

// **************************************************************************
// Print cross-section parameters

ostream& operator<<(ostream &os, const CrossSection2d &cs)
{
  string csType = typeid(cs).name();
  os << csType << endl;

  if (csType == "class CrossSection2dFEM")
  {
    // write previous sections
    if (cs.numPrevSec() > 0)
    {
      os << "Previous sections: ";
      for (int i(0); i < cs.numPrevSec(); i++)
      {
        os << cs.prevSec(i) << "  ";
      }
    }
    os << endl;

    // write next sections
    if (cs.numNextSec() > 0)
    {
      os << "Next sections: ";
      for (int i(0); i < cs.numNextSec(); i++)
      {
        os << cs.nextSec(i) << "  ";
      }
    }
    os << endl;

    os << "Centerline points " << cs.ctrLinePtIn() << "  "
      << cs.ctrLinePtOut() << endl;
    os << "Normals " << cs.normalIn() << "  "
      << cs.normalOut() << endl;
    os << "Scalings " << cs.scaleIn() << "  " << cs.scaleOut() << endl;
    os << "Length " << cs.length() << endl;
    os << "Curvature radius " << cs.curvRadius() << endl;
    os << "Circle arc angle " << cs.circleArcAngle() << endl;
    os << "Area " << cs.area() << endl;
    os << "Is junction " << cs.isJunction() << endl;
  }

  return os;
}
