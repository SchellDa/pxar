#include <iostream>
#include <bitset>
#include <stdlib.h>
#include <algorithm>

#include <TStopwatch.h>
#include <TMarker.h>
#include <TStyle.h>
#include <TBox.h>

#include "PixTestTiming.hh"
#include "PixUtil.hh"
#include "timer.h"
#include "log.h"
#include "helper.h"
#include "constants.h"
#include "math.h"

using namespace std;
using namespace pxar;

ClassImp(PixTestTiming)

//------------------------------------------------------------------------------
PixTestTiming::PixTestTiming(PixSetup *a, std::string name) : PixTest(a, name)
{
  PixTest::init();
  init();
}

//------------------------------------------------------------------------------
PixTestTiming::PixTestTiming() : PixTest() {}

bool PixTestTiming::setParameter(string parName, string sval) {
  bool found(false);
  std::transform(parName.begin(), parName.end(), parName.begin(), ::tolower);
  for (unsigned int i = 0; i < fParameters.size(); ++i) {
    if (fParameters[i].first == parName) {
      found = true;
      if (!parName.compare("fastscan")) {
        PixUtil::replaceAll(sval, "checkbox(", "");
        PixUtil::replaceAll(sval, ")", "");
        fFastScan = atoi(sval.c_str());
        LOG(logDEBUG) << "fFastScan: " << fFastScan;
        setToolTips();
      }
      if (!parName.compare("targetclk")) {
        fTargetClk = atoi(sval.c_str());
        LOG(logDEBUG) << "PixTestTiming::PixTest() targetclk = " << fTargetClk;
      }
      if (!parName.compare("ntrig")) {
        fNTrig = atoi(sval.c_str());
        LOG(logDEBUG) << "PixTestTiming::PixTest() ntrig = " << fNTrig;
      }
      break;
    }
  }
  return found;
}

void PixTestTiming::init()
{
  LOG(logDEBUG) << "PixTestTiming::init()";

  fDirectory = gFile->GetDirectory(fName.c_str());
  if (!fDirectory)
    fDirectory = gFile->mkdir(fName.c_str());
  fDirectory->cd();
}

void PixTestTiming::setToolTips()
{
  fTestTip = string(Form("scan testboard parameter settings and check for valid readout\n")
                    + string("TO BE IMPLEMENTED!!"));  //FIXME
  fSummaryTip = string("summary plot to be implemented");  //FIXME
}

void PixTestTiming::bookHist(string name)
{
  fDirectory->cd();
  LOG(logDEBUG) << "nothing done with " << name;
}

PixTestTiming::~PixTestTiming()
{
  LOG(logDEBUG) << "PixTestTiming dtor";
  std::list<TH1*>::iterator il;
  fDirectory->cd();
  for (il = fHistList.begin(); il != fHistList.end(); ++il)
    {
      LOG(logINFO) << "Write out " << (*il)->GetName();
      (*il)->SetDirectory(fDirectory);
      (*il)->Write();
    }
}

// ----------------------------------------------------------------------
void PixTestTiming::doTest() {

  fDirectory->cd();
  PixTest::update();
  bigBanner(Form("PixTestTiming::doTest()"));

  ClkSdaScan();
  TH1 *h1 = (*fDisplayedHist);
  h1->Draw(getHistOption(h1).c_str());
  PixTest::update();

  PhaseScan();
  h1 = (*fDisplayedHist);
  h1->Draw(getHistOption(h1).c_str());
  PixTest::update();

  // -- save DACs!
  saveTbParameters();
  LOG(logINFO) << "PixTestTiming::doTest() done";
}

