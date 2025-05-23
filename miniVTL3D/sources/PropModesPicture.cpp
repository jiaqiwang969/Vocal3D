// ****************************************************************************
// This file is part of VocalTractLab3D.
// Copyright (C) 2022, Peter Birkholz, Dresden, Germany
// www.vocaltractlab.de
// author: Peter Birkholz and Rémi Blandin
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

#include "PropModesPicture.h"
#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include "Backend/Geometry.h"
#include "Data.h"
#include <wx/rawbmp.h>
#include <chrono>
#include "ColorScale.h"
#include "TableTextPicture.h"

// for eigen
#include <Eigen/Dense>

// for CGAL
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>

// typedef for eigen
typedef Eigen::MatrixXd Matrix;
typedef Eigen::VectorXd Vec;

// typedef for CGAL
typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Polygon_2<K>                            Polygon_2;
typedef CGAL::Aff_transformation_2<K>           Transformation;

// for the colormap
typedef int(*ColorMap)[256][3];

// ****************************************************************************
// IDs.
// ****************************************************************************

static const int IDM_EXPORT_ACOUSTIC_FIELD = 1000;
static const int IDM_EXPORT_CONTOUR        = 1001;

// ****************************************************************************
// The event table.
// ****************************************************************************

BEGIN_EVENT_TABLE(PropModesPicture, BasicPicture)
EVT_MOUSE_EVENTS(PropModesPicture::OnMouseEvent)
EVT_MENU(IDM_EXPORT_ACOUSTIC_FIELD, PropModesPicture::OnExportAcousticField)
EVT_MENU(IDM_EXPORT_CONTOUR, PropModesPicture::OnExportContour)
END_EVENT_TABLE()

// ****************************************************************************
/// Constructor. Passes the parent parameter.
// ****************************************************************************

PropModesPicture::PropModesPicture(wxWindow* parent,
  Acoustic3dSimulation* simu3d, SegmentsPicture* segPic)
  : BasicPicture(parent),
  m_objectToDisplay(CONTOUR),
  m_modeIdx(0),
  m_positionContour(1)
{
	this->m_simu3d = simu3d;
  this->m_segPic = segPic;

  m_contextMenu = new wxMenu();
  m_contextMenu->Append(IDM_EXPORT_ACOUSTIC_FIELD, "Export acoustic field in text file");
  m_contextMenu->Append(IDM_EXPORT_CONTOUR, "Export contour in text file");
}

// ****************************************************************************
/// Draws the picture.
// ****************************************************************************

