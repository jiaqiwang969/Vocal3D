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

#ifndef __CROSS_SECTION_2D_H__
#define __CROSS_SECTION_2D_H__

#include <string>
#include <chrono>    // to get the computation time
#include <ctime>  
#include <vector>
#include <boost/bimap.hpp>
#include "Geometry.h"

// for eigen
#include <Eigen/Dense>
#include <Eigen/Sparse>

// for CGAL
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include "Delaunay_mesh_vertex_base_with_info_2.h"
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Delaunay_triangulation_2.h>
#include <CGAL/Delaunay_mesh_face_base_2.h>
#include <CGAL/Boolean_set_operations_2.h>

using namespace std;

// typedef for eigen
typedef Eigen::MatrixXd Matrix;
typedef Eigen::SparseMatrix<complex<double>> SparseMatC;

// typedef for CGAL
typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Delaunay_mesh_vertex_base_with_info_2<unsigned int, K>    Vb;
typedef CGAL::Delaunay_mesh_face_base_2<K> Fb;
typedef CGAL::Triangulation_data_structure_2<Vb, Fb> Tds;
typedef CGAL::Exact_intersections_tag                     Itag;
typedef CGAL::Constrained_Delaunay_triangulation_2<K, Tds, Itag> CDT;
typedef CDT::Point                  Point;
typedef CGAL::Point_3<K>                Point_3;
typedef CGAL::Vector_2<K>                Vector;

typedef CGAL::Polygon_2<K>                          Polygon_2;

enum propagationMethod {
  MAGNUS,
  STRAIGHT_TUBES
};

enum physicalQuantity {
  IMPEDANCE,
  ADMITTANCE,
  PRESSURE,
  VELOCITY
};

enum areaVariationProfile {
  LINEAR,
  GAUSSIAN,
  ELEPHANT
};

enum integrationMethodRadiation {
  DISCRETE,
  GAUSS
};

struct simulationParameters
{
  double temperature;
  double volumicMass;
  double sndSpeed;
  int numIntegrationStep;
  int orderMagnusScheme;
  double maxCutOnFreq;
  complex<double> viscousBndSpecAdm;
  complex<double> thermalBndSpecAdm;
  enum propagationMethod propMethod;
  double percentageLosses;
  bool viscoThermalLosses;
  bool wallLosses;
  bool constantWallImped;
  complex<double> wallAdmit;
  bool curved;
  bool varyingArea;
  bool junctionLosses;
  bool needToComputeModesAndJunctions;
  bool radImpedPrecomputed;
  double radImpedGridDensity;
  enum integrationMethodRadiation integrationMethodRadiation;

  // for transfer function computation
  double maxComputedFreq;
  int spectrumLgthExponent;
  vector<Point_3> tfPoint;

  // for acoustic field computation
  double freqField;
  enum physicalQuantity fieldPhysicalQuantity;
  bool showAmplitude;
  bool fieldIndB;
  Point bbox[2];
  Point bboxLastFieldComputed[2];
  int fieldResolution;        // number of points per cm
  int fieldResolutionPicture; // number of points per cm of the last field computation
  bool computeRadiatedField;
  bool computeFieldImage;
};

/////////////////////////////////////////////////////////////////////////////
// classe Cross section 2d
/////////////////////////////////////////////////////////////////////////////

class CrossSection2d
{

public:

  CrossSection2d();
  CrossSection2d(Point2D ctrLinePt, Point2D normal);
  ~CrossSection2d();

  // cross section parameters
  virtual void setJunctionSection(bool junction) { ; }
  void setComputImpedance(bool imp) { m_computeImpedance = imp; }
  void setPreviousSection(int prevSec);
  void setPrevSects(vector<int> prevSects) { m_previousSections = prevSects; }
  void setNextSection(int nextSec);
  void setNextSects(vector<int> nextSects) { m_nextSections = nextSects; }
  void clearPrevSects() { m_previousSections.clear(); }
  void clearNextSects() { m_nextSections.clear(); }
  void setZdir(int dir) { m_direction[0] = dir; }
  void setYdir(int dir) { m_direction[1] = dir; }
  void setQdir(int dir) { m_direction[2] = dir; }
  void setPdir(int dir) { m_direction[3] = dir; }
  virtual void setCurvatureRadius(double radius) { ; }
  virtual void setCurvatureAngle(double angle) { ; }

