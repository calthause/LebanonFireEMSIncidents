#include <Arduino.h>
#include <cstring>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <time.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"

// CYD28 Display configuration - ILI9341 with SPI
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9341  _panel_instance;
  lgfx::Bus_SPI        _bus_instance;
  lgfx::Light_PWM      _light_instance;
  lgfx::Touch_XPT2046  _touch_instance;

public:
  LGFX(void)
  {
    {   // SPI bus – display (HSPI)
      auto cfg = _bus_instance.config();
      cfg.spi_host    = HSPI_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 55000000;
      cfg.freq_read   = 20000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = 1;

      cfg.pin_sclk    = 14;
      cfg.pin_mosi    = 13;
      cfg.pin_miso    = 12;
      cfg.pin_dc      = 2;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {   // ILI9341 panel
      auto cfg = _panel_instance.config();

      cfg.pin_cs           = 15;
      cfg.pin_rst          = -1;
      cfg.pin_busy         = -1;

      cfg.memory_width     = 320;
      cfg.memory_height    = 240;
      cfg.panel_width      = 320;
      cfg.panel_height     = 240;

      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 7;

      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = false;
      cfg.rgb_order        = true;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;

      _panel_instance.config(cfg);
    }

    {   // Backlight PWM
      auto cfg = _light_instance.config();
      cfg.pin_bl      = 21;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    {   // Touch – XPT2046
      auto cfg = _touch_instance.config();

      cfg.x_min           = 300;
      cfg.x_max           = 3900;
      cfg.y_min           = 200;
      cfg.y_max           = 3700;

      cfg.pin_int         = 36;
      cfg.bus_shared      = true;
      cfg.offset_rotation = 3;

      cfg.spi_host        = -1;
      cfg.freq            = 2500000;

      cfg.pin_sclk        = 25;
      cfg.pin_mosi        = 32;
      cfg.pin_miso        = 39;
      cfg.pin_cs          = 33;

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

static LGFX display;

const uint16_t COLOR_BG = 0x080F;
const uint16_t COLOR_HEADER = 0x1D4E;
const uint16_t COLOR_CARD = 0x1D1D;
const uint16_t COLOR_MUTED = 0x6B6D;
const uint16_t COLOR_RED = 0xB104;
const uint16_t COLOR_DARK_RED = 0x6800;
const uint16_t COLOR_AMBER = 0xF9A0;
const uint16_t COLOR_GREEN = 0x3A9A;
const uint16_t COLOR_BLUE = 0x2E8B;
const uint16_t COLOR_DARK_GRAY = 0x4208;
const uint16_t COLOR_DARK_BLUE = 0x0010;
const uint16_t COLOR_DARK_GREEN = 0x0320;
const uint16_t COLOR_WHITE = 0xFFFF;
const uint16_t COLOR_CYAN = 0x07FF;
const char* NTP_SERVER = "pool.ntp.org";
const char* NTP_SERVER_BACKUP = "time.nist.gov";
const char* NTP_SERVER_SECOND_BACKUP = "time.google.com";
const char* TIME_ZONE = "EST5EDT,M3.2.0/2,M11.1.0/2"; // U.S. Eastern with DST
const char* INCIDENT_FEED_HOST = "www.lcdes.org";
const char* INCIDENT_FEED_PATH = "/monitor.html";
const unsigned long INCIDENT_FEED_REFRESH_MS = 60000;
const long INCIDENT_LOOKBACK_SECONDS = 3 * 60 * 60;
const int MAX_DISPLAY_INCIDENTS = 12;
volatile bool incidentFetchComplete = false;
bool feedHealthy = false;
time_t lastSuccessfulFeedEpoch = 0;

struct Incident
{
  char title[48];
  char township[64];
  char street[96];
  char unit[128];
  char company[64];
  uint16_t color;
  int minutes;
  time_t eventEpoch;
};

Incident incidents[MAX_DISPLAY_INCIDENTS] = {
  {"Waiting for live incidents", "Lebanon County", "", "", "", COLOR_DARK_GRAY, 0}
};

int incidentCount = 1;
int activeIncidentCount = 0;
int respondingUnitCount = 0;
int accidentIncidentCount = 0;
int fireIncidentCount = 0;
int medicalIncidentCount = 0;
const unsigned long INCIDENT_CYCLE_MS = 5000;
unsigned long lastIncidentFeedUpdate = 0;
const int COUNTY_OUTLINE_X = 160;
const int COUNTY_OUTLINE_Y = 0;
const int ROTATION_BUTTON_X = 227;
const int ROTATION_BUTTON_Y = 3;
const int ROTATION_BUTTON_W = 33;
const int ROTATION_BUTTON_H = 24;
bool incidentRotationPaused = false;
bool unitsHelpVisible = false;
bool wifiInfoVisible = false;
enum class IncidentFilter
{
  All,
  Fire,
  Medical,
  Vehicle
};
IncidentFilter incidentFilter = IncidentFilter::All;
bool companyDirectoryVisible = false;
int companyDirectoryPage = 0;
const int COMPANIES_PER_PAGE = 8;
struct FireCompany
{
  const char* station;
  const char* name;
};
const FireCompany fireCompanies[] = {
  {"01", "Palmyra Citizen's"}, {"02", "Campbelltown Fire"},
  {"03", "Lawn Fire"}, {"05", "Union Hose Annville"},
  {"06", "Bellgrove Fire"}, {"07", "Union Waterworks"},
  {"08", "Cleona Fire"}, {"09", "Ebenezer Fire"},
  {"10", "Perseverance Jonestown"}, {"11", "Lickdale"},
  {"12", "Ono"}, {"14", "Neversink"},
  {"15", "LBF Chief's"}, {"16", "Union Lebanon"},
  {"17", "Perseverance Lebanon"}, {"18", "Hook and Ladder Lebanon"},
  {"19", "Liberty"}, {"20", "Rescue"},
  {"21", "Goodwill Lebanon"}, {"22", "Chemical"},
  {"23", "Washington"}, {"24", "Independent"},
  {"25", "Friendship"}, {"26", "Hebron Hose"},
  {"27", "Citizen's Avon"}, {"28", "Weavertown"},
  {"29", "Prescott"}, {"30", "Goodwill Myerstown"},
  {"31", "H&L Myerstown"}, {"32", "Kutztown"},
  {"33", "Neptune"}, {"34", "Newmanstown"},
  {"35", "Schaefferstown"}, {"36", "Community FC Cornwall"},
  {"37", "Quentin"}, {"38", "Mt Gretna"},
  {"39", "Speedwell"}, {"40", "Mt Zion"},
  {"41", "Fredericksburg"}, {"42", "Glenn Lebanon"},
  {"43", "Rural Security"}, {"46", "Greenpoint"},
  {"47", "Bunkerhill"}, {"48", "S. Lebanon Twp Chiefs"},
  {"49", "Salvation Army Canteen"}, {"50", "LEMA HAZMAT"},
  {"57", "NLFES"}, {"75", "Fort Indiantown Gap"}
};
const char* TOUCH_PREFERENCES_NAMESPACE = "touch";
const char* TOUCH_CALIBRATION_KEY = "calibration";

void copyIncidentText(char* destination, size_t destinationSize, const String& value)
{
  String cleaned = value;
  cleaned.trim();
  cleaned.toCharArray(destination, destinationSize);
}

void normalizeUnitText(char* units, size_t unitsSize)
{
  String normalized = units;
  normalized.replace("<br>", "\n");
  normalized.replace("<br/>", "\n");
  normalized.replace("<br />", "\n");
  normalized.replace("<BR>", "\n");
  normalized.replace("<BR/>", "\n");
  normalized.replace("<BR />", "\n");
  normalized.replace("&amp;", "&");
  normalized.replace("&nbsp;", " ");
  normalized.replace(",", "\n");
  normalized.replace(";", "\n");
  normalized.replace("  ", " ");

  while (true)
  {
    const int tagStart = normalized.indexOf('<');
    if (tagStart < 0)
    {
      break;
    }
    const int tagEnd = normalized.indexOf('>', tagStart);
    if (tagEnd < 0)
    {
      normalized.remove(tagStart);
      break;
    }
    normalized.remove(tagStart, tagEnd - tagStart + 1);
  }

  for (unsigned int index = 0; index < normalized.length(); ++index)
  {
    const char character = normalized[index];
    if (character != '\n' && (character < 32 || character > 126))
    {
      normalized.setCharAt(index, ' ');
    }
  }
  normalized.trim();
  normalized.toCharArray(units, unitsSize);
}

String rssTagValue(const String& item, const char* tag)
{
  const String openTag = String("<") + tag + ">";
  const String closeTag = String("</") + tag + ">";
  const int start = item.indexOf(openTag);
  if (start < 0)
  {
    return String();
  }

  const int valueStart = start + openTag.length();
  const int end = item.indexOf(closeTag, valueStart);
  if (end < 0)
  {
    return String();
  }
  return item.substring(valueStart, end);
}

String cleanIncidentDescription(String description)
{
  description.replace("<br>", ";");
  description.replace("<br/>", ";");
  description.replace("<br />", ";");
  description.replace("&amp;lt;", "<");
  description.replace("&amp;gt;", ">");
  description.replace("&amp;", "&");
  description.replace("&lt;", "<");
  description.replace("&gt;", ">");
  description.replace("&apos;", "'");
  description.replace("&quot;", "\"");
  description.replace("<br>", ";");
  description.replace("<br/>", ";");
  description.replace("<br />", ";");

  while (true)
  {
    const int tagStart = description.indexOf('<');
    if (tagStart < 0)
    {
      break;
    }
    const int tagEnd = description.indexOf('>', tagStart);
    if (tagEnd < 0)
    {
      description.remove(tagStart);
      break;
    }
    description.remove(tagStart, tagEnd - tagStart + 1);
  }
  description.trim();
  return description;
}

uint16_t incidentColor(const String& title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
  if (upperTitle.indexOf("FIRE") >= 0 || upperTitle.indexOf("ALARM") >= 0 ||
      upperTitle.indexOf("SF-") >= 0 || upperTitle.indexOf("AFA") >= 0 ||
      upperTitle.indexOf("GAS LEAK") >= 0)
  {
    return COLOR_RED;
  }
  if (upperTitle.indexOf("MEDICAL") >= 0 || upperTitle.indexOf("MED ") >= 0 ||
      upperTitle.indexOf("EMS") >= 0)
  {
    return COLOR_AMBER;
  }
  if (upperTitle.indexOf("ACCIDENT") >= 0 || upperTitle.indexOf("CRASH") >= 0 ||
      upperTitle.indexOf("MVA") >= 0)
  {
    return COLOR_BLUE;
  }
  return COLOR_GREEN;
}

bool isVehicleAccident(const String& title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
  return upperTitle.indexOf("VEHICLE ACCIDENT") >= 0 ||
      upperTitle.indexOf("VEHICLE CRASH") >= 0 ||
      upperTitle.indexOf("MVA") >= 0;
}

bool isClassOneIncident(const char* title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
  return upperTitle.indexOf("CLASS 1") >= 0 || upperTitle.indexOf("CLASS1") >= 0;
}

bool isFireIncident(const String& title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
    return upperTitle.indexOf("FIRE") >= 0 || upperTitle.indexOf("ALARM") >= 0 ||
      upperTitle.indexOf("SF-") >= 0 || upperTitle.indexOf("AFA") >= 0 ||
      upperTitle.indexOf("GAS LEAK") >= 0;
}

bool isMedicalIncident(const String& title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
    return upperTitle.indexOf("MEDICAL") >= 0 || upperTitle.indexOf("MED ") >= 0 ||
      upperTitle.indexOf("EMS") >= 0;
}

bool isMedicalEmergency(const char* title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
  return upperTitle.indexOf("MEDICAL EMERGENCY") >= 0;
}

int countAssignedUnits(const char* units)
{
  if (units[0] == '\0')
  {
    return 0;
  }

  int count = 1;
  for (const char* character = units; *character != '\0'; ++character)
  {
    if (*character == '\n' || *character == ';' || *character == ',')
    {
      ++count;
    }
  }
  return count;
}

struct StationCompany
{
  const char* code;
  const char* company;
};

const StationCompany stationCompanies[] = {
  {" 02-", "Warwick EMS"}, {" 2-", "Warwick EMS"},
  {" 04-", "Wellspan EMS"}, {" 4-", "Wellspan EMS"},
  {" 06-", "Lancaster EMS"}, {" 6-", "Lancaster EMS"},
  {" 37-", "New Holland Ambulance"},
  {" 56-", "Lancaster EMS"}, {" 77-", "Penn State Health EMS"},
  {" 86-", "MESA"}, {" 09-", "Reinholds Ambulance"},
  {" 1", "Ephrata Ambulance"},
  {" 2-1", "Warwick EMS"}, {" 2-2", "Warwick EMS"},
  {" 2-3", "Warwick EMS"}, {" 2-4", "Warwick EMS"},
  {" 3", "Martindale Fire Co."},
  {" 04-1", "Wellspan EMS"}, {" 04-3", "Wellspan EMS"},
  {" 04-5", "Wellspan EMS"}, {" 04-6", "Wellspan EMS"},
  {" 5", "Strasburg Fire Co."},
  {" 06-1", "Lancaster EMS"}, {" 06-2", "Lancaster EMS"},
  {" 06-3", "Lancaster EMS"}, {" 06-4", "Lancaster EMS"},
  {" 06-5", "Lancaster EMS"}, {" 06-6", "Lancaster EMS"},
  {" 06-7", "Lancaster EMS"}, {" 06-8", "Lancaster EMS"},
  {" 06-9", "Lancaster EMS"}, {" 7", "Mountville Fire Co."},
  {" 9", "Reinholds Ambulance"}, {" 10", "Marietta Fire Co."},
  {" 11", "Adamstown Fire Co."}, {" 12", "Akron Fire Co."},
  {" 13", "Denver Fire Co."}, {" 14", "Durlach & Mount Airy Fire Co."},
  {" 15", "Ephrata Fire Co."}, {" 16", "Lincoln Fire Co."},
  {" 17-1", "Reamstown Fire Co."}, {" 17-2", "Smokestown Fire Co."},
  {" 17-3", "Stevens Fire Co."}, {" 18", "Reinholds Fire Co."},
  {" 19", "Schoeneck Fire Co."}, {" 20", "Manheim Twp. Fire & Rescue"},
  {" 21", "Brickerville Fire Co."}, {" 22", "Brunnerville Fire Co."},
  {" 23", "East Petersburg Fire Co."}, {" 24", "Rothsville Ambulance"},
  {" 25", "Lititz Fire Co."}, {" 26", "Manheim Fire Co."},
  {" 27", "Mastersonville Fire Co."}, {" 28", "Penryn Fire Co."},
  {" 29", "West Earl Fire Co."}, {" 30", "Weaverland Valley Fire Dept."},
  {" 31", "Bareville Fire Co."}, {" 32", "Fivepointville Fire Co."},
  {" 33", "Bowmansville Fire Co."}, {" 34", "Caernarvon Fire Co."},
  {" 35", "Farmersville Fire Co."}, {" 36", "Fivepointville Ambulance"},
  {" 37-1", "New Holland Ambulance"}, {" 37-8", "New Holland Ambulance"},
  {" 39", "Garden Spot Fire Rescue"}, {" 40", "Pequea Valley Fire Dept."},
  {" 41", "Bird-in-Hand Fire Co."}, {" 42", "Gap Fire Co."},
  {" 43", "Gordonville Fire Co."}, {" 44", "Intercourse Fire Co."},
  {" 45", "Kinzer Fire Co."}, {" 46", "Christiana Ambulance"},
  {" 47", "Paradise Fire Co."}, {" 48", "Ronks Fire Co."},
  {" 49", "White Horse Fire Co."}, {" 50", "Willow Street Fire Co."},
  {" 51", "Bart Twp. Fire Co."}, {" 52", "Christiana Fire Co."},
  {" 53", "Conestoga Fire Co."}, {" 54", "Lampeter Fire Co."},
  {" 55", "New Danville Fire Co."},
  {" 56-1", "Lancaster EMS"}, {" 56-2", "Lancaster EMS"},
  {" 56-3", "Lancaster EMS"}, {" 56-4", "Lancaster EMS"},
  {" 56-5", "Lancaster EMS"}, {" 56-6", "Lancaster EMS"},
  {" 56-7", "Lancaster EMS"}, {" 56-8", "Lancaster EMS"},
  {" 56-9", "Lancaster EMS"}, {" 57", "Quarryville Fire Company"},
  {" 58", "Rawlinsville Fire Co."}, {" 59", "Refton Fire Co."},
  {" 60", "West Willow Fire Company"}, {" 61", "Upper Leacock Fire Co."},
  {" 62", "Witmer Fire Co."}, {" 63", "Lafayette Fire Co."},
  {" 64-", "Lancaster City Fire Dept."}, {" 64", "Lancaster City Fire Dept."},
  {" 66", "Lancaster Twp. Fire Dept."},
  {" 67", "Rohrerstown Fire Co."}, {" 69", "Hempfield Fire Co."},
  {" 70", "Rheems Fire Co."}, {" 71", "Bainbridge Fire Co."},
  {" 73", "Franklin & Marshall College QRS"},
  {" 74", "Elizabethtown Fire Co."}, {" 75", "Fire Dept. Mount Joy"},
  {" 76", "West Hempfield Fire & Rescue"},
  {" 77-1", "Penn State Health EMS"}, {" 77-2", "Penn State Health EMS"},
  {" 77-3", "Penn State Health EMS"}, {" 77-4", "Penn State Health EMS"},
  {" 77-5", "Penn State Health EMS"}, {" 77-6", "Penn State Health EMS"},
  {" 77-7", "Penn State Health EMS"}, {" 77-8", "Penn State Health EMS"},
  {" 77-9", "Penn State Health EMS"}, {" 79", "Maytown-E. Donegal Fire Co."},
  {" 80", "Columbia Borough Fire Dept."}, {" 82", "Manheim Twp. Ambulance"},
  {" 85", "Warwick Ambulance"}, {" 86-1", "MESA"}, {" 86-2", "MESA"},
  {" 86-3", "MESA"}, {" 86-4", "MESA"}, {" 86-5", "MESA"},
  {" 87", "Lancaster Co. EMA"}, {" 88", "Wakefield Ambulance"},
  {" 88-1", "Wakefield Ambulance"}, {" 89", "Robert Fulton Fire Co."},
  {" 90", "Blue Rock Fire & Rescue"},
  {" 91", "Lancaster County-Wide Communications"},
  {" 92", "Northern Lancaster County Forest Fire Co."},
  {" 93", "Mt. Joy Twp. Forest Fire Crew"}, {" 94", "Middle Creek S.A.R."},
  {" 95", "PA Canine S.A.R."}, {" 96", "PA Wilderness S.A.R."},
  {" 97", "Lancaster Airport"}, {" 98", "Arconic Mill Products FD"},
  {" 99", "Lanc. Co. Public Safety Training Ctr."}
};

const char* stationCompanyForUnits(const char* units)
{
  String upperUnits = units;
  upperUnits.toUpperCase();
  const int stationCount = sizeof(stationCompanies) / sizeof(stationCompanies[0]);
  for (int index = 0; index < stationCount; ++index)
  {
    const char* match = upperUnits.c_str();
    while ((match = strstr(match, stationCompanies[index].code)) != nullptr)
    {
      const size_t codeLength = strlen(stationCompanies[index].code);
      const char after = match[codeLength];
        const bool prefixCode = stationCompanies[index].code[codeLength - 1] == '-';
      const bool plainStationCode = strchr(stationCompanies[index].code + 1, '-') == nullptr;
      const bool hasStationSuffix = plainStationCode && after == '-' &&
                                    match[codeLength + 1] >= '0' &&
                                    match[codeLength + 1] <= '9';
        const bool hasPrefixNumber = prefixCode && after >= '0' && after <= '9';
        if (after == '\0' || after == ' ' || after == '\n' || after == ';' ||
          hasStationSuffix || hasPrefixNumber)
      {
        return stationCompanies[index].company;
      }
      ++match;
    }
  }
  return "Unknown";
}

String cleanLebanonCell(String value)
{
  value.replace("&nbsp;", " ");
  value.replace("&#160;", " ");
  value.replace("&amp;", "&");
  value.replace("&quot;", "\"");
  value.replace("&#39;", "'");
  value.replace("\r", " ");
  value.replace("\n", " ");
  while (true)
  {
    const int tagStart = value.indexOf('<');
    if (tagStart < 0)
    {
      break;
    }
    const int tagEnd = value.indexOf('>', tagStart);
    if (tagEnd < 0)
    {
      value.remove(tagStart);
      break;
    }
    value.remove(tagStart, tagEnd - tagStart + 1);
  }
  for (int index = value.length() - 1; index >= 0; --index)
  {
    if (static_cast<unsigned char>(value[index]) < 32)
    {
      value.remove(index, 1);
    }
  }
  value.trim();
  return value;
}

bool parseLebanonHtmlRow(const String& row, Incident& incident, time_t& eventEpoch)
{
  static int printedRawRows = 0;
  String rawLower = row;
  rawLower.toLowerCase();
  if (printedRawRows < 3 && rawLower.indexOf("<hr") < 0)
  {
    String preview = row;
    if (preview.length() > 240)
    {
      preview.remove(240);
    }
    Serial.printf("Incident raw row %d: [%s]\n", printedRawRows, preview.c_str());
    ++printedRawRows;
  }
  String lowerRow = row;
  lowerRow.toLowerCase();
  String cells[8];
  int cellCount = 0;
  int searchFrom = 0;
  while (cellCount < 8)
  {
    const int cellStart = lowerRow.indexOf("<td", searchFrom);
    if (cellStart < 0)
    {
      break;
    }
    const int contentStart = lowerRow.indexOf('>', cellStart);
    if (contentStart < 0)
    {
      break;
    }
    int cellEnd = lowerRow.indexOf("<td", contentStart + 1);
    const int rowEnd = lowerRow.length();
    if (cellEnd < 0 || cellEnd > rowEnd)
    {
      cellEnd = rowEnd;
    }
    cells[cellCount++] = cleanLebanonCell(row.substring(contentStart + 1, cellEnd));
    searchFrom = cellEnd;
  }
  static int printedCellLayouts = 0;
  bool hasContent = false;
  for (int index = 0; index < cellCount; ++index)
  {
    if (cells[index].length() > 0)
    {
      hasContent = true;
      break;
    }
  }
  if (hasContent && printedCellLayouts < 3)
  {
    Serial.printf("Incident row cells: %d\n", cellCount);
    for (int index = 0; index < cellCount; ++index)
    {
      Serial.printf("  cell[%d]=[%s]\n", index, cells[index].c_str());
    }
    ++printedCellLayouts;
  }
  if (cellCount < 3)
  {
    return false;
  }

  int hour = 0;
  int minute = 0;
  int second = 0;
  int day = 0;
  int month = 0;
  int year = 0;
  int timeCell = -1;
  int dateCell = -1;
  for (int index = 0; index < cellCount; ++index)
  {
    int valueOne = 0;
    int valueTwo = 0;
    int valueThree = 0;
    if (timeCell < 0 && sscanf(cells[index].c_str(), "%d:%d:%d",
                               &valueOne, &valueTwo, &valueThree) == 3)
    {
      hour = valueOne;
      minute = valueTwo;
      second = valueThree;
      timeCell = index;
    }
    if (dateCell < 0 && sscanf(cells[index].c_str(), "%d-%d-%d",
                               &valueOne, &valueTwo, &valueThree) == 3)
    {
      day = valueOne;
      month = valueTwo;
      year = valueThree;
      dateCell = index;
    }
  }
  if (timeCell < 0 || dateCell < 0 || dateCell <= timeCell)
  {
    return false;
  }
  struct tm localEvent = {};
  localEvent.tm_year = 2000 + year - 1900;
  localEvent.tm_mon = month - 1;
  localEvent.tm_mday = day;
  localEvent.tm_hour = hour;
  localEvent.tm_min = minute;
  localEvent.tm_sec = second;
  localEvent.tm_isdst = -1;
  eventEpoch = mktime(&localEvent);
  if (eventEpoch <= 0)
  {
    return false;
  }

  int descriptionCell = -1;
  for (int index = dateCell + 1; index < cellCount; ++index)
  {
    if (cells[index].indexOf(',') >= 0)
    {
      descriptionCell = index;
      break;
    }
  }
  if (descriptionCell < 0)
  {
    return false;
  }
  String description = cells[descriptionCell];
  String responder;
  const int responderMarker = description.lastIndexOf("EMS-Box");
  if (responderMarker >= 0)
  {
    const int responderStart = description.indexOf(' ', responderMarker + 7);
    if (responderStart >= 0)
    {
      responder = description.substring(responderStart + 1);
    }
  }
  const int firstSeparator = description.indexOf(',');
  if (firstSeparator < 0)
  {
    return false;
  }
  const String street = description.substring(0, firstSeparator);
  String locationAndCall = description.substring(firstSeparator + 1);
  const char* locationSuffixes[] = {" Township", " Borough", " City of Lebanon"};
  int locationEnd = -1;
  int suffixLength = 0;
  for (const char* suffix : locationSuffixes)
  {
    const int suffixStart = locationAndCall.indexOf(suffix);
    if (suffixStart >= 0 && (locationEnd < 0 || suffixStart < locationEnd))
    {
      locationEnd = suffixStart;
      suffixLength = strlen(suffix);
    }
  }
  if (locationEnd < 0)
  {
    return false;
  }
  const String township = locationAndCall.substring(0, locationEnd + suffixLength);
  String title = locationAndCall.substring(locationEnd + suffixLength);
  const int fireBox = title.indexOf("Fire-Box");
  String assignedUnits;
  if (fireBox >= 0)
  {
    String beforeFireBox = title.substring(0, fireBox);
    beforeFireBox.trim();
    const int unitSeparator = beforeFireBox.lastIndexOf(' ');
    if (unitSeparator >= 0 && unitSeparator + 1 < beforeFireBox.length())
    {
      const String candidate = beforeFireBox.substring(unitSeparator + 1);
      bool hasDigit = false;
      for (int index = 0; index < candidate.length(); ++index)
      {
        if (candidate[index] >= '0' && candidate[index] <= '9')
        {
          hasDigit = true;
          break;
        }
      }
      if (hasDigit)
      {
        assignedUnits = candidate;
        title = beforeFireBox.substring(0, unitSeparator);
      }
    }
    else
    {
      title = beforeFireBox;
    }
  }
  title.trim();
  String upperTitle = title;
  upperTitle.toUpperCase();
  if (title.length() == 0 || upperTitle == "TONE")
  {
    return false;
  }

  copyIncidentText(incident.title, sizeof(incident.title), title);
  copyIncidentText(incident.township, sizeof(incident.township), township);
  copyIncidentText(incident.street, sizeof(incident.street), street);
  copyIncidentText(incident.unit, sizeof(incident.unit), assignedUnits);
  normalizeUnitText(incident.unit, sizeof(incident.unit));
  copyIncidentText(incident.company, sizeof(incident.company), responder);
  incident.color = incidentColor(title);
  incident.minutes = static_cast<int>(difftime(time(nullptr), eventEpoch) / 60);
  incident.eventEpoch = eventEpoch;
  return true;
}

String incidentKey(const Incident& incident)
{
  String key = incident.street;
  key += '|';
  key += incident.township;
  return key;
}

void mergeAssignedUnits(char* destination, size_t destinationSize, const char* additions)
{
  String existing = destination;
  String remaining = additions;
  while (remaining.length() > 0)
  {
    const int separator = remaining.indexOf('\n');
    String unit = separator >= 0 ? remaining.substring(0, separator) : remaining;
    unit.trim();
    if (unit.length() > 0 && existing.indexOf(unit) < 0)
    {
      if (existing.length() > 0)
      {
        existing += '\n';
      }
      existing += unit;
    }
    remaining = separator >= 0 ? remaining.substring(separator + 1) : String();
    remaining.trim();
  }
  existing.toCharArray(destination, destinationSize);
}

bool fetchLiveIncidents()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  if (!client.connect(INCIDENT_FEED_HOST, 443))
  {
    Serial.println("Incident feed: HTTPS connection failed");
    return false;
  }

  client.print("GET ");
  client.print(INCIDENT_FEED_PATH);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(INCIDENT_FEED_HOST);
  client.println("User-Agent: Mozilla/5.0");
  client.println("Accept: text/html");
  client.println("Accept-Encoding: identity");
  client.println("Connection: close");
  client.println();

  const unsigned long responseStart = millis();
  while (!client.available() && client.connected() && millis() - responseStart < 10000)
  {
    delay(10);
  }

  if (!client.available())
  {
    Serial.println("Incident feed: response timeout");
    client.stop();
    return false;
  }

  const String statusLine = client.readStringUntil('\n');
  Serial.print("Incident feed status: ");
  Serial.println(statusLine);
  if (statusLine.indexOf(" 200 ") < 0)
  {
    Serial.print("Incident feed: ");
    Serial.println(statusLine);
    client.stop();
    return false;
  }

  while (client.connected())
  {
    const String headerLine = client.readStringUntil('\n');
    Serial.print("Incident feed header: ");
    Serial.println(headerLine);
    if (headerLine == "\r" || headerLine.length() == 0)
    {
      break;
    }
  }

  String response;
  response.reserve(12000);
  unsigned long lastResponseByte = millis();
  while (millis() - lastResponseByte < 3000)
  {
    if (client.available())
    {
      while (client.available())
      {
        response += static_cast<char>(client.read());
      }
      lastResponseByte = millis();
    }
    else
    {
      delay(10);
    }
  }
  client.stop();
  const int lowerRowMarker = response.indexOf("<tr");
  const int upperRowMarker = response.indexOf("<TR");
  const int lowerCellMarker = response.indexOf("<td");
  const int upperCellMarker = response.indexOf("<TD");
  Serial.printf("Incident feed response bytes: %d, tr=%d, td=%d\n",
                response.length(), lowerRowMarker >= 0 ? lowerRowMarker : upperRowMarker,
                lowerCellMarker >= 0 ? lowerCellMarker : upperCellMarker);
  const int timeMarker = response.indexOf(':');
  if (timeMarker >= 0)
  {
    const int previewStart = max(0, timeMarker - 120);
    const int responseLength = static_cast<int>(response.length());
    const int previewEnd = responseLength < timeMarker + 380 ? responseLength : timeMarker + 380;
    String preview = response.substring(previewStart, previewEnd);
    Serial.printf("Incident response preview: [%s]\n", preview.c_str());
  }
  Serial.printf("Incident columns: COL2=%d COL3=%d COL4=%d COL7=%d\n",
                response.indexOf("COL2"), response.indexOf("COL3"),
                response.indexOf("COL4"), response.indexOf("COL7"));

  static Incident liveIncidents[MAX_DISPLAY_INCIDENTS];
  int liveCount = 0;
  int totalCount = 0;
  int respondingCount = 0;
  int accidentCount = 0;
  int fireCount = 0;
  int medicalCount = 0;
  static String incidentKeys[MAX_DISPLAY_INCIDENTS];
  int uniqueCount = 0;
  int rowCount = 0;
  int parsedRowCount = 0;
  bool printedIncidentRow = false;
  int searchFrom = 0;
  while (true)
  {
    const int itemStart = searchFrom;
    int itemEnd = response.indexOf("</tr", itemStart);
    const int uppercaseEnd = response.indexOf("</TR", itemStart);
    if (itemEnd < 0 || (uppercaseEnd >= 0 && uppercaseEnd < itemEnd))
    {
      itemEnd = uppercaseEnd;
    }
    if (itemEnd < 0)
    {
      break;
    }

    if (!printedIncidentRow)
    {
      String rawRow = response.substring(itemStart, itemEnd);
      String rawRowLower = rawRow;
      rawRowLower.toLowerCase();
      if (rawRowLower.indexOf("<hr") < 0)
      {
        if (rawRow.length() > 500)
        {
          rawRow.remove(500);
        }
        Serial.printf("Incident first data row: [%s]\n", rawRow.c_str());
        printedIncidentRow = true;
      }
    }

    static Incident parsed;
    static time_t eventEpoch;
    parsed = {};
    eventEpoch = 0;
    ++rowCount;
    if (parseLebanonHtmlRow(response.substring(itemStart, itemEnd), parsed, eventEpoch))
    {
      ++parsedRowCount;
      const long ageSeconds = static_cast<long>(difftime(time(nullptr), eventEpoch));
      if (ageSeconds < 0 || ageSeconds > INCIDENT_LOOKBACK_SECONDS)
      {
        searchFrom = itemEnd + 5;
        continue;
      }
      const String key = incidentKey(parsed);
      int duplicateIndex = -1;
      for (int index = 0; index < uniqueCount; ++index)
      {
        if (incidentKeys[index] == key)
        {
          duplicateIndex = index;
          break;
        }
      }
      if (duplicateIndex >= 0)
      {
        mergeAssignedUnits(liveIncidents[duplicateIndex].unit,
                           sizeof(liveIncidents[duplicateIndex].unit), parsed.unit);
        if (liveIncidents[duplicateIndex].company[0] == '\0' && parsed.company[0] != '\0')
        {
          copyIncidentText(liveIncidents[duplicateIndex].company,
                           sizeof(liveIncidents[duplicateIndex].company), parsed.company);
        }
        searchFrom = itemEnd + 5;
        continue;
      }
      if (uniqueCount < MAX_DISPLAY_INCIDENTS)
      {
        incidentKeys[uniqueCount++] = key;
      }
      if (liveCount < MAX_DISPLAY_INCIDENTS)
      {
        liveIncidents[liveCount++] = parsed;
      }
    }
    searchFrom = itemEnd + 5;
  }

  totalCount = liveCount;
  for (int index = 0; index < liveCount; ++index)
  {
    respondingCount += countAssignedUnits(liveIncidents[index].unit);
    if (isVehicleAccident(liveIncidents[index].title))
    {
      ++accidentCount;
    }
    if (isFireIncident(liveIncidents[index].title))
    {
      ++fireCount;
    }
    if (isMedicalIncident(liveIncidents[index].title))
    {
      ++medicalCount;
    }
  }

  if (liveCount == 0)
  {
    Serial.printf("Incident feed: no usable incidents (rows=%d parsed=%d)\n",
                  rowCount, parsedRowCount);
    return false;
  }

  for (int index = 0; index < liveCount; ++index)
  {
    incidents[index] = liveIncidents[index];
  }
  incidentCount = liveCount;
  activeIncidentCount = totalCount;
  respondingUnitCount = respondingCount;
  accidentIncidentCount = accidentCount;
  fireIncidentCount = fireCount;
  medicalIncidentCount = medicalCount;
  Serial.printf("Incident feed: loaded %d live incidents\n", incidentCount);
  Serial.printf("Incident totals: active=%d units=%d accidents=%d\n",
                activeIncidentCount, respondingUnitCount, accidentIncidentCount);
  Serial.printf("Incident types: fire=%d medical=%d\n", fireIncidentCount, medicalIncidentCount);
  feedHealthy = true;
  lastSuccessfulFeedEpoch = time(nullptr);
  return true;
}

void fetchLiveIncidentsTask(void*)
{
  const bool fetched = fetchLiveIncidents();
  feedHealthy = fetched;
  Serial.println(fetched ? "Incident fetch task complete" : "Incident fetch task failed");
  incidentFetchComplete = true;
  vTaskDelete(nullptr);
}

bool startIncidentFetch()
{
  incidentFetchComplete = false;
  const BaseType_t result = xTaskCreate(fetchLiveIncidentsTask, "incidentFetch", 8192,
                                        nullptr, 1, nullptr);
  if (result != pdPASS)
  {
    Serial.println("Incident fetch task could not start");
    incidentFetchComplete = true;
    return false;
  }
  while (!incidentFetchComplete)
  {
    delay(10);
  }
  return true;
}

void drawLebanonCountyOutline()
{
  const int outline[][2] = {
    {9, 5}, {12, 7}, {15, 10}, {18, 12}, {21, 15},
    {24, 17}, {27, 19}, {22, 21}, {17, 22}, {14, 23},
    {11, 24}, {7, 27}, {5, 20}, {3, 13}, {2, 10},
    {2, 10}, {4, 8}, {7, 6}, {9, 5}
  };
  const int pointCount = sizeof(outline) / sizeof(outline[0]);

  for (int y = 0; y <= 28; ++y)
  {
    int left = 100;
    int right = -1;

    for (int point = 0; point < pointCount - 1; ++point)
    {
      const int x1 = outline[point][0];
      const int y1 = outline[point][1];
      const int x2 = outline[point + 1][0];
      const int y2 = outline[point + 1][1];

      if ((y >= y1 && y < y2) || (y >= y2 && y < y1))
      {
        const int x = x1 + (y - y1) * (x2 - x1) / (y2 - y1);
        left = min(left, x);
        right = max(right, x);
      }
    }

    if (right >= left)
    {
      display.drawLine(COUNTY_OUTLINE_X + left, COUNTY_OUTLINE_Y + y,
                       COUNTY_OUTLINE_X + right, COUNTY_OUTLINE_Y + y, COLOR_DARK_BLUE);
    }
  }
}

void drawWifiSignalBars()
{
  const int barX = COUNTY_OUTLINE_X + 39;
  const int barBottom = COUNTY_OUTLINE_Y + 24;
  const int barWidth = 3;
  const int barGap = 1;
  int bars = 0;

  if (WiFi.status() == WL_CONNECTED)
  {
    const int rssi = WiFi.RSSI();
    bars = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -80 ? 2 : 1;
  }

  for (int index = 0; index < 4; ++index)
  {
    const int height = 3 + index * 3;
    const uint16_t color = index < bars ? COLOR_CYAN : COLOR_DARK_GRAY;
    display.fillRect(barX + index * (barWidth + barGap),
                     barBottom - height, barWidth, height, color);
  }
}

void drawRotationButton()
{
  display.fillRoundRect(ROTATION_BUTTON_X, ROTATION_BUTTON_Y,
                        ROTATION_BUTTON_W, ROTATION_BUTTON_H, 4, COLOR_HEADER);
  display.drawRoundRect(ROTATION_BUTTON_X, ROTATION_BUTTON_Y,
                        ROTATION_BUTTON_W, ROTATION_BUTTON_H, 4, COLOR_CYAN);

  if (incidentRotationPaused)
  {
    const int left = ROTATION_BUTTON_X + 12;
    display.fillTriangle(left, ROTATION_BUTTON_Y + 7,
               left, ROTATION_BUTTON_Y + 17,
               left + 8, ROTATION_BUTTON_Y + 12, COLOR_CYAN);
  }
  else
  {
    display.fillRect(ROTATION_BUTTON_X + 12, ROTATION_BUTTON_Y + 7, 3, 10, COLOR_CYAN);
    display.fillRect(ROTATION_BUTTON_X + 18, ROTATION_BUTTON_Y + 7, 3, 10, COLOR_CYAN);
  }
}

uint16_t touchX = 0;
uint16_t touchY = 0;
unsigned long lastIncidentCycle = 0;
int incidentOffset = 0;

bool matchesIncidentFilter(const Incident& incident)
{
  switch (incidentFilter)
  {
    case IncidentFilter::Fire:
      return isFireIncident(incident.title);
    case IncidentFilter::Medical:
      return isMedicalIncident(incident.title);
    case IncidentFilter::Vehicle:
      return isVehicleAccident(incident.title);
    case IncidentFilter::All:
    default:
      return true;
  }
}

int nextFilteredIncidentIndex(int currentIndex)
{
  for (int offset = 1; offset <= incidentCount; ++offset)
  {
    const int candidate = (currentIndex + offset) % incidentCount;
    if (matchesIncidentFilter(incidents[candidate]))
    {
      return candidate;
    }
  }
  return currentIndex;
}

int firstFilteredIncidentIndex()
{
  for (int index = 0; index < incidentCount; ++index)
  {
    if (matchesIncidentFilter(incidents[index]))
    {
      return index;
    }
  }
  return -1;
}

bool hasFilteredIncidents()
{
  return firstFilteredIncidentIndex() >= 0;
}

void selectIncidentFilter(IncidentFilter filter)
{
  incidentFilter = incidentFilter == filter ? IncidentFilter::All : filter;
  incidentOffset = firstFilteredIncidentIndex();
  if (incidentOffset < 0)
  {
    incidentOffset = 0;
  }
  lastIncidentCycle = millis();
}
bool clockSynced = false;
const int WIFI_SETUP_VERSION = 2;

void markWifiConfigured()
{
  Preferences preferences;
  preferences.begin("wifi", false);
  preferences.putBool("configured", true);
  preferences.end();
  Serial.println("Wi-Fi credentials saved");
}

void drawWifiSetupScreen()
{
  display.fillScreen(COLOR_BG);
  display.fillRect(0, 0, display.width(), 30, COLOR_HEADER);
  display.setTextColor(COLOR_WHITE, COLOR_HEADER);
  display.setTextSize(1);
  display.setCursor(10, 10);
  display.print("Lebanon Fire & EMS LIVE");
  display.setCursor(11, 10);
  display.print("Lebanon Fire & EMS LIVE");

  display.setTextColor(COLOR_WHITE, COLOR_BG);
  display.setTextSize(2);
  display.setCursor(22, 62);
  display.print("Wi-Fi setup");

  display.setTextSize(1);
  display.setCursor(22, 98);
  display.print("Connect your phone's Wi-Fi to:");
  display.setTextColor(COLOR_CYAN, COLOR_BG);
  display.setTextSize(2);
  display.setCursor(22, 118);
  display.print("LEBANON-FIRE-EMS SETUP");

  display.setTextColor(COLOR_WHITE, COLOR_BG);
  display.setTextSize(1);
  display.setCursor(22, 154);
  display.print("A setup page will open");
  display.setCursor(22, 170);
  display.print("to choose your Wi-Fi.");
  display.setCursor(22, 186);
  display.print("Use a 2.4 GHz Wi-Fi network.");
}

bool loadTouchCalibration()
{
  uint16_t calibration[8];
  Preferences preferences;
  preferences.begin(TOUCH_PREFERENCES_NAMESPACE, true);
  const size_t calibrationSize = preferences.getBytes(TOUCH_CALIBRATION_KEY,
                                                      calibration, sizeof(calibration));
  preferences.end();
  if (calibrationSize != sizeof(calibration))
  {
    return false;
  }

  display.setTouchCalibrate(calibration);
  return true;
}

bool shouldCalibrateTouch()
{
  display.fillScreen(COLOR_BG);
  display.setTextColor(COLOR_WHITE, COLOR_BG);
  display.setTextSize(1);
  display.setCursor(28, 84);
  display.print("Hold screen to calibrate");
  display.setCursor(28, 102);
  display.print("or wait for dashboard");

  const unsigned long promptStart = millis();
  while (millis() - promptStart < 3000)
  {
    uint16_t rawX = 0;
    uint16_t rawY = 0;
    if (display.getTouch(&rawX, &rawY))
    {
      const unsigned long holdStart = millis();
      while (millis() - holdStart < 1200)
      {
        if (!display.getTouch(&rawX, &rawY))
        {
          return false;
        }
        delay(25);
      }
      return true;
    }
    delay(25);
  }
  return false;
}

void calibrateTouchAtBoot()
{
  display.fillScreen(COLOR_BG);
  display.setTextColor(COLOR_WHITE, COLOR_BG);
  display.setTextSize(2);
  display.setCursor(28, 70);
  display.print("Touch calibration");
  display.setTextSize(1);
  display.setCursor(28, 105);
  display.print("Touch each target as it appears");

  uint16_t calibration[8] = {};
  display.calibrateTouch(calibration, COLOR_CYAN, COLOR_BG, 10);

  Preferences preferences;
  preferences.begin(TOUCH_PREFERENCES_NAMESPACE, false);
  preferences.putBytes(TOUCH_CALIBRATION_KEY, calibration, sizeof(calibration));
  preferences.end();
  display.setTouchCalibrate(calibration);
  display.fillScreen(COLOR_BG);
}

void connectAndSyncTime()
{
  drawWifiSetupScreen();

  Serial.println("Starting Wi-Fi connection...");
  WiFiManager wifiManager;
  wifiManager.setConnectTimeout(15);
  wifiManager.setConfigPortalTimeout(0);
  wifiManager.setSaveConfigCallback(markWifiConfigured);

  const bool connected = wifiManager.autoConnect("LEBANON-FIRE-EMS SETUP");

  if (connected && WiFi.status() == WL_CONNECTED)
  {
    Serial.print("Wi-Fi SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("Wi-Fi IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Wi-Fi RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("Wi-Fi mode: ");
    Serial.println(WiFi.getMode());
    configTzTime(TIME_ZONE, NTP_SERVER, NTP_SERVER_BACKUP, NTP_SERVER_SECOND_BACKUP);

    struct tm localTime;
    clockSynced = false;
    for (int attempt = 0; attempt < 3 && !clockSynced; ++attempt)
    {
      clockSynced = getLocalTime(&localTime, 5000);
      if (!clockSynced)
      {
        Serial.printf("NTP sync attempt %d failed\n", attempt + 1);
      }
    }

    if (clockSynced)
    {
      Serial.printf("Time synchronized: %04d-%02d-%02d %02d:%02d:%02d\n",
                    localTime.tm_year + 1900, localTime.tm_mon + 1,
                    localTime.tm_mday, localTime.tm_hour, localTime.tm_min,
                    localTime.tm_sec);
      Serial.printf("Time zone: U.S. Eastern (%s), DST=%s\n", TIME_ZONE,
                    localTime.tm_isdst > 0 ? "active" : "inactive");
    }
    else
    {
      Serial.println("NTP sync failed; displaying uptime until time is available");
    }
  }
  else
  {
    Serial.println("Wi-Fi setup portal active: LEBANON-FIRE-EMS SETUP");
    Serial.print("Setup portal IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

void drawMetricCard(int x, int y, int w, int h,
                    const char* labelLineOne, const char* labelLineTwo,
                    const char* value, uint16_t accentColor)
{
  display.fillRoundRect(x, y, w, h, 8, COLOR_CARD);
  display.fillRect(x + 6, y + 4, w - 12, 5, accentColor);

  display.setTextColor(0x0000, COLOR_CARD);
  display.setTextSize(1);
  const int firstLabelWidth = display.textWidth(labelLineOne);
  display.setCursor(x + (w - firstLabelWidth) / 2, y + 13);
  display.print(labelLineOne);

  const int secondLabelWidth = display.textWidth(labelLineTwo);
  display.setCursor(x + (w - secondLabelWidth) / 2, y + 22);
  display.print(labelLineTwo);

  display.setTextSize(2);
  const int valueWidth = display.textWidth(value);
  display.setCursor(x + (w - valueWidth) / 2, y + 34);
  display.print(value);
}

void drawActiveCard(int x, int y)
{
  display.fillRoundRect(x, y, 150, 52, 8, COLOR_CARD);
  display.fillRect(x + 6, y + 4, 138, 4, COLOR_RED);
  display.fillRect(x + 75, y + 17, 1, 29, COLOR_MUTED);

  display.setTextColor(0x0000, COLOR_CARD);
  display.setTextSize(1);
  const int activeWidth = display.textWidth("ACTIVE");
    display.setCursor(x + (150 - activeWidth) / 2, y + 9);
  display.print("ACTIVE");
    const char* filterText = incidentFilter == IncidentFilter::Fire
             ? "Fire only"
             : incidentFilter == IncidentFilter::Medical
               ? "Medical only"
               : incidentFilter == IncidentFilter::Vehicle
                 ? "Vehicle only"
                 : "All";
    const int filterWidth = display.textWidth(filterText);
    display.setCursor(x + (150 - filterWidth) / 2, y + 18);
    display.print(filterText);
  const int fireWidth = display.textWidth("Fire");
    display.setCursor(x + (75 - fireWidth) / 2 - 4, y + 25);
  display.print("Fire");
  const int medicalWidth = display.textWidth("Medical");
    display.setCursor(x + 75 + (75 - medicalWidth) / 2 + 8, y + 25);
  display.print("Medical");

  display.setTextSize(2);
  const int fireCountWidth = display.textWidth(String(fireIncidentCount).c_str());
    display.setCursor(x + (75 - fireCountWidth) / 2 - 4, y + 34);
  display.print(fireIncidentCount);
  const int medicalCountWidth = display.textWidth(String(medicalIncidentCount).c_str());
    display.setCursor(x + 75 + (75 - medicalCountWidth) / 2 + 8, y + 34);
  display.print(medicalIncidentCount);
}

void drawClippedText(const char* text, int maxWidth)
{
  String clipped = text;
  const bool truncated = display.textWidth(clipped.c_str()) > maxWidth;
  while (clipped.length() > 3 && display.textWidth((clipped + "...").c_str()) > maxWidth)
  {
    clipped.remove(clipped.length() - 1);
  }
  if (truncated)
  {
    clipped += "...";
  }
  display.print(clipped);
}

void drawUnitLines(const char* units, int x, int y, int maxWidth)
{
  String remaining = units;
  for (int line = 0; line < 2 && remaining.length() > 0; ++line)
  {
    String lineText;
    while (remaining.length() > 0)
    {
      const int separator = remaining.indexOf('\n');
      String unit = separator >= 0 ? remaining.substring(0, separator) : remaining;
      unit.trim();
      const String candidate = lineText.length() == 0 ? unit : lineText + " / " + unit;

      if (lineText.length() > 0 && display.textWidth(candidate.c_str()) > maxWidth)
      {
        break;
      }

      lineText = candidate;
      remaining = separator >= 0 ? remaining.substring(separator + 1) : String();
      remaining.trim();
    }

    display.setCursor(x, y + line * 10);
    if (remaining.length() > 0 && line == 1)
    {
      drawClippedText((lineText + "...").c_str(), maxWidth);
    }
    else
    {
      display.print(lineText);
    }
  }
}

void drawIncidentCard(int index, int x, int y)
{
  const Incident& incident = incidents[index];
  const int cardWidth = 300;
  const int cardHeight = 108;
  time_t newestEpoch = 0;
  for (int incidentIndex = 0; incidentIndex < incidentCount; ++incidentIndex)
  {
    newestEpoch = max(newestEpoch, incidents[incidentIndex].eventEpoch);
  }
  const bool newestIncident = incident.eventEpoch > 0 && incident.eventEpoch == newestEpoch;
  char ageText[24];
  const long ageSeconds = incident.eventEpoch > 0
                              ? static_cast<long>(difftime(time(nullptr), incident.eventEpoch))
                              : 0;
  if (newestIncident)
  {
    snprintf(ageText, sizeof(ageText), "NEWEST");
  }
  else if (ageSeconds < 60)
  {
    snprintf(ageText, sizeof(ageText), "Just now");
  }
  else if (ageSeconds < 3600)
  {
    snprintf(ageText, sizeof(ageText), "%ldm Ago", ageSeconds / 60);
  }
  else
  {
    snprintf(ageText, sizeof(ageText), "%ldh %ldm Ago", ageSeconds / 3600,
             (ageSeconds % 3600) / 60);
  }

  display.fillRoundRect(x, y, cardWidth, cardHeight, 8, COLOR_CARD);
  const bool fireBanner = isFireIncident(incident.title);
  const bool vehicleAccidentBanner = isVehicleAccident(incident.title);
  const bool classOneBanner = isClassOneIncident(incident.title);
  const bool medicalBanner = isMedicalEmergency(incident.title);
  const int bannerWidth = fireBanner || vehicleAccidentBanner || classOneBanner ? 5 : medicalBanner ? 2 : 1;
  const uint16_t borderColor = fireBanner || classOneBanner ? COLOR_DARK_RED : incident.color;
  for (int border = 0; border < bannerWidth; ++border)
  {
    display.drawRoundRect(x + border, y + border,
                          cardWidth - border * 2, cardHeight - border * 2,
                          8 - border, borderColor);
  }

  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(1);
  display.setCursor(x + 14, y + 13);
  drawClippedText(incident.title, 190);
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  const int ageWidth = display.textWidth(ageText);
  display.setCursor(x + cardWidth - ageWidth - 12, y + 13);
  display.print(ageText);

  display.setTextColor(0x0000, COLOR_CARD);
  display.setCursor(x + 14, y + 37);
  display.print("LOCATION: ");
  drawClippedText(incident.township, 218);

  display.setCursor(x + 14, y + 53);
  drawClippedText(incident.street, 238);

  display.setCursor(x + 14, y + 72);
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.print("UNITS: ");
  display.setTextColor(0x0000, COLOR_CARD);
  const int unitsX = x + 58;
  display.setCursor(unitsX, y + 72);
  drawUnitLines(incident.unit, unitsX, y + 72, 242);

  display.setTextColor(0x0000, COLOR_CARD);
  display.setCursor(x + 14, y + 94);
  display.print("COMPANY: ");
  drawClippedText(incident.company, 224);
}

void drawUnitsHelpPopup()
{
  const int popupX = 20;
  const int popupY = 42;
  const int popupWidth = 280;
  const int popupHeight = 154;
  display.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CARD);
  display.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CYAN);
  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(2);
  display.setCursor(popupX + 16, popupY + 14);
  display.print("UNIT CODES");
  display.setTextSize(1);
  display.setCursor(popupX + 16, popupY + 44);
  display.print("MU / M   Medic unit");
  display.setCursor(popupX + 16, popupY + 60);
  display.print("E       Engine");
  display.setCursor(popupX + 16, popupY + 76);
  display.print("R       Rescue");
  display.setCursor(popupX + 150, popupY + 44);
  display.print("L       Ladder");
  display.setCursor(popupX + 150, popupY + 60);
  display.print("SQ      Squad");
  display.setCursor(popupX + 150, popupY + 76);
  display.print("STA     Station");
  display.setCursor(popupX + 16, popupY + 100);
  display.print("INT     Intercept medic");
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.setCursor(popupX + 16, popupY + 130);
  display.print("Tap anywhere to close");
}

void drawWifiInfoPopup()
{
  const int popupX = 35;
  const int popupY = 69;
  const int popupWidth = 250;
  const int popupHeight = 102;
  display.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CARD);
  display.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CYAN);
  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(2);
  display.setCursor(popupX + 16, popupY + 14);
  display.print("WI-FI STATUS");
  display.setTextSize(1);
  if (WiFi.status() == WL_CONNECTED)
  {
    char rssiText[24];
    snprintf(rssiText, sizeof(rssiText), "Signal: %d dBm", WiFi.RSSI());
    display.setCursor(popupX + 16, popupY + 48);
    display.print("Connected");
    display.setCursor(popupX + 16, popupY + 65);
    display.print(rssiText);
  }
  else
  {
    display.setCursor(popupX + 16, popupY + 48);
    display.print("Not connected");
  }
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.setCursor(popupX + 16, popupY + 84);
  display.print("Tap anywhere to close");
}

void drawCompanyDirectory()
{
  const int companyCount = sizeof(fireCompanies) / sizeof(fireCompanies[0]);
  const int pageCount = (companyCount + COMPANIES_PER_PAGE - 1) / COMPANIES_PER_PAGE;
  const int firstCompany = companyDirectoryPage * COMPANIES_PER_PAGE;
  display.fillScreen(COLOR_BG);
  display.fillRect(0, 0, display.width(), 30, COLOR_HEADER);
  display.setTextColor(COLOR_WHITE, COLOR_HEADER);
  display.setTextSize(1);
  display.setCursor(12, 10);
  display.print("LEBANON FIRE COMPANIES");
  char pageText[8];
  snprintf(pageText, sizeof(pageText), "%d/%d", companyDirectoryPage + 1, pageCount);
  const int pageWidth = display.textWidth(pageText);
  display.setCursor(270 - pageWidth, 10);
  display.print(pageText);
  display.drawRoundRect(286, 3, 27, 24, 4, COLOR_CYAN);
  display.setCursor(296, 10);
  display.print("X");

  display.setTextColor(COLOR_WHITE, COLOR_BG);
  for (int row = 0; row < COMPANIES_PER_PAGE && firstCompany + row < companyCount; ++row)
  {
    const int y = 43 + row * 18;
    const FireCompany& company = fireCompanies[firstCompany + row];
    display.setCursor(22, y);
    display.print(company.station);
    display.setCursor(54, y);
    drawClippedText(company.name, 235);
  }

  display.drawRoundRect(20, 196, 45, 28, 4, COLOR_CYAN);
  display.drawRoundRect(255, 196, 45, 28, 4, COLOR_CYAN);
  display.setTextColor(COLOR_CYAN, COLOR_BG);
  display.setTextSize(2);
  display.setCursor(36, 202);
  display.print("<");
  display.setCursor(272, 202);
  display.print(">");
}

void drawNoFilteredIncidents(int x, int y)
{
  const char* message = incidentFilter == IncidentFilter::Vehicle
                            ? "No vehicle accidents"
                            : incidentFilter == IncidentFilter::Fire
                                  ? "No fire incidents"
                                  : "No medical incidents";
  display.fillRoundRect(x, y, 300, 108, 8, COLOR_CARD);
  display.drawRoundRect(x, y, 300, 108, 8, COLOR_DARK_GRAY);
  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(2);
  const int messageWidth = display.textWidth(message);
  display.setCursor(x + (300 - messageWidth) / 2, y + 35);
  display.print(message);
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.setTextSize(1);
  const char* detail = "Tap the selected filter again for all";
  const int detailWidth = display.textWidth(detail);
  display.setCursor(x + (300 - detailWidth) / 2, y + 70);
  display.print(detail);
}

void drawDashboard(bool fullRedraw = true)
{
  struct tm localTime;
  const bool hasCurrentTime = clockSynced && getLocalTime(&localTime, 0);
  const unsigned long epochSeconds = millis() / 1000UL;
  const int minutes = hasCurrentTime ? localTime.tm_min : (epochSeconds / 60) % 60;
  const int hours = hasCurrentTime ? localTime.tm_hour : (epochSeconds / 3600) % 24;
  char dateText[9] = "--/--/--";
  char respondingText[8];
  char accidentText[8];

  snprintf(respondingText, sizeof(respondingText), "%d", respondingUnitCount);
  snprintf(accidentText, sizeof(accidentText), "%d", accidentIncidentCount);

  if (hasCurrentTime)
  {
    strftime(dateText, sizeof(dateText), "%m/%d/%y", &localTime);
  }

  display.startWrite();
  if (fullRedraw)
  {
    display.fillScreen(COLOR_BG);
    display.fillRect(0, 0, display.width(), 30, COLOR_HEADER);
    display.setTextColor(COLOR_WHITE, COLOR_HEADER);
    display.setTextSize(1);
    display.setCursor(10, 10);
    display.print("Lebanon Fire & EMS LIVE");
    display.setCursor(11, 10);
    display.print("Lebanon Fire & EMS LIVE");
  }
  else
  {
    display.fillRect(260, 0, 60, 30, COLOR_HEADER);
  }

  display.setTextColor(COLOR_CYAN, COLOR_HEADER);
  display.setTextSize(1);
  display.setCursor(280, 10);
  display.print(hours < 10 ? "0" : "");
  display.print(hours);
  display.print(":");
  display.print(minutes < 10 ? "0" : "");
  display.print(minutes);

  display.setCursor(268, 20);
  display.print(dateText);

  if (fullRedraw)
  {
    drawLebanonCountyOutline();
  }
  else
  {
    display.fillRect(225, 0, 30, 30, COLOR_HEADER);
  }
  drawWifiSignalBars();
  drawRotationButton();

  drawActiveCard(10, 40);
  drawMetricCard(170, 40, 65, 52, "Responding", "Units", respondingText, COLOR_DARK_BLUE);
  drawMetricCard(245, 40, 65, 52, "Vehicle", "Accidents", accidentText, COLOR_AMBER);

  if (fullRedraw)
  {
    display.fillRoundRect(10, 100, 300, 18, 5, 0x1A1A);
    display.setTextColor(COLOR_WHITE, 0x1A1A);
    display.setTextSize(1);
          const char* incidentHeader = "LEBANON COUNTY INCIDENTS";
    display.setCursor(18, 104);
    drawClippedText(incidentHeader, 195);
  }
  else
  {
    display.fillRect(225, 100, 85, 18, 0x1A1A);
  }

  char feedStatus[20];
  if (feedHealthy && lastSuccessfulFeedEpoch > 0)
  {
    struct tm feedTime;
    localtime_r(&lastSuccessfulFeedEpoch, &feedTime);
    snprintf(feedStatus, sizeof(feedStatus), "Feed OK %02d:%02d",
             feedTime.tm_hour, feedTime.tm_min);
  }
  else
  {
    snprintf(feedStatus, sizeof(feedStatus), "Feed Error");
  }
  display.setTextSize(1);
  display.setTextColor(COLOR_CYAN, 0x1A1A);
  const int feedStatusWidth = display.textWidth(feedStatus);
  display.setCursor(310 - feedStatusWidth, 104);
  display.print(feedStatus);

  if (incidentFilter != IncidentFilter::All && !hasFilteredIncidents())
  {
    drawNoFilteredIncidents(10, 126);
  }
  else
  {
    drawIncidentCard(incidentOffset, 10, 126);
  }
  if (unitsHelpVisible)
  {
    drawUnitsHelpPopup();
  }
  if (wifiInfoVisible)
  {
    drawWifiInfoPopup();
  }
  if (companyDirectoryVisible)
  {
    drawCompanyDirectory();
  }

  display.endWrite();
}

void refreshDashboardOnTouch()
{
  static bool wasTouched = false;
  static uint16_t touchStartX = 0;
  static uint16_t touchStartY = 0;

  if (display.getTouch(&touchX, &touchY))
  {
    if (!wasTouched)
    {
      touchStartX = touchX;
      touchStartY = touchY;
      if (unitsHelpVisible)
      {
        unitsHelpVisible = false;
        incidentRotationPaused = false;
        drawDashboard();
      }
      else if (wifiInfoVisible)
      {
        wifiInfoVisible = false;
        incidentRotationPaused = false;
        drawDashboard();
      }
      else if (companyDirectoryVisible)
      {
        const int companyCount = sizeof(fireCompanies) / sizeof(fireCompanies[0]);
        const int pageCount = (companyCount + COMPANIES_PER_PAGE - 1) / COMPANIES_PER_PAGE;
        if (touchX >= 286 && touchY < 30)
        {
          companyDirectoryVisible = false;
          incidentRotationPaused = false;
          drawDashboard();
        }
        else if (touchX >= 20 && touchX < 65 && touchY >= 196)
        {
          companyDirectoryPage = companyDirectoryPage == 0 ? pageCount - 1 : companyDirectoryPage - 1;
          drawCompanyDirectory();
        }
        else if (touchX >= 255 && touchX < 300 && touchY >= 196)
        {
          companyDirectoryPage = (companyDirectoryPage + 1) % pageCount;
          drawCompanyDirectory();
        }
      }
      else if (touchX >= COUNTY_OUTLINE_X + 37 && touchX < COUNTY_OUTLINE_X + 57 &&
               touchY >= COUNTY_OUTLINE_Y && touchY < COUNTY_OUTLINE_Y + 30)
      {
        wifiInfoVisible = true;
        incidentRotationPaused = true;
        drawDashboard();
      }
      else if (touchX >= COUNTY_OUTLINE_X && touchX < COUNTY_OUTLINE_X + 30 &&
               touchY >= COUNTY_OUTLINE_Y && touchY < COUNTY_OUTLINE_Y + 30)
      {
        companyDirectoryVisible = true;
        companyDirectoryPage = 0;
        incidentRotationPaused = true;
        drawCompanyDirectory();
      }
      else if (touchX >= 18 && touchX < 302 && touchY >= 184 && touchY < 216)
      {
        unitsHelpVisible = true;
        incidentRotationPaused = true;
        drawDashboard();
      }
      else if (touchX >= 10 && touchX < 85 && touchY >= 40 && touchY < 92)
      {
        selectIncidentFilter(IncidentFilter::Fire);
        drawDashboard();
      }
      else if (touchX >= 85 && touchX < 160 && touchY >= 40 && touchY < 92)
      {
        selectIncidentFilter(IncidentFilter::Medical);
        drawDashboard();
      }
      else if (touchX >= 245 && touchX < 310 && touchY >= 40 && touchY < 92)
      {
        selectIncidentFilter(IncidentFilter::Vehicle);
        drawDashboard();
      }
      else if (touchX >= ROTATION_BUTTON_X &&
          touchX < ROTATION_BUTTON_X + ROTATION_BUTTON_W &&
          touchY >= ROTATION_BUTTON_Y &&
          touchY < ROTATION_BUTTON_Y + ROTATION_BUTTON_H)
      {
        incidentRotationPaused = !incidentRotationPaused;
        lastIncidentCycle = millis();
        display.startWrite();
        drawRotationButton();
        display.endWrite();
        Serial.println(incidentRotationPaused ? "Incident rotation paused" : "Incident rotation resumed");
      }
    }
    Serial.printf("Touch: X=%d Y=%d\n", touchX, touchY);
    wasTouched = true;
    return;
  }

  if (wasTouched)
  {
    wasTouched = false;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("Fire & EMS Incident Dashboard starting...");

  display.init();
  display.setColorDepth(16);
  display.setRotation(1);
  if (shouldCalibrateTouch())
  {
    calibrateTouchAtBoot();
  }
  else
  {
    loadTouchCalibration();
  }
  connectAndSyncTime();
  startIncidentFetch();
  lastIncidentFeedUpdate = millis();
  drawDashboard();

  Serial.println("Dashboard initialized");
}

void loop()
{
  refreshDashboardOnTouch();

  if (!incidentRotationPaused && millis() - lastIncidentCycle >= INCIDENT_CYCLE_MS)
  {
    lastIncidentCycle = millis();
    incidentOffset = nextFilteredIncidentIndex(incidentOffset);
    drawDashboard(false);
  }

  if (millis() - lastIncidentFeedUpdate >= INCIDENT_FEED_REFRESH_MS)
  {
    lastIncidentFeedUpdate = millis();
    startIncidentFetch();
    if (incidentCount > 0 && incidents[0].eventEpoch > 0)
    {
      incidentOffset = firstFilteredIncidentIndex();
      drawDashboard(false);
    }
  }

  delay(50);
}


