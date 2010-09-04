/*
  Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009

	M Roberts (original release)
	Robin Birch <robinb@ruffnready.co.uk>
	Samuel Gisiger <samuel.gisiger@triadis.ch>
	Jeff Goodenough <jeff@enborne.f2s.com>
	Alastair Harrison <aharrison@magic.force9.co.uk>
	Scott Penrose <scottp@dd.com.au>
	John Wharington <jwharington@gmail.com>
	Lars H <lars_hn@hotmail.com>
	Rob Dunning <rob@raspberryridgesheepfarm.com>
	Russell King <rmk@arm.linux.org.uk>
	Paolo Ventafridda <coolwind@email.it>
	Tobias Lohner <tobias@lohner-net.de>
	Mirek Jezek <mjezek@ipplc.cz>
	Max Kellermann <max@duempel.org>
	Tobias Bieniek <tobias.bieniek@gmx.de>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Logger/LoggerImpl.hpp"
#include "Version.hpp"
#include "Profile.hpp"
#include "LogFile.hpp"
#include "Asset.hpp"
#include "UtilsSystem.hpp"
#include "UtilsFile.hpp"
#include "LocalPath.hpp"
#include "Device/Declaration.hpp"
#include "Compatibility/path.h"
#include "Compatibility/string.h"
#include "Compatibility/dirent.h"
#include "SettingsComputer.hpp"
#include "NMEA/Info.hpp"
#include "Simulator.hpp"
#include "Interface.hpp"
#include "OS/FileUtil.hpp"

#ifdef HAVE_POSIX
#include <unistd.h>
#endif
#include <time.h>
#include <sys/types.h>
#include <tchar.h>
#include <stdio.h>
#include <algorithm>

const struct LoggerImpl::LoggerPreTakeoffBuffer &
LoggerImpl::LoggerPreTakeoffBuffer::operator=(const NMEA_INFO &src)
{
  Location = src.Location;
  Altitude = src.GPSAltitude;
  BaroAltitude = src.GetAltitudeBaroPreferred();

  DateTime = src.DateTime;
  Time = src.Time;

  NAVWarning = src.gps.NAVWarning;
  FixQuality = src.gps.FixQuality;
  SatellitesUsed = src.gps.SatellitesUsed;
  HDOP = src.gps.HDOP;
  Simulator = src.gps.Simulator;

  std::copy(src.gps.SatelliteIDs, src.gps.SatelliteIDs + MAXSATELLITES,
            SatelliteIDs);

  return *this;
}

LoggerImpl::LoggerImpl():
  LoggerActive(false),
  Simulator(false)
{
  frecord.reset();
  szLoggerFileName[0] = 0;
}

bool
LoggerImpl::isLoggerActive() const
{
  return LoggerActive;
}

static TCHAR
NumToIGCChar(int n)
{
  if (n < 10)
    return _T('1') + (n - 1);

  return _T('A') + (n - 10);
}

static int
IGCCharToNum(TCHAR c)
{
  if ((c >= _T('1')) && (c <= _T('9')))
    return c - _T('1') + 1;

  if ((c >= _T('A')) && (c <= _T('Z')))
    return c - _T('A') + 10;

  return 0; // Error!
}

/*
 HFDTE141203  <- should be UTC, same as time in filename
 HFFXA100
 HFPLTPILOT:JOHN WHARINGTON
 HFGTYGLIDERTYPE:LS 3
 HFGIDGLIDERID:VH-WUE
 HFDTM100GPSDATUM:WGS84
 HFRFWFIRMWAREVERSION:3.6
 HFRHWHARDWAREVERSION:3.4
 HFFTYFR TYPE:GARRECHT INGENIEURGESELLSCHAFT,VOLKSLOGGER 1.0
 HFCIDCOMPETITIONID:WUE
 HFCCLCOMPETITIONCLASS:FAI
 HFCIDCOMPETITIONID:WUE
 HFCCLCOMPETITIONCLASS:15M
*/