  // cross section mesh and modes
  virtual void setSpacing(double spacing) { ; }
  virtual void buildMesh() { ; }
  void setModesNumber(int nb) { m_modesNumber = nb; }
  virtual void computeModes(struct simulationParameters simuParams) { ; }
  virtual void selectModes(vector<int> modesIdx) { ; }
  virtual Matrix interpolateModes(vector<Point> pts) { return Matrix(); }
  Matrix interpolateModes(vector<Point> pts, double scaling);
  Matrix interpolateModes(vector<Point> pts, double scaling, Vector translation);

  // scatering  matrices 
  virtual void setMatrixF(vector<Matrix> & F) { ; }
  virtual void setMatrixE(Matrix & E) {;}
  virtual void setMatrixGstart(Matrix Gs) { ; }
  virtual void setMatrixGend(Matrix Ge) { ; }

  // impedance, admittance, acoustic pressure and axial velocity
  void setImpedance(vector<Eigen::MatrixXcd> inputImped) { m_impedance = inputImped; }
  void setZin(Eigen::MatrixXcd imped);
  void setZout(Eigen::MatrixXcd imped);
  void clearImpedance() { m_impedance.clear(); }
  void setAdmittance(vector<Eigen::MatrixXcd> inputAdmit) { m_admittance = inputAdmit; }
  void setYin(Eigen::MatrixXcd admit);
  void setYout(Eigen::MatrixXcd admit);
  void clearAdmittance() { m_admittance.clear(); }
  virtual void characteristicImpedance(
    Eigen::MatrixXcd & characImped, double freq, struct simulationParameters simuParams) {;}
  virtual void characteristicAdmittance(
    Eigen::MatrixXcd& admit, double freq, struct simulationParameters simuParams) {;}
  virtual complex<double> getWallAdmittance( 
    struct simulationParameters simuParams, double freq) { return complex<double>(); }
  virtual void getSpecificBndAdm(struct simulationParameters simuParams, double freq, 
    Eigen::VectorXcd& bndSpecAdm) {;}
  void setAxialVelocity(vector<Eigen::MatrixXcd> inputVelocity) { m_axialVelocity = inputVelocity; }
  void clearAxialVelocity() { m_axialVelocity.clear(); }
  void setAcPressure(vector<Eigen::MatrixXcd> inputPressure) { m_acPressure = inputPressure; }
  void clearAcPressure() { m_acPressure.clear(); }

  // propagation 
  virtual double scaling(double tau){return 1.;}
  virtual double scalingDerivative(double tau){return 1.;}
  virtual void setAreaVariationProfileType(enum areaVariationProfile profile){;}
  virtual void propagateMagnus(Eigen::MatrixXcd Q0, struct simulationParameters simuParams,
    double freq, double direction, enum physicalQuantity quant, std::chrono::duration<double> *time) {;}
  virtual void propagateImpedAdmitStraight(Eigen::MatrixXcd Z0,
    Eigen::MatrixXcd Y0, double freq, struct simulationParameters simuParams,
    double prevArea, double nextArea) {;}
  virtual void propagatePressureVelocityStraight(Eigen::MatrixXcd V0,
    Eigen::MatrixXcd P0, double freq, struct simulationParameters simuParams, double nextArea) {;}

  // for acoustic field computation
  virtual bool getCoordinateFromCartesianPt(Point_3 pt, Point_3 &ptOut, bool useBbox){return bool();}
  virtual void radiatePressure(double distance, double freq,
    struct simulationParameters simuParams, Eigen::MatrixXcd& pressAmp) { ; }
  virtual complex<double> pin(Point pt) {return complex<double>();}
  virtual complex<double> pout(Point pt) {return complex<double>();}
  virtual complex<double> qin(Point pt) {return complex<double>();}
  virtual complex<double> qout(Point pt) {return complex<double>();}
  virtual complex<double> p(Point_3 pt, struct simulationParameters simuParams)
    {return complex<double>();}
  virtual complex<double> q(Point_3 pt, struct simulationParameters simuParams)
    {return complex<double>();}
  virtual complex<double> interiorField(Point_3 pt, struct simulationParameters simuParams,
          enum physicalQuantity quant)
    {return complex<double>();}
  virtual complex<double> interiorField(Point_3 pt, struct simulationParameters simuParams)
    {return complex<double>();}

