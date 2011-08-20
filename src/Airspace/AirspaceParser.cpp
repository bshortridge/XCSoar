/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2011 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

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

#include "AirspaceParser.hpp"
#include "Airspace/Airspaces.hpp"
#include "Operation.hpp"
#include "Units/Units.hpp"
#include "Dialogs/Message.hpp"
#include "Language/Language.hpp"
#include "Util/StringUtil.hpp"
#include "Util/Macros.hpp"
#include "Math/Earth.hpp"
#include "IO/LineReader.hpp"
#include "Airspace/AirspacePolygon.hpp"
#include "Airspace/AirspaceCircle.hpp"
#include "Engine/Navigation/Geometry/GeoVector.hpp"
#include "Compatibility/string.h"
#include "Engine/Airspace/AirspaceClass.hpp"

#include <math.h>
#include <tchar.h>
#include <ctype.h>
#include <assert.h>
#include <stdio.h>
#include <windef.h> /* for MAX_PATH */

#define fixed_7_5 fixed(7.5)

enum AirspaceFileType {
  AFT_UNKNOWN,
  AFT_OPENAIR,
  AFT_TNP
};

struct AirspaceClassStringCouple
{
  const TCHAR *string;
  AirspaceClass type;
};

static const AirspaceClassStringCouple airspace_class_strings[] = {
  { _T("R"), RESTRICT },
  { _T("Q"), DANGER },
  { _T("P"), PROHIBITED },
  { _T("CTR"), CTR },
  { _T("A"), CLASSA },
  { _T("B"), CLASSB },
  { _T("C"), CLASSC },
  { _T("D"), CLASSD },
  { _T("GP"), NOGLIDER },
  { _T("W"), WAVE },
  { _T("E"), CLASSE },
  { _T("F"), CLASSF },
  { _T("TMZ"), TMZ },
  { _T("G"), CLASSG },
};

// this can now be called multiple times to load several airspaces.

struct TempAirspaceType
{
  TempAirspaceType() {
    points.reserve(256);
    reset();
  }

  bool Waiting;

  // General
  tstring Name;
  tstring Radio;
  AirspaceClass Type;
  AirspaceAltitude Base;
  AirspaceAltitude Top;
  AirspaceActivity days_of_operation;

  // Polygon
  std::vector<GeoPoint> points;

  // Circle or Arc
  GeoPoint Center;
  fixed Radius;

  // Arc
  int Rotation;

  void
  reset()
  {
    days_of_operation.set_all();
    Radio = _T("");
    Type = OTHER;
    points.clear();
    Center.Longitude = Angle::zero();
    Center.Latitude = Angle::zero();
    Rotation = 1;
    Radius = fixed_zero;
    Waiting = true;
  }

  void
  AddPolygon(Airspaces &airspace_database)
  {
    AbstractAirspace *as = new AirspacePolygon(points);
    as->SetProperties(Name, Type, Base, Top);
    as->SetRadio(Radio);
    as->SetDays(days_of_operation);
    airspace_database.insert(as);
  }

  void
  AddCircle(Airspaces &airspace_database)
  {
    AbstractAirspace *as = new AirspaceCircle(Center, Radius);
    as->SetProperties(Name, Type, Base, Top);
    as->SetRadio(Radio);
    as->SetDays(days_of_operation);
    airspace_database.insert(as);
  }
};

static bool
ShowParseWarning(int line, const TCHAR* str)
{
  TCHAR sTmp[MAX_PATH];
  _stprintf(sTmp, _T("%s: %d\r\n\"%s\"\r\n%s."),
            _("Parse Error at Line"), line, str,
            _("Line skipped."));
  return (MessageBoxX(sTmp, _("Airspace"), MB_OKCANCEL) == IDOK);

}