/**
 * Stops the logger
 * @param gps_info NMEA_INFO struct holding the current date
 */
void
LoggerImpl::StopLogger(const NMEA_INFO &gps_info)
{
  // Logger can't be switched off if already off -> cancel
  if (!LoggerActive)
    return;

  // Logger off
  LoggerActive = false;

  if (gps_info.gps.Simulator)
    Simulator = true;

  // Make space for logger file, if unsuccessful -> cancel
  if (!LoggerClearFreeSpace(gps_info))
    return;

  // Write IGC File
  DiskBufferFlush();

  // Write GRecord
  if (!Simulator)
    LoggerGStop(szLoggerFileName);

  PreTakeoffBuffer.clear();
}

void
LoggerImpl::LogPointToBuffer(const NMEA_INFO &gps_info)
{
  LoggerPreTakeoffBuffer item;
  item = gps_info;
  PreTakeoffBuffer.push(item);
}

const LoggerImpl::LogPoint_GPSPosition &
LoggerImpl::LogPoint_GPSPosition::operator=(const NMEA_INFO &gps_info)
{
  DegLat = (int)gps_info.Location.Latitude.value_degrees();
  MinLat = gps_info.Location.Latitude.value_degrees() - fixed(DegLat);
  NoS = 'N';
  if (negative(MinLat) || ((int)MinLat == DegLat && DegLat < 0)) {
    NoS = 'S';
    DegLat *= -1;
    MinLat *= -1;
  }
  MinLat *= 60;
  MinLat *= 1000;

  DegLon = (int)gps_info.Location.Longitude.value_degrees();
  MinLon = gps_info.Location.Longitude.value_degrees() - fixed(DegLon);
  EoW = 'E';
  if (negative(MinLon) || ((int)MinLon == DegLon && DegLon < 0)) {
    EoW = 'W';
    DegLon *= -1;
    MinLon *= -1;
  }
  MinLon *= 60;
  MinLon *= 1000;
  GPSAltitude = (int)gps_info.GPSAltitude;
  Initialized = true;

  return *this;
}

void
LoggerImpl::LogPointToFile(const NMEA_INFO& gps_info)
{
  char szBRecord[500];
  int iSIU = GetSIU(gps_info);
  fixed dEPE = GetEPE(gps_info);
  LogPoint_GPSPosition p;

  char IsValidFix;

  // if at least one GPS fix comes from the simulator, disable signing
  if (gps_info.gps.Simulator)
    Simulator = true;

  if (!Simulator) {
    const char *p = frecord.update(gps_info.gps.SatelliteIDs,
                                   gps_info.DateTime, gps_info.Time,
                                   gps_info.gps.NAVWarning);
    if (p != NULL)
      IGCWriteRecord(p);
  }

  if (!LastValidPoint.Initialized &&
      ((gps_info.GPSAltitude < fixed(-100))
       || (gps_info.BaroAltitude < fixed(-100))
          || gps_info.gps.NAVWarning))
    return;


  if (gps_info.gps.NAVWarning) {
    IsValidFix = 'V'; // invalid
    p = LastValidPoint;
  } else {
    IsValidFix = 'A'; // Active
    // save last active fix location
    p = LastValidPoint = gps_info;
  }

  sprintf(szBRecord,"B%02d%02d%02d%02d%05.0f%c%03d%05.0f%c%c%05d%05d%03d%02d\r\n",
          gps_info.DateTime.hour, gps_info.DateTime.minute,
          gps_info.DateTime.second,
          p.DegLat, (double)p.MinLat, p.NoS,
          p.DegLon, (double)p.MinLon, p.EoW, IsValidFix,
          (int)gps_info.BaroAltitude, p.GPSAltitude, (int)dEPE, iSIU);

  IGCWriteRecord(szBRecord);
}