void PropModesPicture::draw(wxDC& dc)
{
	int width, height;
	//double zoom;
	int xBig, yBig, xEnd, yEnd;
	Data *data = Data::getInstance();
	VocalTract *tract = data->vocalTract;
	double cumLength(0.), minDist(1e-15);
	int sectionIdx(0);
  ostringstream info;
  TableTextPicture tbText(
      this->FromDIP(200),  // cell width
      this->FromDIP(20),   // cell height
      this->FromDIP(30),   // cell spacing
      2);                  // nb columns

  // clear info strings
  m_infoStr.clear();
  m_labelStr.clear();
  m_valueStr.clear();

	// Clear the background.
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();

	// check if the modes have been computed
	if (m_simu3d->sectionNumber() == 0)
	{
		dc.SetPen(*wxBLACK_PEN);
		dc.SetBackgroundMode(wxTRANSPARENT);
		dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
		dc.DrawText("No segments.", 0, 0);
		return;
	}

// ****************************************************************
// Identify the index of the corresponding tube
// ****************************************************************

  sectionIdx = m_segPic->activeSegment();
  auto seg = m_simu3d->crossSection(sectionIdx);

// ****************************************************************
// Determine the zoom (pix/cm).
// ****************************************************************

	// get the dimensions of the picture
	this->GetSize(&width, &height);
	m_centerX = width / 2;
	m_centerY = height / 2;
	double maxLength;
	pair<Point2D, Point2D> bbox(m_simu3d->maxCSBoundingBox());

	// get the maximal dimension of the cross-sections
	if (m_simu3d->isGeometryImported())
	{
		maxLength = 2.*max(max(abs(bbox.first.x), abs(bbox.second.x)), 
		max(abs(bbox.first.y), abs(bbox.second.y)));
	}
	else
	{
		maxLength = VocalTract::PROFILE_LENGTH;
	}

	if (width < height)
	{
		m_zoom = (double)width * 1 / maxLength;
	}
	else
	{
    m_zoom = (double)height * 1 / maxLength;
	}

  if ((seg->numberOfFaces() == 0)) {
    m_objectToDisplay = CONTOUR;
  }

  // ****************************************************************
  // add the informations to display as text
  // ****************************************************************

  tbText.addCell("Segment index", sectionIdx);
  tbText.addCell(" ", " ");
  tbText.addCell("Area (cm^2)", seg->area());
  tbText.addCell("Length (cm)", seg->length());
  tbText.addCell("Curv angle (deg)",
    180. * seg->circleArcAngle() / M_PI);
  tbText.addCell("Curv radius (cm)", seg->curvRadius());
  tbText.addCell("Scaling in", seg->scaleIn());
  tbText.addCell("Scaling out", seg->scaleOut());

// ****************************************************************
// Draw the mesh
// ****************************************************************

    vector<int> surf = seg->surfaceIdx();

		switch (m_objectToDisplay) {
		case MESH: {

			array<int, 3> tri;

			int numFaces = seg->numberOfFaces();
			vector<array<double, 2>> pts = seg->getPoints();
			vector<array<int, 3>> triangles = seg->getTriangles();

			auto start = std::chrono::system_clock::now();
			auto end = std::chrono::system_clock::now();
			std::chrono::duration<double> tTri;
			std::chrono::duration<double> tCoord;

      wxPen pen(*wxBLACK, lineWidth1);
			dc.SetPen(pen);
			tTri.zero();
			tCoord.zero();
			for (int it(0); it < numFaces; ++it)
			{
				start = std::chrono::system_clock::now();
				tri = triangles[it];
				end = std::chrono::system_clock::now();
				tTri += end - start;
				for (int v(0); v < 3; v++)
				{
					start = std::chrono::system_clock::now();
					xBig = (int)(m_zoom * (pts[tri[v]][0]) + m_centerX);
					yBig = (int)(m_centerY - m_zoom * (pts[tri[v]][1]));
					xEnd = (int)(m_zoom * (pts[tri[(v + 1) % 3]][0]) + m_centerX);
					yEnd = (int)(m_centerY - m_zoom * (pts[tri[(v + 1) % 3]][1]));
					end = std::chrono::system_clock::now();
					tCoord += end - start;
					dc.DrawLine(xBig, yBig, xEnd, yEnd);
				}
			}

      m_positionContour = 1;
      drawContour(sectionIdx, surf, dc);

			// display number of vertex, segments and triangles
      tbText.addCell("Nb vertexes", seg->numberOfVertices());
      tbText.addCell("nb faces", seg->numberOfFaces());
			}
			break;

			// ****************************************************************
			// plot the modes
			// ****************************************************************

		case TRANSVERSE_MODE: {

			auto start = std::chrono::system_clock::now();
			auto end = std::chrono::system_clock::now();
			std::chrono::duration<double> elapsed_seconds = end - start;

			int normAmp;
			double maxAmp(0.);
			double minAmp(0.);
			Point3D vecTri[3];
			Point3D pointToDraw;
			int numPtSide;
			double alpha;
			double beta;
			double maxDist;

			// check if the mode index is in the range of the number
			// of modes
			m_modeIdx = max(0, min(seg->numberOfModes()-1, m_modeIdx));

			ColorMap colorMap = ColorScale::getColorMap();

			// initialise white bitmap
			wxBitmap bmp(width, height, 24);
			wxNativePixelData data(bmp);
			wxNativePixelData::Iterator p(data);
			for (int i = 0; i < height; ++i)
			{
				wxNativePixelData::Iterator rowStart = p;
				for (int j = 0; j < width; ++j, ++p)
				{
					p.Red() = 254;
					p.Green() = 254;
					p.Blue() = 254;
				}
				p = rowStart;
				p.OffsetY(data, 1);
			}

			int numFaces = seg->numberOfFaces();
			int numVertex = seg->numberOfVertices();
			vector<array<double, 2>> pts = seg->getPoints();
			vector<array<int, 3>> triangles = seg->getTriangles();
			Matrix modes = seg->getModes();

			// extract the maximum and minimum of amplitude
			maxAmp = seg->getMaxAmplitude(m_modeIdx);
			minAmp = seg->getMinAmplitude(m_modeIdx);


			// draw the propagation mode
			//
			// The triangles are interpolated by adding the the fraction a and b 
			// of the vector formed by two sides and the corresponding vertex amplitude.
			// The corresponding values are attributed to the location of the nearest 
			// pixel in the bitmap grid. 
			// This avoid searching to which triangle a pixel belongs, which has a higher
			// computational cost.
			//
			//
			//   ^ P1
			//   |
			//   |   P?
			//   |________\ P2
			//  O         /
			//
			//  P = a*P1 + b*P2 + (1 - a - b)*O 
			//  
			//  with 0 <= a <= 1 and 0 <= b <= 1
			std::chrono::duration<double> tPt3, tCalc;
			tPt3.zero(); tCalc.zero();
			auto big = std::chrono::system_clock::now();
			auto fin = std::chrono::system_clock::now();

			for (int it(0); it < numFaces; ++it)
			{
				for (int i(0); i < 3; i++)
				{
					vecTri[i] = Point3D(pts[triangles[it][i]][0], pts[triangles[it][i]][1],
						modes(triangles[it][i], m_modeIdx));
				}

				big = std::chrono::system_clock::now();
				maxDist = max(sqrt(pow(vecTri[1].x - vecTri[0].x, 2) + pow(vecTri[1].y - vecTri[0].y, 2)),
					max(sqrt(pow(vecTri[2].x - vecTri[0].x, 2) + pow(vecTri[2].y - vecTri[0].y, 2)),
						sqrt(pow(vecTri[1].x - vecTri[2].x, 2) + pow(vecTri[1].y - vecTri[2].y, 2))));
				numPtSide = (int)ceil(maxDist * m_zoom) + 1;

				for (int i(0); i < numPtSide; i++)
				{
					for (int j(0); j < (numPtSide - i); j++)
					{
						alpha = ((double)(i) / (double)(numPtSide - 1));
						beta = ((double)(j) / (double)(numPtSide - 1));
						pointToDraw = alpha * vecTri[1] + beta * vecTri[2] +
							(1. - alpha - beta) * vecTri[0];
						normAmp = max(1, (int)(256 * (pointToDraw.z / max(maxAmp, -minAmp) + 1.) / 2.) - 1);
						p.MoveTo(data, (int)(m_zoom* pointToDraw.x + m_centerX), (int)(m_centerY - m_zoom * pointToDraw.y));
						p.Red() = (*colorMap)[normAmp][0];
						p.Green() = (*colorMap)[normAmp][1];
						p.Blue() = (*colorMap)[normAmp][2];
					}
				}
			}

			// write informations about the mode
      info.str("");
      info << m_modeIdx + 1 << " / "
        << seg->numberOfModes();
      tbText.addCell("mode", info.str());
      tbText.addCell("Cutoff freq (Hz)",
        seg->eigenFrequency(m_modeIdx));

			dc.DrawBitmap(bmp, 0, 0, 0);
		}
		break;

    // ****************************************************************
    // plot the junction matrix
    // ****************************************************************

		case JUNCTION_MATRIX: {
			vector<Matrix> F = seg->getMatrixF();
			int numCont(F.size());
			int maxNumF(0);

			maxNumF = 1;


			double widthFn((double)min(width, height) / ((double)maxNumF * 1.1));
			int margin((int)(0.05 * widthFn));
			int topMargin((int)(((double)height - maxNumF*widthFn*1.05) / 2.));
			int widthSquare, numCols, numRows, normAmp;
			double maxF, minF, normF;
			ColorMap colorMap = ColorScale::getColorMap();
			wxColor color;


			// loop over junction matrices
			for (int n(0); n < F.size(); n++)
			{
				numCols = F[n].cols();
				numRows = F[n].rows();
				// compute the width of the squares displaying the values of the matrix
				widthSquare = (int)(widthFn / (double)numCols);

				// search for the maximal and minimal values of the matrix
				maxF = 0.; minF = 0.;
				// loop over rows
				for (int r(0); r < numRows; r++)
				{
					// loop over columns
					for (int c(0); c < numCols; c++)
					{
						maxF = max(maxF, F[n](r, c));
						minF = min(minF, F[n](r, c));
					}
				}
				maxF = max(maxF, 1.);
				minF = min(minF, -1.);
				normF = 1. / (maxF - minF);

				// loop over rows
				for (int r(0); r < numRows; r++)
				{
					// loop over columns
					for (int c(0); c < numCols; c++)
					{

						normAmp = max(0, (int)(255. * normF * (F[n](r, c) - minF)));
						color = wxColor((*colorMap)[normAmp][0], (*colorMap)[normAmp][1],
							(*colorMap)[normAmp][2]);
						dc.SetPen(color);
						dc.SetBrush(color);
						dc.DrawRectangle(margin + min(width, height) / maxNumF + c * widthSquare, 
							topMargin + n* min(width, height) / maxNumF + r * widthSquare, 
							widthSquare, widthSquare);
					}
				}
			}

			}
		break;

    // ****************************************************************
    // plot the acoustic field
    // ****************************************************************

    case ACOUSTIC_FIELD: {

      if (seg->Pout().rows() > 0)
      {
        ColorMap colorMap = ColorScale::getColorMap();
        double maxAmp;
        double minAmp;
        // to avoid singular values when the field is displayed in dB
        double dbShift(0.5);
        Point3D vecTri[3];
        Point3D pointToDraw;
        int numPtSide;
        double alpha;
        double beta;
        double maxDist;
        int normAmp;

        m_field.resize(height, width);
        m_field.setConstant(NAN);

        int idxI, idxJ;

        // initialise white bitmap
        wxBitmap bmp(width, height, 24);
        wxNativePixelData data(bmp);
        wxNativePixelData::Iterator p(data);
        for (int i = 0; i < height; ++i)
        {
          wxNativePixelData::Iterator rowStart = p;
          for (int j = 0; j < width; ++j, ++p)
          {
            p.Red() = 254;
            p.Green() = 254;
            p.Blue() = 254;
          }
          p = rowStart;
          p.OffsetY(data, 1);
        }

        int numFaces = seg->numberOfFaces();
        int numVertex = seg->numberOfVertices();
        vector<array<double, 2>> pts = seg->getPoints();
        vector<array<int, 3>> triangles = seg->getTriangles();
        Matrix modes = seg->getModes();
        int mn(modes.cols());

        Eigen::MatrixXcd modesAmpl;
        switch (m_simu3d->fieldPhysicalQuantity())
        {
        case PRESSURE:
          modesAmpl = seg->Pout();
          break;

        case VELOCITY:
          modesAmpl = seg->Qout();
          break;
        }

        Vec amplitudes;
        if (m_simu3d->showFieldAmplitude())
        {
          amplitudes = (modes * modesAmpl).cwiseAbs();
        }
        else
        {
          amplitudes = (modes * modesAmpl).array().arg() + M_PI;
        }

        maxAmp = m_simu3d->maxAmpField();
        minAmp = m_simu3d->minAmpField();
        // if no acoustic field has been computed in the sagittal plane 
        // the maximum and minimum amplitude are taken from the local transverse field
        if (maxAmp < 0.)
        {
          maxAmp = amplitudes.maxCoeff();
          minAmp = amplitudes.minCoeff();
        }
        if (m_simu3d->fieldIndB()) {
          maxAmp = 20. * log10(maxAmp);
          minAmp = max(maxAmp - 80, 20. * log10(minAmp));
          maxAmp = maxAmp - minAmp + dbShift;
        }

        if (!m_simu3d->showFieldAmplitude())
        {
          maxAmp += M_PI;
          minAmp += M_PI;
        }

        // draw the acoustic field
        //
        // The triangles are interpolated by adding the the fraction a and b 
        // of the vector formed by two sides and the corresponding vertex amplitude.
        // The corresponding values are attributed to the location of the nearest 
        // pixel in the bitmap grid. 
        // This avoid searching to which triangle a pixel belongs, which has a higher
        // computational cost.
        //
        //
        //   ^ P1
        //   |
        //   |   P?
        //   |________\ P2
        //  O         /
        //
        //  P = a*P1 + b*P2 + (1 - a - b)*O 
        //  
        //  with 0 <= a <= 1 and 0 <= b <= 1

        for (int it(0); it < numFaces; ++it)
        {
          for (int i(0); i < 3; i++)
          {
            vecTri[i] = Point3D(pts[triangles[it][i]][0], pts[triangles[it][i]][1],
              amplitudes(triangles[it][i]));
          }

          maxDist = max(sqrt(pow(vecTri[1].x - vecTri[0].x, 2) + pow(vecTri[1].y - vecTri[0].y, 2)),
            max(sqrt(pow(vecTri[2].x - vecTri[0].x, 2) + pow(vecTri[2].y - vecTri[0].y, 2)),
              sqrt(pow(vecTri[1].x - vecTri[2].x, 2) + pow(vecTri[1].y - vecTri[2].y, 2))));
          numPtSide = (int)ceil(maxDist * m_zoom) + 1;

          for (int i(0); i < numPtSide; i++)
          {
            for (int j(0); j < (numPtSide - i); j++)
            {
              alpha = ((double)(i) / (double)(numPtSide - 1));
              beta = ((double)(j) / (double)(numPtSide - 1));
              pointToDraw = alpha * vecTri[1] + beta * vecTri[2] +
                (1. - alpha - beta) * vecTri[0];
              idxI = min((int)(m_zoom * pointToDraw.x + m_centerX), width - 1);
              idxJ = min((int)(m_centerY - m_zoom * pointToDraw.y), height - 1);
              m_field(height - idxJ -1, idxI) = pointToDraw.z;
              if (m_simu3d->fieldIndB())
              {
                pointToDraw.z = 20. * log10(pointToDraw.z) - minAmp + dbShift;
              }
              normAmp = min(255, max(1, (int)(256. * pointToDraw.z / max(maxAmp, abs(minAmp))) - 1));

              p.MoveTo(data, idxI, idxJ);
              p.Red() = (*colorMap)[normAmp][0];
              p.Green() = (*colorMap)[normAmp][1];
              p.Blue() = (*colorMap)[normAmp][2];
            }
          }
        }
        // write informations
        tbText.addCell("Frequency (Hz)", m_simu3d->lastFreqComputed());

        dc.DrawBitmap(bmp, 0, 0, 0);
        break;
      }
    }

      default:
      {
        double scaling;
        bool needToScale(false);

        switch (m_positionContour)
        {
        case 0:
        {
          scaling = seg->scaleIn();
          needToScale = true;
          tbText.addCell("Entrance", "");
          break;
        }
        case 1:
          tbText.addCell("Mode computation size", "");
          break;
        case 2:
          scaling = seg->scaleOut();
          needToScale = true;
          tbText.addCell("Exit", "");
          break;
        }

        if (needToScale)
        {
          Transformation scale(CGAL::SCALING, scaling);
          double scaledArea = abs(transform(scale, seg->contour()).area());

          // modify the value of te cross-sectional area displayed
          tbText.setCell(2, "Area (cm^2)", scaledArea);
        }

        drawContour(sectionIdx, surf, dc);
        break;
      }
		}

    // Print the text on the picture
    wxColour originalTextColor = dc.GetTextForeground(); // Save original text color
    dc.SetTextForeground(*wxRED); // Set text color to red
    tbText.printCells(dc);
    dc.SetTextForeground(originalTextColor); // Restore original text color
}