  // **************************************************************************
  // accessors

  int numPrevSec() const;
  int numNextSec() const;
  int prevSec(int idx) const;
  vector<int> prevSections() const { return m_previousSections; }
  int nextSec(int idx) const;
  vector<int> nextSections() const { return m_nextSections; }
  bool computeImpedance() const { return m_computeImpedance; }
  Point2D ctrLinePt() const;
  Point ctrLinePtIn() const;
  virtual Point ctrLinePtOut() const { return Point(); }
  Point2D normal() const;
  Vector normalIn() const;
  virtual Vector normalOut() const { return Vector(); }

  double area() const;
  int numberOfModes() const { return m_modesNumber; }

  virtual double scaleIn() const { return 1.;  }
  virtual double scaleOut() const { return 1.; }
  virtual double length() const { return 0.; }
  virtual vector<double> intersectionsArea() const { return vector<double>(0); }
  virtual double curvature() { return 0.; }
  virtual double circleArcAngle() const { return 0.; }
  virtual double spacing() const { return 0.; }
  virtual int numberOfVertices() const { return 0; }
  virtual int numberOfFaces() const { return 0; }
  virtual CDT triangulation() const { return CDT(); }
  virtual Polygon_2 contour() const { return Polygon_2(); }
  virtual bool isJunction() const { return bool(); }
  virtual vector<int> surfaceIdx() const { return vector<int>(); }
  virtual double eigenFrequency(int idxMode) const { return 0.; }
  virtual vector<array<double, 2>> getPoints() const { return vector<array<double, 2>>(); }
  virtual vector<array<int, 3>> getTriangles() const { return vector<array<int, 3>>(); }
  virtual Matrix getModes() const { return Matrix(); }
  virtual double getMaxAmplitude(int idxMode) const { return 0.; }
  virtual double getMinAmplitude(int idxMode) const { return 0.; }
  virtual vector<Matrix> getMatrixF() const { return vector<Matrix>(); }
  virtual Matrix getMatrixGStart() const { return Matrix(); }
  virtual Matrix getMatrixGEnd() const { return Matrix(); }
  virtual Matrix getMatrixC() const { return Matrix(); }
  virtual Matrix getMatrixD() const { return Matrix(); }
  virtual Matrix getMatrixE() const {return Matrix();}
  virtual Matrix getMatrixKR2(int idx) const { return Matrix(); }
  virtual vector<Matrix> getMatrixKR2() const { return vector<Matrix>(); }
  virtual double curvRadius() const { return double(); }
  virtual double radius() const { return double(); }
  virtual double PMLThickness() const { return double(); }

  int Zdir() const { return m_direction[0]; }
  int Ydir() const { return m_direction[1]; }
  int Qdir() const { return m_direction[2]; }
  int Pdir() const { return m_direction[3]; }
  vector<Eigen::MatrixXcd> Z() const { return m_impedance; }
  Eigen::MatrixXcd Zin() const;
  Eigen::MatrixXcd Zout() const;
  vector<Eigen::MatrixXcd> Y() const { return m_admittance; }
  Eigen::MatrixXcd Yin() const;
  Eigen::MatrixXcd Yout() const;
  vector<Eigen::MatrixXcd> Q() const { return m_axialVelocity; }
  Eigen::MatrixXcd Qin() const; 
  Eigen::MatrixXcd Qout() const;
  vector<Eigen::MatrixXcd> P() const { return m_acPressure; }
  Eigen::MatrixXcd Pin() const;
  Eigen::MatrixXcd Pout() const;

protected:

  vector<int> m_previousSections;
  vector<int> m_nextSections;
  Point2D m_ctrLinePt;
  Point2D m_normal;
  double m_area;
  int m_modesNumber;
  int m_direction[4]; // 0 dir Z | 1 dir Y | 2 dir Q | dir P
  vector<Eigen::MatrixXcd> m_impedance;
  vector<Eigen::MatrixXcd> m_admittance;
  vector<Eigen::MatrixXcd> m_axialVelocity;
  vector<Eigen::MatrixXcd> m_acPressure;
  bool m_computeImpedance;

};