void
LoggerImpl::LogEvent(const NMEA_INFO& gps_info, const char* event)
{
  char szBRecord[30];
  sprintf(szBRecord,"E%02d%02d%02d%s\r\n",
          gps_info.DateTime.hour, gps_info.DateTime.minute,
          gps_info.DateTime.second, event);

  IGCWriteRecord(szBRecord);
  // tech_spec_gnss.pdf says we need a B record immediately after an E record
  LogPointToFile(gps_info);
}

void
LoggerImpl::LogPoint(const NMEA_INFO& gps_info)
{
  if (!LoggerActive) {
    LogPointToBuffer(gps_info);
    return;
  }

  while (!PreTakeoffBuffer.empty()) {
    const struct LoggerPreTakeoffBuffer &src = PreTakeoffBuffer.shift();
    NMEA_INFO tmp_info;
    tmp_info.Location = src.Location;
    tmp_info.GPSAltitude = src.Altitude;
    tmp_info.BaroAltitude = src.BaroAltitude;
    tmp_info.DateTime = src.DateTime;
    tmp_info.Time = src.Time;

    tmp_info.gps.NAVWarning = src.NAVWarning;
    tmp_info.gps.FixQuality = src.FixQuality;
    tmp_info.gps.SatellitesUsed = src.SatellitesUsed;
    tmp_info.gps.HDOP = src.HDOP;
    tmp_info.gps.Simulator = src.Simulator;

    for (int iSat = 0; iSat < MAXSATELLITES; iSat++)
      tmp_info.gps.SatelliteIDs[iSat] = src.SatelliteIDs[iSat];

    LogPointToFile(tmp_info);
  }

  LogPointToFile(gps_info);
}

static bool
IsAlphaNum (TCHAR c)
{
  if (((c >= _T('A')) && (c <= _T('Z')))
      || ((c >= _T('a')) && (c <= _T('z')))
      || ((c >= _T('0')) && (c <= _T('9'))))
    return true;

  return false;
}

void
LoggerImpl::StartLogger(const NMEA_INFO &gps_info,
    const SETTINGS_COMPUTER &settings, const TCHAR *astrAssetNumber)
{
  int i;
  TCHAR path[MAX_PATH];
  Simulator = false;
  LastValidPoint.Initialized = false;

  // chars must be legal in file names
  for (i = 0; i < 3; i++)
    strAssetNumber[i] = IsAlphaNum(strAssetNumber[i]) ?
                        strAssetNumber[i] : _T('A');

  LocalPath(path, _T("logs"));

  DiskBufferReset();

  if (gps_info.gps.Simulator)
    Simulator = true;

  if (!Simulator)
    LoggerGInit();

  frecord.reset();

  for (i = 1; i < 99; i++) {
    // 2003-12-31-XXX-987-01.IGC
    // long filename form of IGC file.
    // XXX represents manufacturer code

    if (!settings.LoggerShortName) {
      // Long file name
      _stprintf(szLoggerFileName,
                _T("%s" DIR_SEPARATOR_S "%04u-%02u-%02u-XCS-%c%c%c-%02d.IGC"),
          path,
                gps_info.DateTime.year,
                gps_info.DateTime.month,
                gps_info.DateTime.day,
          strAssetNumber[0],
          strAssetNumber[1],
          strAssetNumber[2],
          i);
    } else {
      // Short file name
      TCHAR cyear, cmonth, cday, cflight;
      cyear = NumToIGCChar((int)gps_info.DateTime.year % 10);
      cmonth = NumToIGCChar(gps_info.DateTime.month);
      cday = NumToIGCChar(gps_info.DateTime.day);
      cflight = NumToIGCChar(i);
      _stprintf(szLoggerFileName,
                _T("%s" DIR_SEPARATOR_S "%c%c%cX%c%c%c%c.IGC"),
          path,
          cyear,
          cmonth,
          cday,
          strAssetNumber[0],
          strAssetNumber[1],
          strAssetNumber[2],
          cflight);

    }

    if (!File::Exists(szLoggerFileName))
      break;  // file not exist, we'll use this name
  }

  LogStartUp(_T("Logger Started: %s"), szLoggerFileName);
}