static void
ReadAltitude(const TCHAR *Text, AirspaceAltitude *Alt)
{
  bool fHasUnit = false;

  Alt->altitude = fixed_zero;
  Alt->flight_level = fixed_zero;
  Alt->altitude_above_terrain = fixed_zero;
  Alt->type = AirspaceAltitude::UNDEFINED;

  for (const TCHAR *p = Text; *p != _T('\0'); ++p) {
    while (*p == _T(' '))
      ++p;

    if (_istdigit(*p)) {
      TCHAR *endptr;
      fixed d = fixed(_tcstod(p, &endptr));

      if (Alt->type == AirspaceAltitude::FL)
        Alt->flight_level = d;
      else if (Alt->type == AirspaceAltitude::AGL)
        Alt->altitude_above_terrain = d;
      else
        Alt->altitude = d;

      p = endptr;
    } else if (_tcsnicmp(p, _T("GND"), 3) == 0) {
      // JMW support XXXGND as valid, equivalent to XXXAGL
      Alt->type = AirspaceAltitude::AGL;
      if (Alt->altitude > fixed_zero) {
        Alt->altitude_above_terrain = Alt->altitude;
        Alt->altitude = fixed_zero;
      } else {
        Alt->flight_level = fixed_zero;
        Alt->altitude = fixed_zero;
        Alt->altitude_above_terrain = fixed_minus_one;
        fHasUnit = true;
      }

      p += 3;
    } else if (_tcsnicmp(p, _T("SFC"), 3) == 0) {
      Alt->type = AirspaceAltitude::AGL;
      Alt->flight_level = fixed_zero;
      Alt->altitude = fixed_zero;
      Alt->altitude_above_terrain = fixed_minus_one;
      fHasUnit = true;

      p += 3;
    } else if (_tcsnicmp(p, _T("FL"), 2) == 0) {
      // this parses "FL=150" and "FL150"
      Alt->type = AirspaceAltitude::FL;
      fHasUnit = true;

      p += 2;
    } else if (*p == _T('F') || *p == _T('f')) {
      Alt->altitude = Units::ToSysUnit(Alt->altitude, unFeet);
      fHasUnit = true;

      ++p;
      if (*p == _T('T') || *p == _T('t'))
        ++p;
    } else if (_tcsnicmp(p, _T("MSL"), 3) == 0) {
      Alt->type = AirspaceAltitude::MSL;

      p += 3;
    } else if (*p == _T('M') || *p == _T('m')) {
      // JMW must scan for MSL before scanning for M
      fHasUnit = true;

      ++p;
    } else if (_tcsnicmp(p, _T("AGL"), 3) == 0) {
      Alt->type = AirspaceAltitude::AGL;
      Alt->altitude_above_terrain = Alt->altitude;
      Alt->altitude = fixed_zero;

      p += 3;
    } else if (_tcsnicmp(p, _T("STD"), 3) == 0) {
      if (Alt->type != AirspaceAltitude::UNDEFINED) {
        // warning! multiple base tags
      }
      Alt->type = AirspaceAltitude::FL;
      Alt->flight_level = Units::ToUserUnit(Alt->altitude, unFlightLevel);

      p += 3;
    } else if (_tcsnicmp(p, _T("UNL"), 3) == 0) {
      // JMW added Unlimited (used by WGC2008)
      Alt->type = AirspaceAltitude::MSL;
      Alt->altitude_above_terrain = fixed_minus_one;
      Alt->altitude = fixed(50000);

      p += 3;
    }
  }

  if (!fHasUnit && (Alt->type != AirspaceAltitude::FL)) {
    // ToDo warning! no unit defined use feet or user alt unit
    // Alt->Altitude = Units::ToSysAltitude(Alt->Altitude);
    Alt->altitude = Units::ToSysUnit(Alt->altitude, unFeet);
    Alt->altitude_above_terrain = Units::ToSysUnit(Alt->altitude_above_terrain, unFeet);
  }

  if (Alt->type == AirspaceAltitude::UNDEFINED)
    // ToDo warning! no base defined use MSL
    Alt->type = AirspaceAltitude::MSL;
}