/////////////////////////////////////////////////////////////////////////////
// classe Cross section 2d FEM
/////////////////////////////////////////////////////////////////////////////

class CrossSection2dFEM : public CrossSection2d
{
  // **************************************************************************
  // Public functions.
  // **************************************************************************

public:

  CrossSection2dFEM(Point2D ctrLinePt, Point2D normal,
    double area, double spacing, Polygon_2 contour, vector<int> surfacesIdx,
    double inLength, double scalingFactors[2]);
  ~CrossSection2dFEM();

  void setJunctionSection(bool junction);
  void setCurvatureRadius(double radius);
  void setCurvatureAngle(double angle);

  void setSpacing(double spacing) { m_spacing = spacing; }
  void buildMesh();
  void computeModes(struct simulationParameters simuParams);
  void selectModes(vector<int> modesIdx);
  Matrix interpolateModes(vector<Point> pts);
  void setMatrixF(vector<Matrix> & F) { m_F = F; }
  void setMatrixE(Matrix & E) {m_E = E;}
  // Set the area of the intersection with the following contour
  void setMatrixGstart(Matrix & Gs){ m_Gstart = Gs; }
  void setMatrixGend(Matrix Ge) {m_Gend = Ge;}
  void characteristicImpedance(Eigen::MatrixXcd& characImped, 
    double freq, struct simulationParameters simuParams);
  void characteristicAdmittance(Eigen::MatrixXcd& admit, 
    double freq, struct simulationParameters simuParams);
  complex<double> getWallAdmittance(struct simulationParameters simuParams, double freq);
  void getSpecificBndAdm(struct simulationParameters simuParams, double freq, 
    Eigen::VectorXcd& bndSpecAdm);

  // propagation
  double curvature(bool curved);
  void setAreaVariationProfileType(enum areaVariationProfile profile);
  double scaling(double tau);
  double scalingDerivative(double tau);
  void propagateMagnus(Eigen::MatrixXcd Q0, struct simulationParameters simuParams,
    double freq, double direction, enum physicalQuantity quant, std::chrono::duration<double> *time);
  void propagateImpedAdmitStraight(Eigen::MatrixXcd Z0,
    Eigen::MatrixXcd Y0, double freq, struct simulationParameters simuParams,
    double prevArea, double nextArea);
  void propagatePressureVelocityStraight(Eigen::MatrixXcd V0,
    Eigen::MatrixXcd P0, double freq, struct simulationParameters simuParams,
    double nextArea);

  // for acoustic field computation
  bool getCoordinateFromCartesianPt(Point_3 pt, Point_3 &ptOut, bool useBbox);
  complex<double> pin(Point pt); 
  complex<double> pout(Point pt); 
  complex<double> qin(Point pt); 
  complex<double> qout(Point pt);
  complex<double> p(Point_3 pt, struct simulationParameters simuParams);
  complex<double> q(Point_3 pt, struct simulationParameters simuParams);
  complex<double> interiorField(Point_3 pt, struct simulationParameters simuParams,
          enum physicalQuantity quant);
  complex<double> interiorField(Point_3 pt, struct simulationParameters simuParams);

  // **************************************************************************
  // accessors

  Point ctrLinePtOut() const;
  Vector normalOut() const;
  double scaleIn() const { return m_scalingFactors[0];  }
  double scaleOut() const { return m_scalingFactors[1]; }
  double length() const;
  double curvRadius() const { return m_curvatureRadius; }
  vector<double> intersectionsArea() const;
  double circleArcAngle() const { return m_circleArcAngle; }
  double spacing() const;
  int numberOfVertices() const;
  int numberOfFaces() const;
  CDT triangulation() const;
  Polygon_2 contour() const;
  bool isJunction() const;
  vector<int> surfaceIdx() const;
  double eigenFrequency(int idxMode) const;
  vector<array<double, 2>> getPoints() const;
  vector<array<int, 3>> getTriangles() const;
  Matrix getModes() const;
  double getMaxAmplitude(int idxMode) const;
  double getMinAmplitude(int idxMode) const;
  vector<Matrix> getMatrixF() const;
  Matrix getMatrixGStart() const;
  Matrix getMatrixGEnd() const;
  Matrix getMatrixC() const { return m_C; }
  Matrix getMatrixD() const { return m_DN; }
  Matrix getMatrixE() const {return m_E;}
  Matrix getMatrixKR2(int idx) const { return m_KR2[idx]; }
  vector<Matrix> getMatrixKR2() const { return m_KR2; }