void
LoggerImpl::LoggerHeader(const NMEA_INFO &gps_info, const Declaration &decl)
{
  char datum[] = "HFDTM100Datum: WGS-84\r\n";
  char temp[100];

  // Flight recorder ID number MUST go first..
  sprintf(temp, "AXCS%C%C%C\r\n",
          strAssetNumber[0],
          strAssetNumber[1],
          strAssetNumber[2]);
  IGCWriteRecord(temp);

  sprintf(temp, "HFDTE%02u%02u%02u\r\n",
          gps_info.DateTime.day,
          gps_info.DateTime.month,
          gps_info.DateTime.year % 100);
  IGCWriteRecord(temp);

  if (!Simulator)
    IGCWriteRecord(GetHFFXARecord());

  sprintf(temp, "HFPLTPILOT:%S\r\n", decl.PilotName);
  IGCWriteRecord(temp);

  sprintf(temp, "HFGTYGLIDERTYPE:%S\r\n", decl.AircraftType);
  IGCWriteRecord(temp);

  sprintf(temp, "HFGIDGLIDERID:%S\r\n", decl.AircraftRego);
  IGCWriteRecord(temp);

  sprintf(temp, "HFFTYFR TYPE:XCSOAR,XCSOAR %S\r\n", XCSoar_VersionStringOld);
  IGCWriteRecord(temp);

  DeviceConfig device_config;
  // this is only the XCSoar Simulator, not Condor etc, so don't use Simulator flag
  if (is_simulator())
    _tcscpy(device_config.driver_name, _T("Simulator"));
  else
    Profile::GetDeviceConfig(0, device_config);

  sprintf(temp, "HFGPS: %S\r\n", device_config.driver_name);
  IGCWriteRecord(temp);

  IGCWriteRecord(datum);

  if (!Simulator)
    IGCWriteRecord(GetIRecord());
}

void
LoggerImpl::StartDeclaration(const NMEA_INFO &gps_info, const int ntp)
{
  // TODO bug: this is causing problems with some analysis software
  // maybe it's because the date and location fields are bogus
  char start[] = "C0000000N00000000ETAKEOFF\r\n";
  char temp[100];

  BrokenDateTime FirstDateTime = !PreTakeoffBuffer.empty()
    ? PreTakeoffBuffer.peek().DateTime
    : gps_info.DateTime;

  // JMW added task start declaration line

  // LGCSTKF013945TAKEOFF DETECTED

  // IGC GNSS specification 3.6.1
  sprintf(temp, "C%02u%02u%02u%02u%02u%02u0000000000%02d\r\n",
          // DD  MM  YY  HH  MM  SS  DD  MM  YY IIII TT
          FirstDateTime.day,
          FirstDateTime.month,
          FirstDateTime.year % 100,
          FirstDateTime.hour,
          FirstDateTime.minute,
          FirstDateTime.second,
          ntp - 2);

  IGCWriteRecord(temp);
  // takeoff line
  // IGC GNSS specification 3.6.3
  IGCWriteRecord(start);
}

void
LoggerImpl::EndDeclaration(void)
{
  // TODO bug: this is causing problems with some analysis software
  // maybe it's because the date and location fields are bogus
  const char start[] = "C0000000N00000000ELANDING\r\n";
  IGCWriteRecord(start);
}