static bool
ReadCoords(const TCHAR *Text, GeoPoint &point)
{
  // Format: 53:20:41 N 010:24:41 E
  // Alternative Format: 53:20.68 N 010:24.68 E

  TCHAR *Stop;

  // ToDo, add more error checking and making it more tolerant/robust

  double deg = _tcstod(Text, &Stop);
  if ((Text == Stop) || (*Stop == '\0'))
    return false;

  if (*Stop == ':') {
    Stop++;

    double min = _tcstod(Stop, &Stop);
    if (*Stop == '\0')
      return false;

    deg += min / 60;

    if (*Stop == ':') {
      Stop++;

      double sec = _tcstod(Stop, &Stop);
      if (*Stop == '\0')
        return false;

      deg += sec / 3600;
    }
  }

  point.Latitude = Angle::degrees(fixed(deg));

  if (*Stop == ' ')
    Stop++;

  if (*Stop == '\0')
    return false;

  if ((*Stop == 'S') || (*Stop == 's'))
    point.Latitude.flip();

  Stop++;
  if (*Stop == '\0')
    return false;

  deg = _tcstod(Stop, &Stop);
  if ((Text == Stop) || (*Stop == '\0'))
    return false;

  if (*Stop == ':') {
    Stop++;

    double min = _tcstod(Stop, &Stop);
    if (*Stop == '\0')
      return false;

    deg += min / 60;

    if (*Stop == ':') {
      Stop++;

      double sec = _tcstod(Stop, &Stop);
      if (*Stop == '\0')
        return false;

      deg += sec / 3600;
    }
  }

  point.Longitude = Angle::degrees(fixed(deg));

  if (*Stop == ' ')
    Stop++;

  if (*Stop == '\0')
    return false;

  if ((*Stop == 'W') || (*Stop == 'w'))
    point.Longitude.flip();

  point.normalize(); // ensure longitude is within -180:180
  return true;
}

static void
CalculateSector(const TCHAR *Text, TempAirspaceType &temp_area)
{
  // 5 or -5, depending on direction
  const Angle BearingStep = Angle::degrees(temp_area.Rotation * fixed(5));

  // Determine radius and start/end bearing
  TCHAR *Stop;
  fixed Radius = Units::ToSysUnit(fixed(_tcstod(&Text[2], &Stop)), unNauticalMiles);
  Angle StartBearing = Angle::degrees(fixed(_tcstod(&Stop[1], &Stop))).as_bearing();
  Angle EndBearing = Angle::degrees(fixed(_tcstod(&Stop[1], &Stop))).as_bearing();

  // Add intermediate polygon points
  GeoPoint TempPoint;
  while ((EndBearing - StartBearing).magnitude_degrees() > fixed_7_5) {
    TempPoint = FindLatitudeLongitude(temp_area.Center, StartBearing, Radius);
    temp_area.points.push_back(TempPoint);
    StartBearing = (StartBearing + BearingStep).as_bearing();
  }

  // Add last polygon point
  TempPoint = FindLatitudeLongitude(temp_area.Center, EndBearing, Radius);
  temp_area.points.push_back(TempPoint);
}

static void
AddArc(const GeoPoint Start, const GeoPoint End, TempAirspaceType &temp_area)
{
  // 5 or -5, depending on direction
  const Angle BearingStep = Angle::degrees(temp_area.Rotation * fixed(5));

  // Determine start bearing and radius
  const GeoVector v = temp_area.Center.distance_bearing(Start);
  Angle StartBearing = v.Bearing;
  const fixed Radius = v.Distance;

  // Determine end bearing
  Angle EndBearing = Bearing(temp_area.Center, End);

  // Add first polygon point
  GeoPoint TempPoint = Start;
  temp_area.points.push_back(TempPoint);

  // Add intermediate polygon points
  while ((EndBearing - StartBearing).magnitude_degrees() > fixed_7_5) {
    StartBearing = (StartBearing + BearingStep).as_bearing();
    TempPoint = FindLatitudeLongitude(temp_area.Center, StartBearing, Radius);
    temp_area.points.push_back(TempPoint);
  }

  // Add last polygon point
  TempPoint = End;
  temp_area.points.push_back(TempPoint);
}

static void
CalculateArc(const TCHAR *Text, TempAirspaceType &temp_area)
{
  // Read start coordinates
  GeoPoint Start;
  if (!ReadCoords(&Text[3], Start))
    return;

  // Skip comma character
  const TCHAR* Comma = _tcschr(Text, ',');
  if (!Comma)
    return;

  // Read end coordinates
  GeoPoint End;
  if (!ReadCoords(&Comma[1], End))
    return;

  AddArc(Start, End, temp_area);
}

static AirspaceClass
ParseType(const TCHAR* text)
{
  for (unsigned i = 0; i < ARRAY_SIZE(airspace_class_strings); i++)
    if (string_after_prefix(text, airspace_class_strings[i].string))
      return airspace_class_strings[i].type;

  return OTHER;
}