// ****************************************************************************
// ****************************************************************************

enum objectToDisplay PropModesPicture::getObjectDisplayed()
{
  return(m_objectToDisplay);
}

// ****************************************************************************

void PropModesPicture::setObjectToDisplay(enum objectToDisplay object)
{
  m_objectToDisplay = object;
  Refresh();
}

// ****************************************************************************

void PropModesPicture::setModeIdx(int idx)
{
	m_modeIdx = idx;
	Refresh();
}

// ****************************************************************************

double PropModesPicture::getScaling()
{
  // determine which contour must be plot
  CrossSection2d* seg = m_simu3d->crossSection(m_segPic->activeSegment());
  double scaling;
  switch (m_positionContour)
  {
  case 0:
    scaling = seg->scaleIn();
    break;
  case 1:
    scaling = 1;
    break;
  case 2:
    scaling = seg->scaleOut();
    break;
  }
  return(scaling);
}

// ****************************************************************************

void PropModesPicture::drawContour(int sectionIdx, vector<int> &surf, wxDC& dc)
{
  CrossSection2d* seg = m_simu3d->crossSection(sectionIdx);
  Polygon_2 contour = seg->contour();
  CGAL::Polygon_2<K>::Edge_const_iterator vit;
  int s, xBig, yBig, xEnd, yEnd;
  double scaling(getScaling());
  int penWidth(5);

  // colors are from https://davidmathlogic.com/colorblind
  for (s = 0, vit = contour.edges_begin(); vit != contour.edges_end(); ++vit, ++s)
  {

    // get the coordinate of the segment of the contour
    xBig = (int)(m_zoom * (scaling * vit->point(0).x()) + m_centerX);
    yBig = (int)(m_centerY - m_zoom * (scaling * vit->point(0).y()));
    xEnd = (int)(m_zoom * (scaling * vit->point(1).x()) + m_centerX);
    yEnd = (int)(m_centerY - m_zoom * (scaling * vit->point(1).y()));

    if (m_objectToDisplay == CONTOUR)
    {
      switch (surf[s])
      {
      case 2: case 3: case 23: case 24:		// covers
        dc.SetPen(wxPen(wxColour(68, 170, 153, 255), penWidth, wxPENSTYLE_SOLID));
        break;
      case 16:						// tongue
        dc.SetPen(wxPen(wxColour(170, 68, 153, 255), penWidth, wxPENSTYLE_SOLID));
        break;
      case 0: case 1:					// teeth
        dc.SetPen(wxPen(wxColour(221, 204, 119, 255), penWidth, wxPENSTYLE_SOLID));
        break;
      case 29:						// epiglotis
        dc.SetPen(wxPen(wxColour(51, 34, 136, 255), penWidth, wxPENSTYLE_SOLID));
        break;
      case 26:						// uvula
        dc.SetPen(wxPen(wxColour(204, 102, 119, 255), penWidth, wxPENSTYLE_SOLID));
        break;
      case 4: case 5:					// lips
        dc.SetPen(wxPen(wxColour(136, 34, 85, 255), penWidth, wxPENSTYLE_SOLID));
        break;
      case 31:						// radiation
        dc.SetPen(wxPen(wxColour(136, 204, 238, 255), penWidth, wxPENSTYLE_SOLID));
        break;
      default:
        dc.SetPen(wxPen(wxColour(68, 170, 153, 255), penWidth, wxPENSTYLE_SOLID));
      }

      // draw a colored dot to indicate the nature of the anatomical part 
      // corresponding to the surface
      dc.DrawCircle(xBig, yBig, 1);
    }

    // draw the segment
    dc.SetPen(*wxBLACK_PEN);
    dc.DrawLine(xBig, yBig, xEnd, yEnd);
  }
}