//------------------------------------------------------------------------------
void PixTestTiming::ClkSdaScan() {
  cacheDacs();
  fDirectory->cd();
  PixTest::update();
  banner(Form("PixTestTiming::ClkSdaScan()"));

  //Make a histogram
  TH2D *h1(0);
  h1 = bookTH2D("ClkSdaScan","ClkSdaScan", 20, -0.5, 19.5, 20, -0.5, 19.5);
  h1->SetDirectory(fDirectory);
  setTitles(h1, "Clk", "Sda");
  fHistOptions.insert(make_pair(h1, "colz"));

  //Turn of Vana
  fApi->setDAC("vana", 0);
  pxar::mDelay(2000);
  double IA = fApi->getTBia();

  // Start test timer
  timer t;

  //Scan the Clock and SDA to find the working values. iclk starts at fTargetClk and ends at fTargetClk-1. Both sda and clk ranges are limited to 0-19.
  int GoodSDA = -1;
  int GoodClk = -1;
  map<int, vector<int> > goodclksdalist;
  for (int i = 0; i < 20; i++) {
    int iclk = (i+fTargetClk) % 20;
    vector<int> goodsdalist;
    for (int j = 0; j < 20; j++) {
      int isda = (j+iclk+15) % 20;
      fApi->setTestboardDelays(getDelays(iclk,isda));
      LOG(logDEBUG) << "Checking Clk: " << iclk << " Sda: " << isda;
      fApi->setDAC("vana", 70);
      pxar::mDelay(10);
      double NewIA = fApi->getTBia();
      if (fabs(NewIA-IA) > 0.005) {
        h1->Fill(iclk,isda); //Need 5mA change in current to see if ROCs are programmable
        GoodClk=iclk;
        GoodSDA=isda;
        goodsdalist.push_back(isda);
        if (fFastScan) break;
      }
      fApi->setDAC("vana", 0);
      pxar::mDelay(10);
    }
    goodclksdalist[iclk]=goodsdalist;
    if (fFastScan && GoodClk != -1) break;
  }

  //Overly complicated algorithm to figure out the best SDA.
  //Normally there are 7 sda settings that work, and this selects the middle one.
  //It's completcated because the working SDA settings can be 0, 1, 2, 3, 4, 18, and 19. The center most value is 1.
  if (GoodClk != -1) {
    if (!fFastScan) {
      GoodClk = -1;
      for (int i = 0; i < 20; i++) {
        int iclk = (i+fTargetClk) % 20;
        if (goodclksdalist.count(iclk)) {
          GoodClk = iclk;
          vector<int> goodsdalist = goodclksdalist[iclk];
          if (goodsdalist.size() == 1) {
            GoodSDA=goodsdalist[0];
          } else {
            sort(goodsdalist.begin(),goodsdalist.end());
            for (size_t isda=1; isda<goodsdalist.size(); isda++) if (fabs(goodsdalist[isda]-goodsdalist[isda-1])>1) goodsdalist[isda] -= 20;
            sort(goodsdalist.begin(),goodsdalist.end());
            GoodSDA=goodsdalist[round(goodsdalist.size()/2)];
            if (GoodSDA<0) GoodSDA+=20;
            break;
          }
        }
      }
    }

    //Print out good values and set them on the test board.
    vector<pair<string,uint8_t> > GoodDelays = getDelays(GoodClk,GoodSDA);
    LOG(logINFO) << "SDA delay found at clock:" << GoodClk;
    for(vector<pair<string,uint8_t> >::iterator idelay = GoodDelays.begin(); idelay != GoodDelays.end(); ++idelay) {
      LOG(logINFO) << idelay->first << ":    " << int(idelay->second);
      fPixSetup->getConfigParameters()->setTbParameter(idelay->first, idelay->second);
    }
    fApi->setTestboardDelays(GoodDelays);
  } else LOG(logERROR) << "No working SDA setting found!";

  //Draw the plot
  h1->Draw("colz");
  fHistList.push_back(h1);
  fDisplayedHist = find(fHistList.begin(), fHistList.end(), h1);
  PixTest::update();
  restoreDacs();

  // Print timer value:
  LOG(logINFO) << "Test took " << t << " ms.";
  LOG(logINFO) << "PixTestTiming::ClkSdaScan() done.";
}

