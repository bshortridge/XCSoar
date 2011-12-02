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

#include "UnitsConfigPanel.hpp"
#include "DataField/Enum.hpp"
#include "DataField/ComboList.hpp"
#include "DataField/Float.hpp"
#include "Form/Form.hpp"
#include "Form/Edit.hpp"
#include "Form/Util.hpp"
#include "Form/Frame.hpp"
#include "Dialogs/ComboPicker.hpp"
#include "Units/UnitsStore.hpp"
#include "Profile/ProfileKeys.hpp"
#include "Profile/Profile.hpp"
#include "Interface.hpp"
#include "MainWindow.hpp"
#include "Asset.hpp"
#include "Language/Language.hpp"

static WndForm* wf = NULL;
static unsigned SpeedUnits = 1; // default is knots
static unsigned TaskSpeedUnits = 2; // default is kph
static unsigned DistanceUnits = 2; // default is km
static unsigned LiftUnits = 0;
static unsigned AltitudeUnits = 0; //default ft
static unsigned TemperatureUnits = 0; //default is celcius
static bool loading = false;


static void
UpdateUnitFields(const UnitSetting &units)
{
  LoadFormProperty(*wf, _T("prpUnitsLatLon"), units.coordinate_format);

  unsigned index;
  index = (units.speed_unit == unStatuteMilesPerHour) ? 0 :
          (units.speed_unit == unKnots) ? 1 : 2;
  LoadFormProperty(*wf, _T("prpUnitsSpeed"), index);
  if (loading)
    SpeedUnits = index;

  index = (units.task_speed_unit == unStatuteMilesPerHour) ? 0 :
          (units.task_speed_unit == unKnots) ? 1 : 2;
  LoadFormProperty(*wf, _T("prpUnitsTaskSpeed"), index);
  if (loading)
    TaskSpeedUnits = index;

  index = (units.distance_unit == unStatuteMiles) ? 0 :
          (units.distance_unit == unNauticalMiles) ? 1 : 2;
  LoadFormProperty(*wf, _T("prpUnitsDistance"), index);
  if (loading)
    DistanceUnits = index;

  index = (units.altitude_unit == unFeet) ? 0 : 1;
  LoadFormProperty(*wf, _T("prpUnitsAltitude"), index);
  if (loading)
    AltitudeUnits = index;

  index = (units.temperature_unit == unGradFahrenheit) ? 1 : 0;
  LoadFormProperty(*wf, _T("prpUnitsTemperature"), index);
  if (loading)
    TemperatureUnits = index;

  index = (units.vertical_speed_unit == unKnots) ? 0 :
          (units.vertical_speed_unit == unFeetPerMinute) ? 2 : 1;
  LoadFormProperty(*wf, _T("prpUnitsLift"), index);
  if (loading)
    LiftUnits = index;

  LoadFormProperty(*wf, _T("prpUnitsPressure"), units.pressure_unit);
}


static void
SetUnitsTitle(const TCHAR* title)
{
  TCHAR caption[255];
  _tcscpy(caption,  _("Units"));
  _tcscat(caption, _T(": "));
  _tcscat(caption, title);
  ((WndFrame *)wf->FindByName(_T("lblUnitsSetting")))->SetCaption(caption);
}


static void
UpdateUnitsTitle()
{
  TCHAR title[255];
  if (Profile::Get(szProfileUnitsPresetName, title, 255))
    SetUnitsTitle(title);
}


void
UnitsConfigPanel::OnLoadPreset(WndButton &button)
{
  ComboList list;
  unsigned len = Units::Store::Count();
  for (unsigned i = 0; i < len; i++)
    list.Append(i, Units::Store::GetName(i));

  list.Sort();

  /* let the user select */

  int result = ComboPicker(XCSoarInterface::main_window, _("Unit Presets"), list, NULL);
  if (result >= 0) {
    const UnitSetting& units = Units::Store::Read(list[result].DataFieldIndex);
    UpdateUnitFields(units);

    Profile::Set(szProfileUnitsPresetName, list[result].StringValue);
    UpdateUnitsTitle();
  }
}


void
UnitsConfigPanel::OnFieldData(DataField *Sender, DataField::DataAccessKind_t Mode)
{
  switch (Mode) {
  case DataField::daChange:
    if (!loading)
      Profile::Set(szProfileUnitsPresetName, _T("Custom"));
    UpdateUnitsTitle();
    break;

  case DataField::daSpecial:
    return;
  }
}

