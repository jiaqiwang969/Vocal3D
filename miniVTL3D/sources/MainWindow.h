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

#ifndef _MAIN_WINDOW_H_
#define _MAIN_WINDOW_H_

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/filename.h>
// #include "SignalPage.h"
// #include "VocalTractPage.h"
// #include "TdsPage.h"
// #include "GesturalScorePage.h"
#include "Acoustic3dPage.h"						   

#include "VocalTractShapesDialog.h"
#include "PhoneticParamsDialog.h"
// #include "AnatomyParamsDialog.h" // Removed as the dialog and its core logic (AnatomyParams) are being removed
#include "TdsOptionsDialog.h"
#include "FdsOptionsDialog.h"
#include "LfPulseDialog.h"
#include "SpectrumOptionsDialog.h"
#include "GlottisDialog.h"
// #include "VocalTractDialog.h"
#include "AnalysisResultsDialog.h"
//#include "AnnotationDialog.h"
#include "PoleZeroDialog.h"
#include "TransitionDialog.h"
#include "ParamSimu3DDialog.h"

#include "Data.h"

// ****************************************************************************
// Main window of the application.
// ****************************************************************************

class MainWindow : public wxFrame
{
  // **************************************************************************
  // Public data.
  // **************************************************************************


  // **************************************************************************
  // Public functions.
  // **************************************************************************

public:
  MainWindow();
  void initWidgets();
  void updateWidgets();
    
  // ****************************************************************************
  // Public data.
  // **************************************************************************

public:
  static const int NUM_TOOLBAR_TOOLS = 2;

  // Notebook and pages in that notebook
  wxNotebook *notebook;
  // SignalPage *signalPage;
  // VocalTractPage *vocalTractPage;
  // TdsPage *tdsPage;
  // GesturalScorePage *gesturalScorePage;
  Acoustic3dPage *acoustic3dPage;								 

  wxToolBar *toolBar;
  // wxMenuBar *menuBar;
  wxMenu *clearContextMenu;
  wxStatusBar *statusBar;

  wxButton *btnTest;

  wxFileName audioFileName;
  wxFileName exportFileName;
  Data *data;
  Acoustic3dSimulation* simu3d;

  // **************************************************************************
  // Private functions.
  // **************************************************************************

private:
  bool saveWaveformAsTxtFile(const wxString &fileName, Signal16 *signal, int pos, int length);
  bool saveAreaFunction(const wxString &fileName);
  bool saveSpectrum(const wxString &fileName, ComplexSignal *spectrum);

  // Window events
  void OnCloseWindow(wxCloseEvent &event);
  void OnPageChanged(wxNotebookEvent &event);

  // Menu functions
  void OnLoadWavEgg(wxCommandEvent &event);
  void OnSaveWavEgg(wxCommandEvent &event);
  void OnLoadWav(wxCommandEvent &event);
  void OnSaveWav(wxCommandEvent &event);
  void OnSaveWavAsTxt(wxCommandEvent &event);
  // void OnLoadSegmentSequence(wxCommandEvent &event); // Removed
  // void OnSaveSegmentSequence(wxCommandEvent &event); // Removed
  // void OnImportSegmentSequenceEsps(wxCommandEvent &event); // Removed
  // void OnLoadGesturalScore(wxCommandEvent &event); // Removed
  // void OnSaveGesturalScore(wxCommandEvent &event); // Removed
  void OnLoadSpeaker(wxCommandEvent &event);
  void OnSaveSpeaker(wxCommandEvent &event);
  void OnExit(wxCommandEvent &event);

  void OnSetAudioZero(wxCommandEvent &event);
  void OnNormalizeAmplitude(wxCommandEvent &event);
  void OnScaleAmplitudeUp(wxCommandEvent &event);
  void OnScaleAmplitudeDown(wxCommandEvent &event);


  void OnReduceConsonantVariability(wxCommandEvent &event);

  // void OnChangeGesturalScoreF0Offset(wxCommandEvent &event); // Removed
  // void OnChangeGesturalScoreF0Range(wxCommandEvent &event); // Removed
  // void OnChangeGesturalScoreF0TargetSlopes(wxCommandEvent &event); // Removed
  // void OnSubstituteGesturalScoreGlottalShapes(wxCommandEvent &event); // Removed
  // void OnChangeGesturalScoreSubglottalPressure(wxCommandEvent &event); // Removed
  // void OnGetGesturalScoreF0Statistics(wxCommandEvent &event); // Removed
  // void OnChangeGesturalScoreDuration(wxCommandEvent &event); // Removed
  // void OnChangeGesturalScoreTimeConstants(wxCommandEvent &event); // Removed
  