// ****************************************************************************

void PropModesPicture::OnMouseEvent(wxMouseEvent& event)
{
  // Right click
  if (event.ButtonDown(wxMOUSE_BTN_RIGHT) && (m_simu3d->numberOfSegments() > 0))
  {
    PopupMenu(m_contextMenu);
  }
}

// ****************************************************************************

void PropModesPicture::OnExportAcousticField(wxCommandEvent& event)
{
  wxFileName fileName;
  wxString name = wxFileSelector("Save acoustic field", fileName.GetPath(),
    fileName.GetFullName(), ".txt", "(*.txt)|*.txt",
    wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);

  // export field
  ofstream ofs(name.ToStdString());
  stringstream txtField;
  txtField << m_field;
  ofs << regex_replace(txtField.str(), regex("-nan\\(ind\\)"), "nan");
  ofs.close();

  ofstream log("log.txt", ofstream::app);
  log << "Transverse acoustic field of segment " 
    << m_segPic->activeSegment() << " saved in file:" << endl;
  log << name.ToStdString() << endl;
  log.close();
}

// ****************************************************************************

void PropModesPicture::OnExportContour(wxCommandEvent& event)
{
  wxFileName fileName;
  wxString name = wxFileSelector("Save contour", fileName.GetPath(),
    fileName.GetFullName(), ".txt", "(*.txt)|*.txt",
    wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);

  ofstream ofs(name.ToStdString());
  if (ofs.is_open())
  {
    double scaling(getScaling());
    int sectionIdx = m_segPic->activeSegment();
    for (auto pt : m_simu3d->crossSection(sectionIdx)->contour())
    {
      ofs << scaling * pt.x() << "  " 
        << scaling * pt.y() << endl;
    }
    ofs.close();

    ofstream log("log.txt", ofstream::app);
    log << "Contour of segment " 
      << m_segPic->activeSegment() << " saved in file:\n"
      << name.ToStdString() << endl;
    log.close();
  }
}

// ****************************************************************************

void PropModesPicture::prevContourPosition()
{
  m_positionContour = max(0, m_positionContour - 1);
  Refresh();
}

// ****************************************************************************

void PropModesPicture::nextContourPosition()
{
  m_positionContour = min(2, m_positionContour + 1);
  Refresh();
}