/**
 * Returns the value of the specified line, after a space character
 * which is skipped.  If the input is empty (without a leading space),
 * an empty string is returned, as a special case to work around
 * broken input files.
 *
 * @return the first character of the value, or NULL if the input is
 * malformed
 */
static const TCHAR *
value_after_space(const TCHAR *p)
{
  if (string_is_empty(p))
    return p;

  if (*p != _T(' '))
    /* not a space: must be a malformed line */
    return NULL;

  /* skip the space */
  return p + 1;
}

static bool
ParseLine(Airspaces &airspace_database, const TCHAR *line,
          TempAirspaceType &temp_area)
{
  const TCHAR *value;

  // Only return expected lines
  switch (line[0]) {
  case _T('D'):
  case _T('d'):
    switch (line[1]) {
    case _T('P'):
    case _T('p'):
      value = value_after_space(line + 2);
      if (value == NULL)
        break;

    {
      GeoPoint TempPoint;
      if (!ReadCoords(value, TempPoint))
        return false;

      temp_area.points.push_back(TempPoint);
      break;
    }

    case _T('C'):
    case _T('c'):
      temp_area.Radius = Units::ToSysUnit(fixed(_tcstod(&line[2], NULL)),
                                          unNauticalMiles);
      temp_area.AddCircle(airspace_database);
      temp_area.reset();
      break;

    case _T('A'):
    case _T('a'):
      CalculateSector(line, temp_area);
      break;

    case _T('B'):
    case _T('b'):
      CalculateArc(line, temp_area);
      break;

    default:
      return true;
    }
    break;

  case _T('V'):
  case _T('v'):
    // Need to set these while in count mode, or DB/DA will crash
    if (string_after_prefix_ci(&line[2], _T("X="))) {
      if (!ReadCoords(&line[4],temp_area.Center))
        return false;
    } else if (string_after_prefix_ci(&line[2], _T("D=-"))) {
      temp_area.Rotation = -1;
    } else if (string_after_prefix_ci(&line[2], _T("D=+"))) {
      temp_area.Rotation = +1;
    }
    break;

  case _T('A'):
  case _T('a'):
    switch (line[1]) {
    case _T('C'):
    case _T('c'):
      value = value_after_space(line + 2);
      if (value == NULL)
        break;

      if (!temp_area.Waiting)
        temp_area.AddPolygon(airspace_database);

      temp_area.reset();

      temp_area.Type = ParseType(value);
      temp_area.Waiting = false;
      break;

    case _T('N'):
    case _T('n'):
      value = value_after_space(line + 2);
      if (value != NULL)
        temp_area.Name = value;
      break;

    case _T('L'):
    case _T('l'):
      value = value_after_space(line + 2);
      if (value != NULL)
        ReadAltitude(value, &temp_area.Base);
      break;

    case _T('H'):
    case _T('h'):
      value = value_after_space(line + 2);
      if (value != NULL)
        ReadAltitude(value, &temp_area.Top);
      break;

    case _T('R'):
    case _T('r'):
      value = value_after_space(line + 2);
      if (value != NULL)
        temp_area.Radio = value;
      break;

    default:
      return true;
    }

    break;

  }
  return true;
}

static AirspaceClass
ParseClassTNP(const TCHAR* text)
{
  if (text[0] == _T('A'))
    return CLASSA;

  if (text[0] == _T('B'))
    return CLASSB;

  if (text[0] == _T('C'))
    return CLASSC;

  if (text[0] == _T('D'))
    return CLASSD;

  if (text[0] == _T('E'))
    return CLASSE;

  if (text[0] == _T('F'))
    return CLASSF;

  if (text[0] == _T('G'))
    return CLASSG;

  return OTHER;
}

static AirspaceClass
ParseTypeTNP(const TCHAR* text)
{
  if (_tcsicmp(text, _T("C")) == 0 ||
      _tcsicmp(text, _T("CTA")) == 0 ||
      _tcsicmp(text, _T("CTA")) == 0 ||
      _tcsicmp(text, _T("CTA/CTR")) == 0)
    return CTR;

  if (_tcsicmp(text, _T("R")) == 0 ||
      _tcsicmp(text, _T("RESTRICTED")) == 0)
    return RESTRICT;

  if (_tcsicmp(text, _T("P")) == 0 ||
      _tcsicmp(text, _T("PROHIBITED")) == 0)
    return RESTRICT;

  if (_tcsicmp(text, _T("D")) == 0 ||
      _tcsicmp(text, _T("DANGER")) == 0)
    return RESTRICT;

  if (_tcsicmp(text, _T("G")) == 0 ||
      _tcsicmp(text, _T("GSEC")) == 0)
    return WAVE;

  return OTHER;
}