//------------------------------------------------------------------------------
void PixTestTiming::PhaseScan() {

  banner(Form("PixTestTiming::PhaseScan()"));
  fDirectory->cd();
  PixTest::update();

  // Start test timer
  timer t;

  //Make histograms
  TH2D *h1(0);
  TH2D *h2(0);

  //Ignore the first 3 triggers
  fTrigBuffer = 3;

  // Setup a new pattern with only res and token:
  vector<pair<string, uint8_t> > pg_setup;
  //pg_setup.push_back(make_pair("resetroc", 25));
  pg_setup.push_back(make_pair("resettbm", 25));
  pg_setup.push_back(make_pair("trigger", 0));
  fApi->setPatternGenerator(pg_setup);
  uint16_t period = 300;
  vector<rawEvent> daqRawEv;

  //Get the number of TBMs, Total ROCs, and ROCs per TBM
  int nTBMs = fApi->_dut->getNTbms();
  vector<uint8_t> rocIds = fApi->_dut->getEnabledRocIDs();
  vector<int> nROCs;
  vector<TH2D*> phasehists;
  for (int itbm = 0; itbm < nTBMs; itbm++) {
    fApi->setTbmReg("basea", 0, itbm); //Reset the ROC delays
    nROCs.push_back(0);
    h1 = bookTH2D(Form("TBMPhaseScan%d",itbm),Form("TBM %d Phase Scan",itbm), 8, -0.5, 7.5, 8, -0.5, 7.5);
    h1->SetDirectory(fDirectory);
    setTitles(h1, "160MHz Phase", "400 MHz Phase");
    fHistOptions.insert(make_pair(h1, "colz"));
    h1->SetMinimum(0);
    phasehists.push_back(h1);
  }
  //Count up the ROCs on each TBM Core
  for (size_t iROC=0; iROC<rocIds.size(); iROC++) {
    if (rocIds[iROC]<8) nROCs[0]++; //8 is a magic number! Number of expected ROCs per TBM should be in the dut or ConfigParameters!
    else nROCs[1]++;
  }

  // Loop through all possible TBM phase settings.
  map <int, vector<int> > TBMROCPhaseMap;
  for (int iclk160 = 0; iclk160 < 8 && !(int(TBMROCPhaseMap.size())>0 && fFastScan); iclk160++) {
    for (int iclk400 = 0; iclk400 < 8 && !(int(TBMROCPhaseMap.size())>0 && fFastScan); iclk400++) {
      uint8_t delaysetting = iclk160<<5 | iclk400<<2;
      fApi->setTbmReg("basee", delaysetting, 0); //Set TBM 160-400 MHz Clock Phase
      LOG(logINFO) << "160MHz Phase: " << iclk160 << " 400MHz Phase: " << iclk400 << " Delay Setting: " << bitset<8>(delaysetting).to_string();
      for (int itbm = 0; itbm < nTBMs; itbm++) fApi->setTbmReg("basea", 0, itbm); //Reset the ROC delays
      //Loop through each TBM core and count the number of ROC headers on the core for all 256 delay settings
      vector<int> GoodROCDelays;
      for (int itbm = 0; itbm < nTBMs; itbm++) {
        int MaxGoodROCSize=0;
        bool goodROCDelay = false;
        LOG(logDEBUG) << "Looking at TBM Core: " << itbm;
        for (int delaytht = 0; delaytht < 4 && !goodROCDelay; delaytht++) {
          h2 = bookTH2D(Form("ROCPhaseScan_clk160_%d_clk400_%d_TBM_%d_delay_%d", iclk160, iclk400, itbm, delaytht),
                        Form("ROC Phase Scan: TBM %d Phase: %s THT Delay: %s", itbm, bitset<8>(delaysetting).to_string().c_str(), bitset<2>(delaytht).to_string().c_str()), 8, -0.5, 7.5, 8, -0.5, 7.5);
          setTitles(h2, "ROC Port 0", "ROC Port 1");
          h2->SetDirectory(fDirectory);
          h2->SetMinimum(0);
          h2->SetMaximum(nROCs[itbm]);
          fHistOptions.insert(make_pair(h2, "colz"));
          fApi->daqStart();
          for (int idelay = 0; idelay < 64 && !goodROCDelay; idelay++) {
            int ROCDelay = (delaytht << 6) | idelay;
            LOG(logDEBUG) << "Testing ROC Delay: " << bitset<8>(ROCDelay).to_string() << " For TBM Core: " << itbm;
            fApi->setTbmReg("basea", ROCDelay, itbm);
            fApi->daqTrigger(fNTrig+fTrigBuffer,period);
            daqRawEv = fApi->daqGetRawEventBuffer();
            LOG(logDEBUG) << "Events in Data Buffer: " << daqRawEv.size();
            if (int(daqRawEv.size()) < fNTrig+fTrigBuffer) continue; //Grab fNTrig triggers + a small (3) buffer
            int gooddecodings=0;
            for (int ievent = fTrigBuffer; ievent<fNTrig+fTrigBuffer; ievent++) { //Ignore the first fTrigBugg (3) triggers
              rawEvent event = daqRawEv.at(ievent);
              LOG(logDEBUG) << "Event: " << event;
              vector<int> tbmheaders;
              vector<int> tbmtrailers;
              int header=0;
              for (int idata = 0; idata < int(event.data.size()); idata++) {
                if (event.data.at(idata) >> 8  == 160) tbmheaders.push_back(idata); //Look for TBM Header a0
                if (event.data.at(idata) == 49152) tbmtrailers.push_back(idata); //Look for TBM Trailer c000
                if (header==0 && int(tbmheaders.size())==itbm+1 && int(tbmtrailers.size())==itbm && event.data.at(idata) >> 12 == 4) header = event.data.at(idata); //Grab the first object that looks like a header between the correct header and trailer
              }
              if (int(tbmheaders.size()) != nTBMs || int(tbmtrailers.size()) != nTBMs || header==0) continue; //Skip event if the correct number of TBM headers and trailer are not present or if a ROC header could not be found.
              int rocheader_count = 0;
              for (int idata = tbmheaders[itbm]; idata < tbmtrailers[itbm]; idata++) if (event.data.at(idata) >> 2  == header >> 2) rocheader_count++; //Count the number of ROCs on TBM Core itbm
              LOG(logDEBUG) << rocheader_count << " ROC headers (" << hex << (header) << dec << ") found for TBM Core " << itbm << ". Looking for " << nROCs[itbm] << ".";
              h2->Fill(ROCDelay & 7, (ROCDelay & 56)>>3, rocheader_count);
              if (rocheader_count==nROCs[itbm]) gooddecodings++;
            }
            if (gooddecodings==fNTrig && fFastScan) {
              goodROCDelay = true;
              fPixSetup->getConfigParameters()->setTbmDac("basea", ROCDelay, itbm);
            }
          }
          pair <int, int> GoodRegion = getGoodRegion(h2, nROCs[itbm]*fNTrig);
          LOG(logDEBUG) << h2->GetTitle() << " - Size: " << GoodRegion.first << " Delay: " << GoodRegion.second;
          h2->Scale(1/float(fNTrig));
          if (GoodRegion.first > MaxGoodROCSize) {
            MaxGoodROCSize = GoodRegion.first;
            int ROCDelay = (delaytht << 6) | GoodRegion.second;
            if (int(GoodROCDelays.size()) < itbm+1) GoodROCDelays.push_back(ROCDelay);
            else GoodROCDelays[itbm] = ROCDelay;
          }
          //Draw the plot
          if (GoodRegion.first > 0 || (fFastScan && h2->GetEntries()>0)) {
            h2->Draw(getHistOption(h2).c_str());
            fHistList.push_back(h2);
            fDisplayedHist = find(fHistList.begin(), fHistList.end(), h2);
            PixTest::update();
          }
          fApi->daqStop();
        }
        phasehists[itbm]->Fill(iclk160, iclk400, MaxGoodROCSize);
        if (int(GoodROCDelays.size())==itbm+1) { // Use the good ROC delays, or reset the ROCs to 0
          fApi->setTbmReg("basea", GoodROCDelays[itbm], itbm);
          fPixSetup->getConfigParameters()->setTbmDac("basea", GoodROCDelays[itbm], itbm);
        } else fApi->setTbmReg("basea", 0, itbm);
      }
      if (int(GoodROCDelays.size())==nTBMs){
        fPixSetup->getConfigParameters()->setTbmDac("basee", delaysetting, 0);
        TBMROCPhaseMap[delaysetting] = GoodROCDelays;
        LOG(logINFO) << "Good Timings Found!";
        LOG(logINFO) << "TBMPhase basee: " << bitset<8>(delaysetting).to_string();
        for (int itbm = 0; itbm < nTBMs; itbm++) LOG(logINFO) << "ROCPhase TBM Core " << itbm << " basea: " << bitset<8>(GoodROCDelays[itbm]).to_string();
      }
    }
  }

  // Reset the pattern generator to the configured default:
  fApi->setPatternGenerator(fPixSetup->getConfigParameters()->getTbPgSettings());

  if (int(TBMROCPhaseMap.size())<1) {
    LOG(logERROR) << "No working TBM-ROC Phase found. Verify you have disabled bypassed ROCs in pXar on the h/w tab.";
    for (int itbm = 0; itbm < nTBMs; itbm++) LOG(logERROR) << "PhaseScan searched for " << nROCs[itbm] << " ROCs on TBM Core " << itbm << ".";
  }

  //Draw TBM Phase Map
  for (int itbm = 0; itbm < nTBMs && !fFastScan; itbm++) {
    phasehists[itbm]->Draw(getHistOption(phasehists[itbm]).c_str());
    fHistList.push_back(phasehists[itbm]);
    fDisplayedHist = find(fHistList.begin(), fHistList.end(), phasehists[itbm]);
    PixTest::update();
  }

  // Print timer value:
  LOG(logINFO) << "Test took " << t << " ms.";
  LOG(logINFO) << "PixTestTiming::PhaseScan() done.";
}