void
LoggerImpl::AddDeclaration(const GEOPOINT &location, const TCHAR *ID)
{
  const fixed Latitude = location.Latitude.value_degrees();
  const fixed Longitude = location.Longitude.value_degrees();

  char szCRecord[500];
  char IDString[MAX_PATH];
  int i;

  int DegLat, DegLon;
  fixed MinLat, MinLon;
  char NoS, EoW;

  TCHAR tmpstring[MAX_PATH];
  _tcscpy(tmpstring, ID);
  _tcsupr(tmpstring);
  for (i = 0; i < (int)_tcslen(tmpstring); i++)
    IDString[i] = (char)tmpstring[i];

  IDString[i] = '\0';

  DegLat = (int)Latitude;
  MinLat = Latitude - fixed(DegLat);
  NoS = 'N';
  if (negative(MinLat) || (((int)MinLat - DegLat == 0) && DegLat < 0)) {
    NoS = 'S';
    DegLat *= -1;
    MinLat *= -1;
  }
  MinLat *= 60;
  MinLat *= 1000;

  DegLon = (int)Longitude;
  MinLon = Longitude - fixed(DegLon);
  EoW = 'E';
  if (negative(MinLon) || ((int)MinLon == DegLon && DegLon < 0)) {
    EoW = 'W';
    DegLon *= -1;
    MinLon *= -1;
  }
  MinLon *= 60;
  MinLon *= 1000;

  sprintf(szCRecord, "C%02d%05.0f%c%03d%05.0f%c%s\r\n",
          DegLat, (double)MinLat, NoS, DegLon, (double)MinLon, EoW, IDString);

  IGCWriteRecord(szCRecord);
}

void
LoggerImpl::LoggerNote(const TCHAR *text)
{
  if (!LoggerActive)
    return;

  char fulltext[500];
  sprintf(fulltext, "LPLT%S\r\n", text);
  IGCWriteRecord(fulltext);
}

static time_t
LogFileDate(const NMEA_INFO &gps_info, const TCHAR *filename)
{
  TCHAR asset[MAX_PATH];
  unsigned short year, month, day, num;
  int matches;
  // scan for long filename
  matches = _stscanf(filename, _T("%hu-%hu-%hu-%7s-%hu.IGC"),
                     &year, &month, &day, asset, &num);

  if (matches == 5) {
    struct tm tm;
    tm.tm_sec = 0;
    tm.tm_min = 0;
    tm.tm_hour = num;
    tm.tm_mday = day;
    tm.tm_mon = month - 1;
    tm.tm_year = year - 1900;
    tm.tm_isdst = -1;
    return mktime(&tm);
  }

  TCHAR cyear, cmonth, cday, cflight;
  // scan for short filename
  matches = _stscanf(filename, _T("%c%c%c%4s%c.IGC"),
		                 &cyear, &cmonth, &cday, asset, &cflight);

  if (matches == 5) {
    int iyear = (int)gps_info.DateTime.year;
    int syear = iyear % 10;
    int yearzero = iyear - syear;
    int yearthis = IGCCharToNum(cyear) + yearzero;
    if (yearthis > iyear)
      yearthis -= 10;

    struct tm tm;
    tm.tm_sec = 0;
    tm.tm_min = 0;
    tm.tm_hour = IGCCharToNum(cflight);
    tm.tm_mday = IGCCharToNum(cday);
    tm.tm_mon = IGCCharToNum(cmonth) - 1;
    tm.tm_year = yearthis - 1900;
    tm.tm_isdst = -1;
    return mktime(&tm);
    /*
      YMDCXXXF.IGC
      Y: Year, 0 to 9 cycling every 10 years
      M: Month, 1 to 9 then A for 10, B=11, C=12
      D: Day, 1 to 9 then A for 10, B=....
      C: Manuf. code = X
      XXX: Logger ID Alphanum
      F: Flight of day, 1 to 9 then A through Z
    */
  }

  return 0;
}

static bool
LogFileIsOlder(const NMEA_INFO &gps_info,
               const TCHAR *oldestname, const TCHAR *thisname)
{
  return LogFileDate(gps_info, oldestname) > LogFileDate(gps_info, thisname);
}

/**
 * Delete eldest IGC file in the given path
 * @param gps_info Current NMEA_INFO
 * @param pathname Path where to search for the IGC files
 * @return True if a file was found and deleted, False otherwise
 */