static bool
ParseCoordsTNP(const TCHAR *Text, GeoPoint &point)
{
  // Format: N542500 E0105000
  bool negative = false;
  long deg = 0, min = 0, sec = 0;
  TCHAR *ptr;

  if (Text[0] == _T('S') || Text[0] == _T('s'))
    negative = true;

  sec = _tcstol(&Text[1], &ptr, 10);
  deg = labs(sec / 10000);
  min = labs((sec - deg * 10000) / 100);
  sec = sec - min * 100 - deg * 10000;

  point.Latitude = Angle::dms(fixed(deg), fixed(min), fixed(sec));
  if (negative)
    point.Latitude.flip();

  negative = false;

  if (ptr[0] == _T(' '))
    ptr++;

  if (ptr[0] == _T('W') || ptr[0] == _T('w'))
    negative = true;

  sec = _tcstol(&ptr[1], &ptr, 10);
  deg = labs(sec / 10000);
  min = labs((sec - deg * 10000) / 100);
  sec = sec - min * 100 - deg * 10000;

  point.Longitude = Angle::dms(fixed(deg), fixed(min), fixed(sec));
  if (negative)
    point.Longitude.flip();

  point.normalize(); // ensure longitude is within -180:180

  return true;
}

static bool
ParseArcTNP(const TCHAR *Text, TempAirspaceType &temp_area)
{
  if (temp_area.points.empty())
    return false;

  // (ANTI-)CLOCKWISE RADIUS=34.95 CENTRE=N523333 E0131603 TO=N522052 E0122236

  GeoPoint from = temp_area.points.back();

  const TCHAR* parameter;
  if ((parameter = _tcsstr(Text, _T(" "))) == NULL)
    return false;
  if ((parameter = string_after_prefix_ci(parameter, _T(" CENTRE="))) == NULL)
    return false;

  ParseCoordsTNP(parameter, temp_area.Center);

  if ((parameter = _tcsstr(parameter, _T(" "))) == NULL)
    return false;
  parameter++;
  if ((parameter = _tcsstr(parameter, _T(" "))) == NULL)
    return false;
  if ((parameter = string_after_prefix_ci(parameter, _T(" TO="))) == NULL)
    return false;

  GeoPoint to;
  ParseCoordsTNP(parameter, to);

  AddArc(from, to, temp_area);

  return true;
}

static bool
ParseCircleTNP(const TCHAR *Text, TempAirspaceType &temp_area)
{
  // CIRCLE RADIUS=17.00 CENTRE=N533813 E0095943

  const TCHAR* parameter;
  if ((parameter = string_after_prefix_ci(Text, _T("RADIUS="))) == NULL)
    return false;
  temp_area.Radius = Units::ToSysUnit(fixed(_tcstod(parameter, NULL)),
                                      unNauticalMiles);

  if ((parameter = _tcsstr(parameter, _T(" "))) == NULL)
    return false;
  if ((parameter = string_after_prefix_ci(parameter, _T(" CENTRE="))) == NULL)
    return false;
  ParseCoordsTNP(parameter, temp_area.Center);

  return true;
}