  void OnExportAreaFunction(wxCommandEvent &event);
  void OnExportCrossSections(wxCommandEvent &event);
  void OnExportVocalTractToSTL(wxCommandEvent& event);													  
  void OnExportModelSvg(wxCommandEvent &event);
  void OnExportWireframeModelSvg(wxCommandEvent &event);
  void OnExportModelObj(wxCommandEvent &event);
  void OnExportContourSvg(wxCommandEvent &event);
  void OnExportPrimarySpectrum(wxCommandEvent &event);
  void OnExportSecondarySpectrum(wxCommandEvent &event);
  // void OnExportEmaTrajectories(wxCommandEvent &event); // Removed
  // void OnExportVocalTractVideoFrames(wxCommandEvent &event); // Removed
  // void OnExportTransferFunctionsFromScore(wxCommandEvent &event); // Removed
  // void OnExportCrossSectionsFromScore(wxCommandEvent& event); // Removed

  // void OnShowVocalTractDialog(wxCommandEvent &event); // Removed as per user request
  void OnShowVocalTractShapes(wxCommandEvent &event);
  void OnShowPhoneticParameters(wxCommandEvent &event);
  // void OnShowAnatomyParameters(wxCommandEvent &event); // Removed
  void OnShowFdsOptionsDialog(wxCommandEvent &event);
  void OnShowTdsOptionsDialog(wxCommandEvent &event);
  void OnShowVocalFoldDialog(wxCommandEvent &event);
  void OnShowLfModelDialog(wxCommandEvent &event);

  void OnGesturalScoreFileToAudio(wxCommandEvent &event);
  void OnTubeSequenceFileToAudio(wxCommandEvent &event);
  void OnTractSequenceFileToAudio(wxCommandEvent &event);
  // void OnGesturalScoreToTubeSequenceFile(wxCommandEvent &event); // Removed
  // void OnGesturalScoreToTractSequenceFile(wxCommandEvent &event); // Removed
  
  void OnHertzToSemitones(wxCommandEvent &event);

  void OnAbout(wxCommandEvent &event);

  // Toolbar functions
  void OnRecord(wxCommandEvent &event);
  void OnPlayAll(wxCommandEvent &event);
  void OnPlayPart(wxCommandEvent &event);
  void OnClearItems(wxCommandEvent &event);

  // Individual clear functions
  void OnClearAll(wxCommandEvent &event);
  void OnClearMainTrack(wxCommandEvent &event);
  void OnClearEggTrack(wxCommandEvent &event);
  void OnClearExtraTrack(wxCommandEvent &event);
  void OnClearAudioTracks(wxCommandEvent &event);
  // void OnClearGesturalScore(wxCommandEvent &event); // Removed
  // void OnClearSegmentSequence(wxCommandEvent &event); // Removed
  void OnClearAnalysisTracks(wxCommandEvent &event);
  
  // Special shortcuts
  // void OnKeyCtrlLeft(wxCommandEvent &event); // Removed
  // void OnKeyCtrlRight(wxCommandEvent &event); // Removed
  // void OnKeyShiftLeft(wxCommandEvent &event); // Removed
  // void OnKeyShiftRight(wxCommandEvent &event); // Removed
  // void OnKeyCtrlLess(wxCommandEvent &event); // Removed
  // void OnKeyCtrlDelete(wxCommandEvent &event); // Removed
  // void OnKeyCtrlInsert(wxCommandEvent &event); // Removed
  // void OnKeyF6(wxCommandEvent& event); // Removed
  // void OnKeyF7(wxCommandEvent &event); // Removed
  // void OnKeyF8(wxCommandEvent &event); // Removed
  // void OnKeyF9(wxCommandEvent &event); // Removed
  // void OnKeyF11(wxCommandEvent &event); // Removed
  // void OnKeyF12(wxCommandEvent &event); // Removed

  // ****************************************************************************
  // Declare the event table right at the end
  // ****************************************************************************

  DECLARE_EVENT_TABLE()
};

#endif