void
UnitsConfigPanel::Init(WndForm *_wf)
{
  assert(_wf != NULL);
  wf = _wf;

  loading = true;

  WndProperty *wp = (WndProperty*)wf->FindByName(_T("prpUnitsSpeed"));
  assert(wp != NULL);
  DataFieldEnum *dfe = (DataFieldEnum*)wp->GetDataField();
  dfe->addEnumText(_("mph"));
  dfe->addEnumText(_("knots"));
  dfe->addEnumText(_("km/h"));
  wp->RefreshDisplay();

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsLatLon"));
  assert(wp != NULL);
  dfe = (DataFieldEnum*)wp->GetDataField();
  const TCHAR *const units_lat_lon[] = {
    _T("DDMMSS"),
    _T("DDMMSS.ss"),
    _T("DDMM.mmm"),
    _T("DD.dddd"),
    NULL
  };

  dfe->addEnumTexts(units_lat_lon);

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsTaskSpeed"));
  assert(wp != NULL);
  dfe = (DataFieldEnum*)wp->GetDataField();
  dfe->addEnumText(_("mph"));
  dfe->addEnumText(_("knots"));
  dfe->addEnumText(_("km/h"));
  wp->RefreshDisplay();

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsDistance"));
  assert(wp != NULL);
  dfe = (DataFieldEnum*)wp->GetDataField();
  dfe->addEnumText(_("sm"));
  dfe->addEnumText(_("nm"));
  dfe->addEnumText(_("km"));
  wp->RefreshDisplay();

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsAltitude"));
  assert(wp != NULL);
  dfe = (DataFieldEnum*)wp->GetDataField();
  dfe->addEnumText(_("foot"));
  dfe->addEnumText(_("meter"));
  wp->RefreshDisplay();

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsTemperature"));
  assert(wp != NULL);
  dfe = (DataFieldEnum*)wp->GetDataField();
  dfe->addEnumText(_("C"));
  dfe->addEnumText(_("F"));
  wp->RefreshDisplay();

  wp = (WndProperty*)wf->FindByName(_T("prpUnitsLift"));
  assert(wp != NULL);
  dfe = (DataFieldEnum*)wp->GetDataField();
  dfe->addEnumText(_("knots"));
  dfe->addEnumText(_("m/s"));
  dfe->addEnumText(_("ft/min"));
  wp->RefreshDisplay();

  UpdateUnitFields(CommonInterface::GetUISettings().units);

  static gcc_constexpr_data StaticEnumChoice pressure_labels_list[] = {
    { unHectoPascal, N_("hPa") },
    { unMilliBar, N_("mb") },
    { unInchMercury, N_("inHg") },
    { 0 }
  };

  LoadFormProperty(*wf, _T("prpUnitsPressure"), pressure_labels_list,
                   CommonInterface::GetUISettings().units.pressure_unit);

  loading = false;
}


bool
UnitsConfigPanel::Save()
{
  UnitSetting &config = CommonInterface::SetUISettings().units;
  bool changed = false;

  /* the Units settings affect how other form values are read and translated
   * so changes to Units settings should be processed after all other form settings
   */
  int tmp = GetFormValueInteger(*wf, _T("prpUnitsSpeed"));
  if ((int)SpeedUnits != tmp) {
    SpeedUnits = tmp;
    Profile::Set(szProfileSpeedUnitsValue, SpeedUnits);
    changed = true;

    switch (SpeedUnits) {
    case 0:
      config.speed_unit = config.wind_speed_unit = unStatuteMilesPerHour;
      break;
    case 1:
      config.speed_unit = config.wind_speed_unit = unKnots;
      break;
    case 2:
    default:
      config.speed_unit = config.wind_speed_unit = unKiloMeterPerHour;
      break;
    }
  }

  tmp = GetFormValueInteger(*wf, _T("prpUnitsLatLon"));
  if ((int)config.coordinate_format != tmp) {
    config.coordinate_format = (CoordinateFormats)tmp;
    Profile::Set(szProfileLatLonUnits, config.coordinate_format);
    changed = true;
  }

  tmp = GetFormValueInteger(*wf, _T("prpUnitsTaskSpeed"));
  if ((int)TaskSpeedUnits != tmp) {
    TaskSpeedUnits = tmp;
    Profile::Set(szProfileTaskSpeedUnitsValue, TaskSpeedUnits);
    changed = true;

    switch (TaskSpeedUnits) {
    case 0:
      config.task_speed_unit = unStatuteMilesPerHour;
      break;
    case 1:
      config.task_speed_unit = unKnots;
      break;
    case 2:
    default:
      config.task_speed_unit = unKiloMeterPerHour;
      break;
    }
  }

  tmp = GetFormValueInteger(*wf, _T("prpUnitsDistance"));
  if ((int)DistanceUnits != tmp) {
    DistanceUnits = tmp;
    Profile::Set(szProfileDistanceUnitsValue, DistanceUnits);
    changed = true;

    switch (DistanceUnits) {
    case 0:
      config.distance_unit = unStatuteMiles;
      break;
    case 1:
      config.distance_unit = unNauticalMiles;
      break;
    case 2:
    default:
      config.distance_unit = unKiloMeter;
      break;
    }
  }

  tmp = GetFormValueInteger(*wf, _T("prpUnitsLift"));
  if ((int)LiftUnits != tmp) {
    LiftUnits = tmp;
    Profile::Set(szProfileLiftUnitsValue, LiftUnits);
    changed = true;

    switch (LiftUnits) {
    case 0:
      config.vertical_speed_unit = unKnots;
      break;
    case 1:
    default:
      config.vertical_speed_unit = unMeterPerSecond;
      break;
    case 2:
      config.vertical_speed_unit = unFeetPerMinute;
      break;
    }
  }

  changed |= SaveFormPropertyEnum(*wf, _T("prpUnitsPressure"),
                                  szProfilePressureUnitsValue,
                                  config.pressure_unit);

  tmp = GetFormValueInteger(*wf, _T("prpUnitsAltitude"));
  if ((int)AltitudeUnits != tmp) {
    AltitudeUnits = tmp;
    Profile::Set(szProfileAltitudeUnitsValue, AltitudeUnits);
    changed = true;

    switch (AltitudeUnits) {
    case 0:
      config.altitude_unit = unFeet;
      break;
    case 1:
    default:
      config.altitude_unit = unMeter;
      break;
    }
  }

  tmp = GetFormValueInteger(*wf, _T("prpUnitsTemperature"));
  if ((int)TemperatureUnits != tmp) {
    TemperatureUnits = tmp;
    Profile::Set(szProfileTemperatureUnitsValue, TemperatureUnits);
    changed = true;

    switch (TemperatureUnits) {
    case 0:
      config.temperature_unit = unGradCelcius;
      break;
    case 1:
    default:
      config.temperature_unit = unGradFahrenheit;
      break;
    }
  }

  return changed;
}