//------------------------------------------------------------------------------
void PixTestTiming::TimingTest() {

  banner(Form("PixTestTiming::TimingTest()"));

  size_t nTBMs = fApi->_dut->getNTbms();
  size_t nROCs = fApi->_dut->getEnabledRocIDs().size();

  // Setup a new pattern with only res and token:
  vector<pair<string, uint8_t> > pg_setup;
  pg_setup.push_back(make_pair("resetroc", 25));
  pg_setup.push_back(make_pair("trigger", 0));
  fApi->setPatternGenerator(pg_setup);
  vector<rawEvent> daqRawEv;
  fApi->daqStart();
  fApi->daqTrigger(fNTrig,300);
  daqRawEv = fApi->daqGetRawEventBuffer();
  fApi->daqStop();
  LOG(logINFO) << daqRawEv.size() << " events found. " << fNTrig << " events expected.";
  int ngoodevents = 0;
  for (size_t ievent=0; ievent<daqRawEv.size(); ievent++) {
    banner(Form("Decoding Event Number %d", int(ievent)));
    rawEvent event = daqRawEv.at(ievent);
    LOG(logDEBUG) << "Event: " << event;
    vector<int> tbmheaders;
    vector<int> tbmtrailers;
    vector<int> rocheaders;
    for (int idata=0; idata < int(event.data.size()); idata++) {
      if (event.data.at(idata) >> 8  == 160) tbmheaders.push_back(idata);
      if (event.data.at(idata) == 49152) tbmtrailers.push_back(idata);
      if (event.data.at(idata) >> 12 == 4) rocheaders.push_back(idata);
    }
    LOG(logDEBUG) << tbmheaders.size() << " TBM Headers found. " << nTBMs << " TBM Headers expected.";
    LOG(logDEBUG) << tbmtrailers.size() << " TBM Trailers found. " << nTBMs << " TBM Trailers expected.";
    LOG(logDEBUG) << rocheaders.size() << " ROC Headers found. " << nROCs << " ROC Headers expected.";
    if (tbmheaders.size()==nTBMs && tbmtrailers.size()==nTBMs && rocheaders.size()==nROCs) ngoodevents++;
  }
  fApi->setPatternGenerator(fPixSetup->getConfigParameters()->getTbPgSettings());
  LOG(logINFO) << Form("The fraction of properly decoded events is %4.2f: ", float(ngoodevents)/fNTrig*100) << ngoodevents << "/" << fNTrig;
  LOG(logINFO) << "PixTestTiming::TimingTest() done.";

}