static bool
ParseLineTNP(Airspaces &airspace_database, const TCHAR *line,
             TempAirspaceType &temp_area, bool &ignore)
{
  const TCHAR* parameter;
  if ((parameter = string_after_prefix_ci(line, _T("INCLUDE="))) != NULL) {
    if (_tcsicmp(parameter, _T("YES")) == 0)
      ignore = false;
    else if (_tcsicmp(parameter, _T("NO")) == 0)
      ignore = true;

    return true;
  }

  if (ignore)
    return true;

  if ((parameter = string_after_prefix_ci(line, _T("POINT="))) != NULL) {
    GeoPoint TempPoint;
    if (!ParseCoordsTNP(parameter, TempPoint))
      return false;

    temp_area.points.push_back(TempPoint);
  } else if ((parameter =
      string_after_prefix_ci(line, _T("CIRCLE "))) != NULL) {
    if (!ParseCircleTNP(parameter, temp_area))
      return false;

    temp_area.AddCircle(airspace_database);
  } else if ((parameter =
      string_after_prefix_ci(line, _T("CLOCKWISE "))) != NULL) {
    temp_area.Rotation = 1;
    if (!ParseArcTNP(parameter, temp_area))
      return false;
  } else if ((parameter =
      string_after_prefix_ci(line, _T("ANTI-CLOCKWISE "))) != NULL) {
    temp_area.Rotation = -1;
    if (!ParseArcTNP(parameter, temp_area))
      return false;
  } else if ((parameter = string_after_prefix_ci(line, _T("TITLE="))) != NULL) {
    temp_area.Name = parameter;
  } else if ((parameter = string_after_prefix_ci(line, _T("TYPE="))) != NULL) {
    if (!temp_area.Waiting)
      temp_area.AddPolygon(airspace_database);

    temp_area.reset();

    temp_area.Type = ParseTypeTNP(parameter);
    temp_area.Waiting = false;
  } else if ((parameter = string_after_prefix_ci(line, _T("CLASS="))) != NULL) {
    if (temp_area.Type == OTHER)
      temp_area.Type = ParseClassTNP(parameter);
  } else if ((parameter = string_after_prefix_ci(line, _T("TOPS="))) != NULL) {
    ReadAltitude(parameter, &temp_area.Top);
  } else if ((parameter = string_after_prefix_ci(line, _T("BASE="))) != NULL) {
    ReadAltitude(parameter, &temp_area.Base);
  } else if ((parameter = string_after_prefix_ci(line, _T("RADIO="))) != NULL) {
    temp_area.Radio = parameter;
  } else if ((parameter = string_after_prefix_ci(line, _T("ACTIVE="))) != NULL) {
    if (_tcsicmp(parameter, _T("WEEKEND")) == 0)
      temp_area.days_of_operation.set_weekend();
    else if (_tcsicmp(parameter, _T("WEEKDAY")) == 0)
      temp_area.days_of_operation.set_weekdays();
    else if (_tcsicmp(parameter, _T("EVERYDAY")) == 0)
      temp_area.days_of_operation.set_all();
  }

  return true;
}

static AirspaceFileType
DetectFileType(const TCHAR *line)
{
  if (string_after_prefix_ci(line, _T("INCLUDE=")) ||
      string_after_prefix_ci(line, _T("TYPE=")))
    return AFT_TNP;

  const TCHAR *p = string_after_prefix_ci(line, _T("AC"));
  if (p != NULL && (string_is_empty(p) || *p == _T(' ')))
    return AFT_OPENAIR;

  return AFT_UNKNOWN;
}

bool
AirspaceParser::Parse(TLineReader &reader, OperationEnvironment &operation)
{
  bool ignore = false;

  // Create and init ProgressDialog
  operation.SetProgressRange(1024);

  long file_size = reader.size();

  TempAirspaceType temp_area;
  AirspaceFileType filetype = AFT_UNKNOWN;

  TCHAR *line;
  TCHAR *comment;

  // Iterate through the lines
  for (unsigned LineCount = 1; (line = reader.read()) != NULL; LineCount++) {
    // Strip comments
    comment = _tcschr(line, _T('*'));
    if (comment != NULL)
      *comment = _T('\0');

    // Skip empty line
    if (string_is_empty(line))
      continue;

    if (filetype == AFT_UNKNOWN) {
      filetype = DetectFileType(line);
      if (filetype == AFT_UNKNOWN)
        continue;
    }

    // Parse the line
    if (filetype == AFT_OPENAIR)
      if (!ParseLine(airspaces, line, temp_area) &&
          !ShowParseWarning(LineCount, line))
        return false;

    if (filetype == AFT_TNP)
      if (!ParseLineTNP(airspaces, line, temp_area, ignore) &&
          !ShowParseWarning(LineCount, line))
        return false;

    // Update the ProgressDialog
    if ((LineCount & 0xff) == 0)
      operation.SetProgressPosition(reader.tell() * 1024 / file_size);
  }

  if (filetype == AFT_UNKNOWN) {
    operation.SetErrorMessage(_("Unknown airspace filetype"));
    return false;
  }

  // Process final area (if any)
  if (!temp_area.Waiting)
    temp_area.AddPolygon(airspaces);

  return true;
}