  // **************************************************************************
  // Private data.
  // **************************************************************************

private:

  enum areaVariationProfile m_areaProfile;
  double m_scalingFactors[2];
  double m_curvatureRadius;
  double m_circleArcAngle;
  CDT m_mesh;
  vector<array<double, 2>> m_points;
  vector<array<int, 3>> m_triangles;
  vector<array<int, 2>> m_meshContourSeg;
  Polygon_2 m_contour;
  double m_perimeter;
  bool m_junctionSection;
  vector<int> m_surfaceIdx;        // surface indexes of cont pts
  vector<int> m_surfIdxList;        // list of different surf idx
  double m_length;
  //double m_meshDensity;
  vector<double> m_intersectionsArea;
  double m_spacing;
  vector<double> m_eigenFreqs;
  Matrix m_modes;
  vector<double> m_maxAmplitude;
  vector<double> m_minAmplitude;
  vector<Matrix> m_F;
  Matrix m_Gstart;
  Matrix m_Gend;
  Matrix m_C;
  Matrix m_DN;
  vector<Matrix> m_DR;
  Matrix m_E;
  vector<Matrix> m_KR2;

};

/////////////////////////////////////////////////////////////////////////////
// classe Cross section 2d radiation
/////////////////////////////////////////////////////////////////////////////

class CrossSection2dRadiation : public CrossSection2d
{
// **************************************************************************
// Public functions.
// **************************************************************************

public:

  CrossSection2dRadiation(Point2D ctrLinePt, Point2D normal, double radius, double PMLThickness);
  ~CrossSection2dRadiation() { ; }

  void computeModes(struct simulationParameters simuParams);
  void selectModes(vector<int> modesIdx) { ; }
  Matrix interpolateModes(vector<Point> pts);

  void characteristicImpedance(
    Eigen::MatrixXcd& characImped, double freq, struct simulationParameters simuParams);
  void characteristicAdmittance(Eigen::MatrixXcd &admit, 
    double freq, struct simulationParameters simuParams);
  complex<double> getWallAdmittance(struct simulationParameters simuParams,
    double freq) {return complex<double>(); }
  void getSpecificBndAdm(struct simulationParameters simuParams, double freq,
    Eigen::VectorXcd& bndSpecAdm) {;}

  // propagation
  void propagateMagnus(Eigen::MatrixXcd Q0, struct simulationParameters simuParams,
    double freq, double direction, enum physicalQuantity quant, std::chrono::duration<double> *time) {;}
  void propagateImpedAdmitStraight(Eigen::MatrixXcd Z0,
    Eigen::MatrixXcd Y0, double freq, struct simulationParameters simuParams,
    double prevArea, double nextArea);
  void propagatePressureVelocityStraight(Eigen::MatrixXcd V0,
    Eigen::MatrixXcd P0, double freq, struct simulationParameters simuParams,
    double nextArea);
  // to get the amplitude of the pressure modes at a given distance from the exit
  void radiatePressure(double distance, double freq, 
    struct simulationParameters simuParams, Eigen::MatrixXcd& pressAmp);

  // **************************************************************************
  // Accessors

  double scaleIn() const { return 1.; }
  double scaleOut() const { return 1.; }
  double radius() const { return m_radius; }
  double PMLThickness() const { return m_PMLThickness; }
  double BesselZero(int m) const { return m_BesselZeros[m]; }
  int BesselOrder(int m) const { return m_BesselOrder[m]; }


// **************************************************************************
// Private data.
// **************************************************************************

private:

  double m_radius;
  double m_PMLThickness;
  vector<double> m_BesselZeros;
  vector<int> m_BesselOrder;
  vector<bool> m_degeneration;
  vector<double> m_normModes;
  SparseMatC m_CPML;
  SparseMatC m_DPML;
  Eigen::MatrixXcd m_eigVec, m_invEigVec;
  Eigen::VectorXcd m_eigVal;

// **************************************************************************
// Private functions.
// **************************************************************************

  void setBesselParam(struct simulationParameters simuParams);
};

// Print cross-section parameters
ostream& operator<<(ostream &os, const CrossSection2d &cs);

#endif