//------------------------------------------------------------------------------
vector<pair<string,uint8_t> > PixTestTiming::getDelays(uint8_t clk, uint8_t sda) {
  vector<pair<string,uint8_t> > sigdelays;
  sigdelays.push_back(make_pair("clk", clk%20));
  sigdelays.push_back(make_pair("ctr", clk%20));
  sigdelays.push_back(make_pair("sda", sda%20));
  sigdelays.push_back(make_pair("tin", (clk+5)%20));
  return sigdelays;
}

// ----------------------------------------------------------------------
pair <int, int> PixTestTiming::getGoodRegion(TH2D* hist, int hits) {

  if (hist->GetEntries()==0) return make_pair(0,0);

  int MaxGoodRegionSize=0;
  int GoodROCDelay=0;
  for (int startbinx=1; startbinx<=hist->GetNbinsX(); startbinx++) {
    for (int startbiny=1; startbiny<=hist->GetNbinsY(); startbiny++) {
      if (int(hist->GetBinContent(startbinx,startbiny))!=hits) continue;
      for (int regionsize=0; regionsize<8; regionsize++) {
        bool regiongood = true;
        for (int xoffset=0; xoffset<=regionsize && regiongood; xoffset++) {
          for (int yoffset=0; yoffset<=regionsize && regiongood; yoffset++) {
            int checkbinx = (startbinx+xoffset>8) ? startbinx+xoffset-8 : startbinx+xoffset;
            int checkbiny = (startbiny+yoffset>8) ? startbiny+yoffset-8 : startbiny+yoffset;
            if (int(hist->GetBinContent(checkbinx,checkbiny))!=hits) regiongood=false;
          }
        }
        if (regiongood && regionsize+1>MaxGoodRegionSize) {
          MaxGoodRegionSize=regionsize+1;
          GoodROCDelay = (startbinx-1+regionsize/2)%8 | (startbiny-1+regionsize/2)%8<<3;
        } else break;
      }
    }
  }

  return make_pair(MaxGoodRegionSize, GoodROCDelay);

}

// ----------------------------------------------------------------------
void PixTestTiming::saveTbParameters() {
  LOG(logINFO) << "PixTestTiming:: Write Tb parameters to file.";
  fPixSetup->getConfigParameters()->writeTbParameterFile();
}

// ----------------------------------------------------------------------
void PixTestTiming::runCommand(string command) {
  transform(command.begin(), command.end(), command.begin(), ::tolower);
  LOG(logDEBUG) << "running command: " << command;
  if (!command.compare("clocksdascan")) {
    ClkSdaScan();
    return;
  }
  if (!command.compare("phasescan")) {
    PhaseScan();
    return;
  }
  if (!command.compare("savetbparameters")) {
    saveTbParameters();
    return;
  }
  if (!command.compare("timingtest")) {
    TimingTest();
    return;
  }
  LOG(logDEBUG) << "did not find command ->" << command << "<-";
}