static bool
DeleteOldestIGCFile(const NMEA_INFO &gps_info, const TCHAR *pathname)
{
  TCHAR oldestname[MAX_PATH];
  TCHAR fullname[MAX_PATH];

  _TDIR *dir = _topendir(pathname);
  if (dir == NULL)
    return false;

  _tdirent *ent;
  while ((ent = _treaddir(dir)) != NULL) {
    if (!MatchesExtension(ent->d_name, _T(".igc")))
      continue;

    _tcscpy(fullname, pathname);
    _tcscpy(fullname, ent->d_name);

    if (File::Exists(fullname) &&
        LogFileIsOlder(gps_info, oldestname, ent->d_name))
      // we have a new oldest name
      _tcscpy(oldestname, ent->d_name);
  }

  _tclosedir(dir);

  // now, delete the file...
  _stprintf(fullname, _T("%s%s"), pathname, oldestname);
  File::Delete(fullname);

  // did delete one
  return true;
}

#define LOGGER_MINFREESTORAGE (250+MINFREESTORAGE)
// JMW note: we want to clear up enough space to save the persistent
// data (85 kb approx) and a new log file

#ifdef DEBUG_IGCFILENAME
static const TCHAR testtext1[] = _T("2007-11-05-XXX-AAA-01.IGC");
static const TCHAR testtext2[] = _T("2007-11-05-XXX-AAA-02.IGC");
static const TCHAR testtext3[] = _T("3BOA1VX2.IGC");
static const TCHAR testtext4[] = _T("5BDX7B31.IGC");
static const TCHAR testtext5[] = _T("3BOA1VX2.IGC");
static const TCHAR testtext6[] = _T("9BDX7B31.IGC");
static const TCHAR testtext7[] = _T("2008-01-05-XXX-AAA-01.IGC");
#endif

/**
 * Deletes old IGC files until at least LOGGER_MINFREESTORAGE KiB of space are
 * available
 * @param gps_info Current NMEA_INFO
 * @return True if enough space could be cleared, False otherwise
 */
bool
LoggerImpl::LoggerClearFreeSpace(const NMEA_INFO &gps_info)
{
  bool found = true;
  unsigned long kbfree = 0;
  const TCHAR *pathname = GetPrimaryDataPath();
  TCHAR subpathname[MAX_PATH];
  int numtries = 0;

  LocalPath(subpathname, _T("logs"));

#ifdef DEBUG_IGCFILENAME
  bool retval;
  retval = LogFileIsOlder(testtext1,
                          testtext2);
  retval = LogFileIsOlder(testtext1,
                          testtext3);
  retval = LogFileIsOlder(testtext4,
                          testtext5);
  retval = LogFileIsOlder(testtext6,
                          testtext7);
#endif

  while (found && ((kbfree = FindFreeSpace(pathname)) < LOGGER_MINFREESTORAGE)
	       && (numtries++ < 100)) {
    /* JMW asking for deleting old files is disabled now --- system
       automatically deletes old files as required
    */

    // search for IGC files, and delete the oldest one
    found = DeleteOldestIGCFile(gps_info, pathname);
    if (!found)
      found = DeleteOldestIGCFile(gps_info, subpathname);
  }

  if (kbfree >= LOGGER_MINFREESTORAGE) {
    LogStartUp(_T("LoggerFreeSpace returned: true"));
    return true;
  } else {
    LogStartUp(_T("LoggerFreeSpace returned: false"));
    return false;
  }
}

// TODO: fix scope so only gui things can start it
void
LoggerImpl::StartLogger(const NMEA_INFO &gps_info,
                        const SETTINGS_COMPUTER &settings,
                        const TCHAR *strAssetNumber,
                        const Declaration &decl)
{
  StartLogger(gps_info, settings, strAssetNumber);
  LoggerHeader(gps_info, decl);

  if (decl.size()) {
    StartDeclaration(gps_info, decl.size());
    for (unsigned i = 0; i< decl.size(); ++i)
      AddDeclaration(decl.get_location(i), decl.get_name(i));

    EndDeclaration();
  }

  LoggerActive = true; // start logger after Header is completed.  Concurrency
}

void
LoggerImpl::clearBuffer()
{
  PreTakeoffBuffer.clear();
}
