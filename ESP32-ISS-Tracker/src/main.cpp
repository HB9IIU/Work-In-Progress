#include "config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <Sgp4.h>
#include <PNGdec.h> // Include the PNG decoder library
// https://notisrac.github.io/FileToCArray/
#include "ISSsplashImage.h" // Image is stored here in an 8-bit array
#include "worldMap.h"       // Image is stored here in an 8-bit array
#include "fancySplashImage.h"
#include "expedition72.h"
#include <HB9IIU7segFonts.h> //  https://rop.nl/truetype2gfx/   https://fontforge.org/en-US/
#include <WebSocketsServer.h>

// TFT setup
TFT_eSPI tft = TFT_eSPI();
// PNG decoder instance
PNG png;
// SGP4 decoder instance
Sgp4 sat;
// WebSocket server on port 4235
int websocketPort = 4235;
WebSocketsServer webSocket = WebSocketsServer(websocketPort);

// Wi-Fi credentials
const char *ssid = "NO WIFI FOR YOU!!!";
const char *password = "Nestle2010Nestle";

// Configuration
const unsigned long TLEupdateFrequencyInHours = 1; // Update threshold in seconds (10 hours)

// Global variables
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // Default NTP client configuration
Preferences preferences;                                // Preferences for storing TLE data
bool timeInitialized = false;
int totalTimeOffset = 0;
char SatNameCharArray[20];
char TLEline1CharArray[70];
char TLEline2CharArray[70];
bool newTFTprintPage; // set to true if cls TFT before printing
String TLEelementsAge;
unsigned long unixtime;
int orbitNumber;
bool refreshBecauseReturningFromOtherPage = false;
// Next pass information
unsigned long nextPassStart = 0;
unsigned long nextPassEnd = 0;
double nextPassAOSAzimuth = 0;
double nextPassLOSAzimuth = 0;
double nextPassMaxTCA = 0;
double nextPassPerigee = 0;
unsigned long nextPassCulminationTime = 0;
float culminationAzimuth = 0.0; // Variable to store azimuth at culmination
unsigned long passDuration = 0; // Duration of the pass in seconds
unsigned long passMinutes = 0;  // Pass duration in minutes
unsigned long passSeconds = 0;  // Remaining seconds after minutes
bool speakerisON = true;
//____________________________________________________________________
void displaySysInfo();
void initializeTFT();
void logWithBoxFrame(const String &message);
void displayWelcomeMessage(int duration);
void TFTprint(const String &text, uint16_t color = TFT_WHITE);
void getTimezoneData();
String processTLE(String line1charArray);
void retrieveTLEelementsForSatellite(int catalogNumber);
void getTLEelements(int catalogNumber);
bool syncTimeFromNTP(bool displayOnTFT);
void connectToWiFi();
void initializeBuzzer();
void displaySplashScreen(int duration);
void pngDraw(PNGDRAW *pDraw);
void displayMainPage();
void getOrbitNumber(time_t t);
void updateBigClock(bool refreshBecauseReturningFromOtherPage = false);
void displayElevation(float number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage);
void displayAzimuth(float number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage);
void displayLatitude(float number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage);
void displayLongitude(float number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage);
void displayAltitude(int number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage);
void displayDistance(int number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage);
void displayLTLEage(int y, bool refreshBecauseReturningFromOtherPage);
void displayClassicClock();
void display7segmentClock(int xOffset, int yOffset, uint16_t textColor, bool refreshBecauseReturningFromOtherPage);
void displayOrbitNumber(int number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage);
void calculateNextPass();
String formatTimeOnly(unsigned long epochTime, bool isLocal);
String formatDate(unsigned long epochTime, bool isLocal);
void displayNextPassTime(unsigned long durationInSec, int x, int y, uint16_t color, bool refresh);
String displayRemainingVisibleTimeinMMSS(int delta);
String formatTime(unsigned long epochTime, bool isLocal);
void beepsBeforeVisibility();
void displayAzElPlotPage();
void displayPolarPlotPage();
String formatWithSeparator(unsigned long number);
void displayTableNext10Passes();
void displayMapWithMultiPasses();
void displayEquirectangularWorlsMap();
void displayPExpedition72image();
void calibrateTFTscreen();
void checkAndApplyTFTCalibrationData(bool recalibrate);
void drawSpeakerON(int x, int y);
void drawSpeakerOFF(int x, int y);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void startWebSocket();
//---------------------------------------------------------------------------------------------------
void displaySysInfo()
{
    logWithBoxFrame("System Info");
    unsigned long sketchSize = ESP.getSketchSize();
    unsigned long freeSketchSpace = ESP.getFreeSketchSpace();
    unsigned long totalFlashSize = sketchSize + freeSketchSpace;

    float percentageUsed = (static_cast<float>(sketchSize) / totalFlashSize) * 100;

    Serial.println("Sketch Size: " + String(sketchSize) + " bytes");
    Serial.println("Free Sketch Space: " + String(freeSketchSpace) + " bytes");
    Serial.println("Flash Chip Size: " + String(totalFlashSize / 1024 / 1024) + " MB");
    Serial.println("Flash Frequency: " + String(ESP.getFlashChipSpeed() / 1000000) + " MHz");
    Serial.printf("Flash Usage: %.1f%% used\n", percentageUsed);
}
void initializeTFT()
{
    pinMode(TFT_BLP, OUTPUT); // for TFT backlight
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK); // Clears the screen to black
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
}
void displayWelcomeMessage(int duration)
{
    tft.fillScreen(TFT_BLACK);

    // Define content strings
    String title = "HB9IIU Live Sat Tracker";
    String slogan = "Explore the Skies. Track with Precision.";
    String version = "Version: " + String(VERSION_NUMBER);
    String versionDate = "(" + String(VERSION_DATE) + ")";
    String disclaimer = "Disclaimer: Use at your own risk!";

    // Set font and calculate screen width
    tft.setTextFont(4); // Adjust font size as needed
    tft.setTextSize(1); // Text size
    int screenWidth = tft.width();

    // Define colors
    uint16_t titleColor = TFT_GOLD;
    uint16_t sloganColor = TFT_SKYBLUE;
    uint16_t versionColor = TFT_GREEN;
    uint16_t dateColor = TFT_WHITE; // Different color for the date
    uint16_t disclaimerColor = TFT_RED;

    // Calculate line height
    int lineHeight = tft.fontHeight() + 15; // Add padding between lines
    int yPosition = 50;                     // Starting Y position for the title

    // Display title
    tft.setTextColor(titleColor, TFT_BLACK);
    tft.drawString(title, (screenWidth - tft.textWidth(title)) / 2, yPosition);

    // Display slogan
    yPosition += lineHeight;
    tft.setTextColor(sloganColor, TFT_BLACK);
    tft.drawString(slogan, (screenWidth - tft.textWidth(slogan)) / 2, yPosition);

    // Display version information
    yPosition += lineHeight + 10; // Extra spacing before the version
    tft.setTextColor(versionColor, TFT_BLACK);
    tft.drawString(version, (screenWidth - tft.textWidth(version)) / 2, yPosition);

    // Display date on a new line
    yPosition += lineHeight; // Move to the next line
    tft.setTextColor(dateColor, TFT_BLACK);
    tft.drawString(versionDate, (screenWidth - tft.textWidth(versionDate)) / 2, yPosition);

    // Display disclaimer
    yPosition += lineHeight * 2;
    tft.setTextColor(disclaimerColor, TFT_BLACK);
    tft.drawString(disclaimer, (screenWidth - tft.textWidth(disclaimer)) / 2, yPosition);

    int width = max(max(title.length(), version.length()), disclaimer.length()) + 4; // Adjust frame width

    // Print top border
    Serial.println();
    Serial.print("+");
    for (int i = 0; i < width; i++)
        Serial.print("-");
    Serial.println("+");

    // Print empty line for padding
    Serial.print("|");
    for (int i = 0; i < width; i++)
        Serial.print(" ");
    Serial.println("|");

    // Function to print a line with centered text
    auto printCenteredLine = [&](String text)
    {
        int padding = (width - text.length()) / 2;
        Serial.print("|");
        for (int i = 0; i < padding; i++)
            Serial.print(" ");
        Serial.print(text);
        for (int i = 0; i < width - text.length() - padding; i++)
            Serial.print(" ");
        Serial.println("|");
    };

    // Print title, version, and disclaimer with centered alignment
    printCenteredLine(title);
    printCenteredLine(version);
    printCenteredLine(disclaimer);

    // Print empty line for padding
    Serial.print("|");
    for (int i = 0; i < width; i++)
        Serial.print(" ");
    Serial.println("|");

    // Print bottom border
    Serial.print("+");
    for (int i = 0; i < width; i++)
        Serial.print("-");
    Serial.println("+");
    Serial.println();
    newTFTprintPage = true;
    delay(duration);
    tft.fillScreen(TFT_BLACK);
}
void TFTprint(const String &text, uint16_t color)
{

    // Constants for font and screen dimensions
    const int FONT_HEIGHT = 28;    // Approximate height of FONT4; adjust if necessary
    const int SCREEN_HEIGHT = 320; // Screen height for 480x320 TFT
    static int currentLine = 0;    // Track the current line position

    if (newTFTprintPage)
    {
        currentLine = 0;
        tft.fillScreen(TFT_BLACK);
        newTFTprintPage = false;
    }
    // Initialize TFT settings
    tft.setTextFont(4);      // Use FONT4
    tft.setTextColor(color); // Set text color with a transparent background

    // Calculate the y-position for the current line
    int yPos = currentLine * FONT_HEIGHT;

    // If we've reached the bottom of the screen, reset to the top
    if (yPos + FONT_HEIGHT > SCREEN_HEIGHT)
    {
        tft.fillScreen(TFT_BLACK); // Clear the screen
        currentLine = 0;           // Reset line counter
        yPos = 0;
    }

    // Set cursor to the beginning of the line and print text
    tft.setCursor(0, yPos); // Start at x = 0 for each line
    tft.print(text);

    // Move to the next line for subsequent calls
    currentLine++;
}
void logWithBoxFrame(const String &message)
{
    int messageLength = message.length();
    String topBottomBorder = "+";
    for (int i = 0; i < messageLength + 2; i++)
    { // Add 2 for padding around the message
        topBottomBorder += "-";
    }
    topBottomBorder += "+";

    String framedMessage = "| " + message + " |";

    Serial.println(topBottomBorder);
    Serial.println(framedMessage);
    Serial.println(topBottomBorder);
}
void getTimezoneData()
{
    logWithBoxFrame("Trying to retriev observer timezone details via timezonedb.com");
    newTFTprintPage = true;
    TFTprint("Retrieving timezone data...", TFT_YELLOW);
    // Variables to store offset data
    int timezoneOffset; // Offset in seconds, will be updated during setup for your location
    int dstOffset;      // DST offset in seconds, will be updated during setup for your location
    // Build the Time Zone Database URL
    String timezoneUrl = "http://api.timezonedb.com/v2.1/get-time-zone?key=" + String(TIMEZONE_API_KEY) + "&format=json&by=position&lat=" + String(OBSERVER_LATITUDE) + "&lng=" + String(OBSERVER_LONGITUDE);
    // Serial.println(timezoneUrl); // Debug URL

    int maxRetries = 3; // Number of retry attempts
    String timeZoneName;
    bool success = false;

    for (int attempt = 1; attempt <= maxRetries; attempt++)
    {
        Serial.printf("Attempt %d of %d to retrieve timezone data...\n", attempt, maxRetries);

        if (WiFi.status() == WL_CONNECTED)
        {
            HTTPClient http;
            http.begin(timezoneUrl);
            http.setTimeout(5000); // Set HTTP timeout to 5 seconds
            int httpCode = http.GET();

            if (httpCode == 200)
            {
                String payload = http.getString();
                DynamicJsonDocument doc(1024);
                DeserializationError error = deserializeJson(doc, payload);

                if (!error)
                {
                    timezoneOffset = doc["gmtOffset"];        // Offset in seconds
                    dstOffset = (doc["dst"] == 1) ? 3600 : 0; // DST offset (1 hour if active)
                    totalTimeOffset = timezoneOffset + dstOffset;

                    timeZoneName = doc["zoneName"].as<String>();
                    success = true;

                    Serial.println("Timezone data retrieved successfully!");
                    Serial.print("Timezone Name: ");
                    Serial.println(timeZoneName);
                    Serial.print("Timezone Offset (seconds): ");
                    Serial.println(timezoneOffset);
                    Serial.print("DST Offset (seconds): ");
                    Serial.println(dstOffset);
                    Serial.print("Total time offset (seconds): ");
                    Serial.println(totalTimeOffset);
                    TFTprint("Timezone data retrieved successfully!", TFT_GREEN);
                    TFTprint("");

                    TFTprint("Timezone Name: " + String(timeZoneName), TFT_WHITE);
                    TFTprint("");

                    TFTprint("Timezone Offset (seconds): " + String(timezoneOffset), TFT_WHITE);
                    TFTprint("");

                    TFTprint("DST Offset (seconds): " + String(dstOffset), TFT_WHITE);
                    TFTprint("");

                    TFTprint("Total time Offset (seconds): " + String(totalTimeOffset), TFT_WHITE);

                    break; // Exit the loop on success
                }
                else
                {
                    Serial.println("JSON deserialization error.");
                    TFTprint("JSON deserialization error.", TFT_RED);
                }
            }
            else
            {
                Serial.printf("HTTP error: %d\n", httpCode);
                TFTprint("Connection error to timezonedb.com", TFT_RED);
                TFTprint("Retrying....", TFT_YELLOW);
            }

            http.end(); // Close HTTP request
        }
        else
        {
            Serial.println("Not connected to WiFi.");
            TFTprint("Not connected to WiFi.", TFT_RED);
            WiFi.reconnect();
        }

        // Retry delay with exponential backoff
        delay(2000 * attempt); // Increase delay with each retry
    }

    if (!success)
    {
        Serial.println("Failed to retrieve timezone data after maximum retries.");
        TFTprint("Failed to retrieve timezone data.", TFT_RED);
        TFTprint("Check your API key in config.h", TFT_YELLOW);
        delay(5000);
        TFTprint("Rebooting....", TFT_YELLOW);
        delay(2000);
        ESP.restart();
    }
}
bool syncTimeFromNTP(bool displayOnTFT)
{
    logWithBoxFrame("Connecting to NTP Servers for Time Synchronization");
    newTFTprintPage = true;
    if (displayOnTFT)
    {
        TFTprint("Time Synchronization via NTP Servers", TFT_YELLOW);
    }
    int ntpRetries = 10;
    const char *ntpServers[] = {
        "216.239.35.0",    // Google NTP
        "132.163.96.1",    // NIST NTP
        "162.159.200.123", // Cloudflare NTP
        "129.6.15.28",     // Pool server
        "193.67.79.202"    // Time server (Europe)
    };

    const char *serverNames[] = {
        "time.google.com",     // Google NTP
        "time.nist.gov",       // NIST NTP
        "time.cloudflare.com", // Cloudflare NTP
        "time.windows.com",    // Pool server
        "time.europe.com"      // Time server (Europe)
    };

    const int numServers = sizeof(ntpServers) / sizeof(ntpServers[0]);
    bool success = false;

    for (int i = 0; i < numServers; i++)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            logWithBoxFrame("Wi-Fi is disconnected. Retrying...");
            WiFi.reconnect();
            delay(2000);
            continue;
        }

        Serial.printf("Trying NTP server: %s (%s)\n", serverNames[i], ntpServers[i]);

        timeClient.end();                            // Stop the current client
        timeClient.setPoolServerName(ntpServers[i]); // Set the new NTP server
        timeClient.begin();                          // Re-initialize the NTP client

        unsigned long retryStartTime = millis(); // Capture retry start time

        for (int retries = 0; retries < ntpRetries; retries++)
        {
            if (timeClient.update())
            {
                unsigned long unixtime = timeClient.getEpochTime();
                if (unixtime > 1000000000) // Ensure the time is valid
                {
                    Serial.println("NTP time updated successfully.");
                    Serial.printf("Unix Time: %lu\n", unixtime);

                    // Convert UNIX time to human-readable format
                    time_t rawTime = (time_t)unixtime;      // Explicitly cast to time_t
                    struct tm *timeInfo = gmtime(&rawTime); // Convert to UTC time structure
                    char formattedTime[20];                 // Buffer for formatted time
                    strftime(formattedTime, sizeof(formattedTime), "%H:%M:%S %d:%m:%y", timeInfo);
                    Serial.printf("Formatted Time: %s\n", formattedTime);
                    Serial.println();

                    if (displayOnTFT)
                    {
                        // Adjust for local time using the offset
                        time_t rawTime = (time_t)unixtime;            // Cast unixtime to time_t
                        time_t localTime = rawTime + totalTimeOffset; // Add offset for local time

                        // Convert UTC time to struct tm
                        struct tm *utcTimeInfo = gmtime(&rawTime); // UTC time

                        // Convert local time to struct tm
                        struct tm localTimeInfo;
                        gmtime_r(&localTime, &localTimeInfo); // Use gmtime_r for thread-safe local time conversion

                        // Get UTC time in HH:MM:SS format
                        char utcTimeBuffer[10];
                        strftime(utcTimeBuffer, sizeof(utcTimeBuffer), "%H:%M:%S", utcTimeInfo);
                        String utcTimeString = String(utcTimeBuffer);

                        // Get local time in HH:MM:SS format
                        char localTimeBuffer[10];
                        strftime(localTimeBuffer, sizeof(localTimeBuffer), "%H:%M:%S", &localTimeInfo);
                        String localTimeString = String(localTimeBuffer);

                        // Get local date in "Friday, 3 November 205" format
                        char dateBuffer[30];
                        strftime(dateBuffer, sizeof(dateBuffer), "%A, %d %B %Y", &localTimeInfo);
                        String dateString = String(dateBuffer);

                        // Print to TFT
                        TFTprint("NTP time updated successfully!", TFT_GREEN);
                        TFTprint("");
                        TFTprint("UTC Time: " + utcTimeString, TFT_WHITE); // UTC time in HH:MM:SS
                        TFTprint("");
                        TFTprint("Local Time: " + localTimeString, TFT_WHITE); // Local time in HH:MM:SS
                        TFTprint("");
                        TFTprint("Local Date: " + dateString, TFT_WHITE); // Local date in desired format
                    }
                    timeInitialized = true;
                    return true; // Exit immediately on success
                }
            }
            else
            {
                unsigned long elapsedMillis = millis() - retryStartTime;
                unsigned long elapsedSeconds = elapsedMillis / 1000;
                Serial.printf("Retry %d: NTP update failed... Elapsed Time: %lus\n", retries + 1, elapsedSeconds);
                delay(2000); // Delay before retrying
            }
        }
    }

    logWithBoxFrame("All NTP servers failed. Retrying indefinitely...");
    delay(5000);  // Wait before trying again
    return false; // Indicate failure
}
void displayPExpedition72image()
{
    // https://notisrac.github.io/FileToCArray/
    int16_t rc = png.openFLASH((uint8_t *)expedition72, sizeof(expedition72), pngDraw);
    if (rc == PNG_SUCCESS)
    {
        Serial.println("Successfully opened png file");
        Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
        tft.startWrite();
        uint32_t dt = millis();
        rc = png.decode(NULL, 0);
        Serial.print(millis() - dt);
        Serial.println("ms");
        tft.endWrite();
    }
}
String processTLE(String line1charArray)
{
    time_t tleEpochUnix = 0;
    String line1 = String(line1charArray); // Convert char array to String

    // Extract timestamp from Line 1 (Plain Text)
    int epochYear = line1.substring(18, 20).toInt() + 2000; // Convert 2-digit year to full year
    float epochDay = line1.substring(20, 32).toFloat();     // Extract fractional day of the year

    // Convert to Unix timestamp
    struct tm tleTime = {0};
    tleTime.tm_year = epochYear - 1900; // tm_year is years since 1900
    tleTime.tm_mday = 1;                // Start from January 1st
    tleTime.tm_mon = 0;
    tleEpochUnix = mktime(&tleTime) + (unsigned long)((epochDay - 1) * 86400); // Add fractional days

    // Get current time
    time_t currentTime = time(NULL);

    // Calculate TLE age
    unsigned long unixtime;
    unixtime = timeClient.getEpochTime();
    unsigned long ageInSeconds = unixtime - tleEpochUnix;
    unsigned long ageInDays = ageInSeconds / 86400;
    unsigned long remainingSeconds = ageInSeconds % 86400;
    unsigned long ageInHours = remainingSeconds / 3600;
    unsigned long ageInMinutes = (remainingSeconds % 3600) / 60;

    char ageFormatted[50];
    sprintf(ageFormatted, "%lu days, %lu hours, %lu minutes", ageInDays, ageInHours, ageInMinutes);
    Serial.print("TLE Age: ");
    Serial.print(String(ageFormatted));
    unsigned long totalHours = ageInSeconds / 3600;          // Total hours (can exceed 24)
    unsigned long totalMinutes = (ageInSeconds % 3600) / 60; // Remaining minutes
    Serial.printf(" ,i.e. ");
    char ageHHMM[10];                                        // Buffer to hold the formatted string
    sprintf(ageHHMM, "%lu:%02lu", totalHours, totalMinutes); // Format hours and minutes
    Serial.print(String(ageHHMM));
    return String(ageHHMM);
}
void connectToWiFi()
{
    logWithBoxFrame("Trying to connect to Wifi");
    int attempt = 0;
    const int maxAttempts = 5; // Maximum number of attempts for each network
    bool connected = false;

    while (!connected)
    {
        if (attempt < maxAttempts)
        {
            Serial.print("Connecting to primary Wi-Fi: ");
            Serial.println(WIFI_SSID);

            String message = "Connecting to primary Wi-Fi: " + String(WIFI_SSID);
            TFTprint(message, TFT_YELLOW);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        else
        {
            Serial.print("Connecting to alternative Wi-Fi: ");
            Serial.println(WIFI_SSID_ALT);
            TFTprint("");

            String message = "Connecting to alternative Wi-Fi: " + String(WIFI_SSID_ALT);
            TFTprint(message, TFT_YELLOW);
            WiFi.begin(WIFI_SSID_ALT, WIFI_PASSWORD_ALT);
        }

        // Check Wi-Fi connection status
        for (int i = 0; i < 10; i++)
        { // Wait up to 5 seconds
            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println("\nConnected to Wi-Fi.");
                TFTprint("Wi-Fi Connection Successful", TFT_GREEN);

                connected = true;
                break;
            }
            delay(500);
            Serial.print(".");
        }

        // If not connected, try again or switch to the alternative network
        if (!connected)
        {
            WiFi.disconnect();
            attempt++;
            if (attempt == maxAttempts * 2)
            {
                Serial.println("\nFailed to connect to both networks. Retrying...");
                TFTprint("");
                TFTprint("Failed to connect to both networks", TFT_RED);
                TFTprint("");
                TFTprint("Check your credentials in config.h", TFT_YELLOW);
                TFTprint("");
                delay(3000);
                TFTprint("Retrying...", TFT_YELLOW);
                delay(1000);
                tft.fillScreen(TFT_BLACK);
                newTFTprintPage = true;
                attempt = 0; // Reset attempts to retry both networks
            }
            else if (attempt == maxAttempts)
            {
                Serial.println("\nSwitching to alternative network...");
                TFTprint("Switching to alternative network...", TFT_YELLOW);
            }
        }
    }

    // Get Wi-Fi signal strength (RSSI)
    int32_t rssi = WiFi.RSSI();
    Serial.print("Signal Strength: ");
    Serial.print(rssi);
    Serial.println(" dBm");
    TFTprint("");
    TFTprint("Signal Strength: " + String(rssi) + " dBm", TFT_WHITE);
}
void getTLEelements(int catalogNumber)
{
    logWithBoxFrame("Trying to retrieve stored TLE elements from flash memory");
    newTFTprintPage = true;
    TFTprint("Retrieving TLE elements", TFT_YELLOW);
    TFTprint("");

    // Open preferences
    preferences.begin("tle-storage", true);
    int storedCatalogNumber = preferences.getInt("catalogNumber", 0); // Default to 0 if not found
    Serial.print("catalogNumber:  ");
    Serial.println(catalogNumber);
    Serial.print("storedCatalogNumber:  ");
    Serial.println(storedCatalogNumber);
    unsigned long lastRetrievalTime = preferences.getULong("retrievalTime", 0);
    String satelliteName = preferences.getString("satelliteName", "");
    String tleLine1 = preferences.getString("tleLine1", "");
    String tleLine2 = preferences.getString("tleLine2", "");
    preferences.end();

    // First we check if elements were found in oreferences
    if (lastRetrievalTime == 0)
    {
        Serial.println("No elements found in preferences");
        Serial.println("Fetching new elements from celestrak.org");
        TFTprint("No elements found in flash memory", TFT_RED);
        TFTprint("");
        TFTprint("Fetching new elements from celestrak.org", TFT_YELLOW);
        retrieveTLEelementsForSatellite(catalogNumber);
    }

    // The we check if the catalog number has changed
    if (storedCatalogNumber != catalogNumber)
    {
        Serial.println("Satellite has changed. Clearing old preferences...");
        Serial.println("Fetching new elements from celestrak.org");
        TFTprint("You changed Satellite!!!", TFT_RED);
        TFTprint("");
        TFTprint("Fetching new elements from celestrak.org", TFT_YELLOW);
        retrieveTLEelementsForSatellite(catalogNumber);
        return;
    }

    // Now we check if new call should be made to celestrak.org
    // Calculate time since last retrieval
    unsigned long currentTime = timeClient.getEpochTime();
    unsigned long secondsSinceLastRetrieval = currentTime - lastRetrievalTime;

    // Check if TLE data is outdated
    if (secondsSinceLastRetrieval > TLEupdateFrequencyInHours * 3600)
    {
        Serial.println("Last TLE elements downloaded " + String(secondsSinceLastRetrieval / 3600) + " hours ago");
        Serial.println("Fetching new elements from celestrak.org");
        TFTprint("Last TLE elements downloaded " + String(secondsSinceLastRetrieval / 3600) + " hours ago", TFT_WHITE);
        TFTprint("");
        TFTprint("Fetching new elements from celestrak.org", TFT_YELLOW);
        retrieveTLEelementsForSatellite(catalogNumber);
        return;
    }

    Serial.println("Last TLE elements downloaded less than" + String(secondsSinceLastRetrieval / 60) + " min. ago");
    Serial.println("Keeping the ones from flash memory");

    TFTprint("Last TLE elements downloaded " + String(secondsSinceLastRetrieval / 60) + " min ago", TFT_WHITE);
    TFTprint("");
    TFTprint("Keeping the ones from flash memory", TFT_GREEN);

    // format for sgpd4
    satelliteName.toCharArray(SatNameCharArray, sizeof(SatNameCharArray));
    tleLine1.toCharArray(TLEline1CharArray, sizeof(TLEline1CharArray));
    tleLine2.toCharArray(TLEline2CharArray, sizeof(TLEline2CharArray));
}
void initializeBuzzer()
{
    logWithBoxFrame("Initialize LEDC peripheral for tone generation");
    // Initialize LEDC peripheral for tone generation
    ledcSetup(0, 5000, 8);    // Channel 0, 5kHz frequency, 8-bit resolution
    ledcAttachPin(BUZZER, 0); // Attach buzzer pin to channel 0
    digitalWrite(TFT_BLP, HIGH);
    for (int i = 0; i < 3; i++)
    {
        ledcWriteTone(0, 2160); // Play 2.16kHz tone
        delay(100);             // Wait 1 second
        ledcWriteTone(0, 0);    // Stop the tone
        delay(25);              // Wait 0.5 second between repetitions
    }
}
void displaySplashScreen(int duration)
{
    digitalWrite(TFT_BLP, LOW);

    // https://notisrac.github.io/FileToCArray/
    int16_t rc = png.openFLASH((uint8_t *)fancySplash, sizeof(fancySplash), pngDraw);

    if (rc == PNG_SUCCESS)
    {
        logWithBoxFrame("Displaying Splash Screen");
        Serial.println("Successfully opened png file");
        Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
        tft.startWrite();
        uint32_t dt = millis();
        rc = png.decode(NULL, 0);
        Serial.print("Displayed in ");
        Serial.print(millis() - dt);
        Serial.println(" ms");
        tft.endWrite();
    }

    digitalWrite(TFT_BLP, HIGH);

    delay(duration);
}
void pngDraw(PNGDRAW *pDraw)
{
    uint16_t lineBuffer[480];
    png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
    tft.pushImage(0, 0 + pDraw->y, pDraw->iWidth, 1, lineBuffer);
}
void displayUsedElements()
{
    newTFTprintPage = true;
    TLEelementsAge = processTLE(TLEline1CharArray);
    TFTprint("Following Element will be used");
    TFTprint("");
    TFTprint(SatNameCharArray, TFT_YELLOW);
    TFTprint("");
    TFTprint(TLEline1CharArray, TFT_GREEN);
    TFTprint("");
    TFTprint("");
    TFTprint(TLEline2CharArray, TFT_GREEN);
    TFTprint("");
    TFTprint("");
    TFTprint("TLE Elements age (HH:MM): " + TLEelementsAge, TFT_YELLOW);
}
void displayMainPage()
{
    static bool speakerIsDrawn = false;
    if (speakerIsDrawn == false || refreshBecauseReturningFromOtherPage == true)
        if (speakerisON == true)
        {
            drawSpeakerON(226, 130);
        }
        else
        {
            drawSpeakerOFF(226, 130);
        }

    updateBigClock(refreshBecauseReturningFromOtherPage);

    int AZELcolor;
    if (sat.satEl > 3)
    {
        AZELcolor = TFT_GREEN; // Elevation greater than 3 -> Green
    }
    else if (sat.satEl < -3)
    {
        AZELcolor = TFT_RED; // Elevation less than -3 -> Red
    }
    else
    {
        AZELcolor = TFT_YELLOW; // Elevation between -3 and 3 -> Yellow
    }

    displayElevation(sat.satEl, 5 + 30, 116, AZELcolor, refreshBecauseReturningFromOtherPage);
    displayAzimuth(sat.satAz, 303 - 30, 116, AZELcolor, refreshBecauseReturningFromOtherPage);

    int startXmain = 30;
    int startYmain = 200;
    int deltaY = 30;

    displayAltitude(sat.satAlt, 25, startYmain, TFT_GOLD, refreshBecauseReturningFromOtherPage);
    displayDistance(sat.satDist, 25, startYmain + 1 * deltaY, TFT_GOLD, refreshBecauseReturningFromOtherPage);
    displayOrbitNumber(orbitNumber, 25, startYmain + 2 * deltaY, TFT_GOLD, refreshBecauseReturningFromOtherPage);
    displayLatitude(sat.satLat, 320, startYmain, TFT_GOLD, refreshBecauseReturningFromOtherPage);
    displayLongitude(sat.satLon, 320, startYmain + deltaY, TFT_GOLD, refreshBecauseReturningFromOtherPage);
    displayLTLEage(startYmain + 2 * deltaY, refreshBecauseReturningFromOtherPage);

    // Managing the bottom banner
    int lowerBannerY = 295;
    static bool first_time_below = true;
    static bool first_time_above = true;
    if (sat.satEl < 0)

    {
        calculateNextPass(); // only if below horizon, otherwise we get a problem with next pass when visible

        int shifting = 50;
        if (first_time_below == true || refreshBecauseReturningFromOtherPage == true)
        {
            tft.fillRect(0, 295, 480, 50, TFT_BLACK); // clear entire area
            tft.setCursor(shifting, lowerBannerY);
            tft.setTextColor(TFT_CYAN);
            tft.print("Next Pass in ");
            tft.setCursor(tft.textWidth("Next pass in 00:00:00 ") + shifting, lowerBannerY);
            tft.print("at ");
            tft.print(formatTimeOnly(nextPassStart, true));
            displayNextPassTime(nextPassStart - unixtime, shifting, lowerBannerY, TFT_CYAN, refreshBecauseReturningFromOtherPage);
            bool refreshRemainingTime = true;
        }
        tft.setCursor(shifting, lowerBannerY);
        displayNextPassTime(nextPassStart - unixtime, shifting, lowerBannerY, TFT_CYAN, first_time_below);
        first_time_below = false;
        first_time_above = true;
    }

    if (sat.satEl > 0)
    {
        int shifting = 40;
        if (first_time_above == true || refreshBecauseReturningFromOtherPage == true)
        {
            tft.fillRect(0, 295, 480, 50, TFT_BLACK);
            tft.setCursor(shifting, lowerBannerY);
            tft.setTextColor(TFT_CYAN);
            tft.print("Satellite is above horizon for");
            tft.setCursor(shifting, lowerBannerY);
            first_time_above = false;
            refreshBecauseReturningFromOtherPage == false;
        }

        int tmpX = tft.textWidth("Satellite is above horizon for ") + shifting;
        tft.fillRect(tmpX, 295, 480 - shifting, 50, TFT_BLACK);
        tft.setCursor(tmpX, lowerBannerY);
        tft.setTextColor(TFT_CYAN);
        tft.print(displayRemainingVisibleTimeinMMSS(nextPassEnd - unixtime));
        first_time_below = true;
    }
}
void getOrbitNumber(time_t t)
{
    char tempstr[13]; // Temporary string for parsing
    int baselineOrbitNumber;
    int year;
    float epochDay;
    float meanMotion;

    // Extract Revolution Number (Baseline Orbit Number) from TLE Line 2
    char revNumStr[6] = {0};                     // Maximum 5 characters + null terminator
    String tleLine2 = String(TLEline2CharArray); // Convert char array to String
    strncpy(revNumStr, &tleLine2[63], 5);        // Extract columns 63–67
    revNumStr[5] = '\0';                         // Null-terminate
    baselineOrbitNumber = atoi(revNumStr);

    // Extract Epoch Year (columns 19-20 in Line 1)
    String tleLine1 = String(TLEline1CharArray); // Convert char array to String

    strncpy(tempstr, &tleLine1[18], 2); // Extract columns 19-20
    tempstr[2] = '\0';
    int epochYear = atoi(tempstr);
    year = (epochYear < 57) ? (epochYear + 2000) : (epochYear + 1900);

    // Extract Epoch Day (columns 21-32 in Line 1)
    strncpy(tempstr, &tleLine1[20], 12); // Extract columns 21-32
    tempstr[12] = '\0';
    epochDay = atof(tempstr);

    // Extract Mean Motion (columns 53-63 in Line 2)
    strncpy(tempstr, &tleLine2[52], 10); // Extract columns 53-63
    tempstr[10] = '\0';
    meanMotion = atof(tempstr);

    // Convert TLE Epoch to Unix Time
    struct tm tmEpoch = {};
    tmEpoch.tm_year = year - 1900; // tm_year is years since 1900
    tmEpoch.tm_mday = 1;           // Start of the year
    tmEpoch.tm_hour = 0;
    tmEpoch.tm_min = 0;
    tmEpoch.tm_sec = 0;
    time_t tleEpochStart = mktime(&tmEpoch);                  // Start of the year in Unix time
    time_t tleEpoch = tleEpochStart + (epochDay - 1) * 86400; // Add fractional days as seconds

    // Validate input time
    if (t <= tleEpoch)
    {
        Serial.println("Error: Input time is before or equal to TLE Epoch. Check the time data.");
        return;
    }

    // Time Since TLE Epoch
    unsigned long timeSinceEpoch = t - tleEpoch;

    // Convert Time Since Epoch to Days
    float timeSinceEpochDays = timeSinceEpoch / 86400.0;

    // Calculate Orbits Since TLE Epoch
    float orbitsSinceEpoch = timeSinceEpochDays * meanMotion;

    // Update the Global Orbit Number
    orbitNumber = baselineOrbitNumber + (int)ceil(orbitsSinceEpoch);

    // Print the updated Orbit Number
    // Serial.print("Updated Orbit Number: ");
    // Serial.println(orbitNumber);
}
String formatTime(unsigned long epochTime, bool isLocal)
{
    // Adjust the epoch time by adding timezone offset and DST offset if applicable
    time_t t = isLocal ? epochTime + totalTimeOffset : epochTime;
    struct tm *tmInfo = gmtime(&t);
    char buffer[20];
    strftime(buffer, 20, "%d.%m.%y @ %H:%M:%S", tmInfo);
    return String(buffer);
}
void updateBigClock(bool refreshBecauseReturningFromOtherPage)
{
    if (display7DigisStyleClock == true)
    {
        display7segmentClock(26, 92, clockDigitsColor, refreshBecauseReturningFromOtherPage);
        return;
    }
    else
    {
        displayClassicClock();
    }
}
void displayElevation(float number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage)
{
    tft.setTextSize(1);
    tft.setTextFont(7);

    static char previousOutput[6] = "     ";   // Previous state (5 characters + null terminator) 999.9
    static uint16_t previousColor = TFT_GREEN; // Track the last color used
    static bool isInitiated = false;           // Track initialization of static elements
    char output[6] = "     ";                  // Current output (5 characters + null terminator) 999.9

    // Format the number with sign and one decimal place (e.g., "+123.4" or "-123.4")
    char tempBuffer[7];                                       // Temporary buffer to hold the formatted string
    snprintf(tempBuffer, sizeof(tempBuffer), "%.1f", number); // Format number without the sign

    // Dynamically fill the output array from the right
    int len = strlen(tempBuffer); // Length of the formatted number
    int startIndex = 5 - len;     // Calculate starting index for right alignment
    for (int i = 0; i < len; i++)
    {
        output[startIndex + i] = tempBuffer[i]; // Copy characters from tempBuffer to output
    }

    // Positions for characters
    int xPos[] = {0, 32, 64, 96, 117, 124}; // Adjust character width dynamically
    for (int i = 0; i < 6; i++)
        xPos[i] += x;

    // Handle color change or refreshBecauseReturningFromOtherPage logic
    if (color != previousColor)
    {
        // If the color has changed, trigger a full redraw
        refreshBecauseReturningFromOtherPage = true;
        previousColor = color; // Update the last used color
    }

    // Handle refreshBecauseReturningFromOtherPage logic for static elements (decimal point)
    if (!isInitiated || refreshBecauseReturningFromOtherPage)
    {

        // Margins around the text
        int horiMargin = 12;
        int topMargin = 14;
        int bottomMargin = 22;
        // Calculate text dimensions
        int textLength = xPos[5] - x + tft.textWidth("8") + 14;
        ;
        // Total width and height of the rectangle
        int rectWidth = textLength + 2 * horiMargin;
        int rectHeight = tft.fontHeight() + topMargin + bottomMargin;
        // X and Y positions for the rectangle
        int rectX = xPos[0] - horiMargin;
        int rectY = y - topMargin;
        // Draw the rectangle with rounded corners
        tft.drawRoundRect(rectX, rectY, rectWidth, rectHeight, 10, TFT_LIGHTGREY);
        // Draw Elevation label
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.setFreeFont(&FreeSans12pt7b);
        tft.drawString("Elevation", rectX + rectWidth / 2 - tft.textWidth("Elevation") / 2, rectY + rectHeight - 12); // Decimal point is fixed at xPos[4]

        tft.setTextFont(7);
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString(".", xPos[4], y); // Decimal point is fixed at xPos[4]
        tft.setFreeFont(&FreeMonoBold12pt7b);
        tft.drawString("o", xPos[4] + 40, y - 5); // Decimal point is fixed at xPos[4]
        tft.setTextFont(7);

        isInitiated = true;
    }

    // Update only changed characters or redraw all if refreshBecauseReturningFromOtherPage is triggered
    for (int i = 0; i < 6; i++)
    { // Loop through each character of the formatted number
        if (output[i] != previousOutput[i] || refreshBecauseReturningFromOtherPage)
        {
            // Print the previous character in black to erase it
            if (previousOutput[i] != ' ')
            {
                tft.setTextColor(TFT_BLACK, TFT_BLACK);
                tft.drawChar(previousOutput[i], xPos[i], y);
            }

            // Draw the new character
            if (output[i] != ' ')
            {
                tft.setTextColor(color, TFT_BLACK);
                tft.drawChar(output[i], xPos[i], y);
            }

            // Update previous state
            previousOutput[i] = output[i];
        }
    }
}
void displayAzimuth(float number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage)
{
    tft.setTextSize(1);
    tft.setTextFont(7);

    static char previousOutput[6] = "     ";   // Previous state (5 characters + null terminator) 999.9
    static uint16_t previousColor = TFT_GREEN; // Track the last color used
    static bool isInitiated = false;           // Track initialization of static elements
    char output[6] = "     ";                  // Current output (5 characters + null terminator) 999.9

    // Format the number with sign and one decimal place (e.g., "+123.4" or "-123.4")
    char tempBuffer[7];                                       // Temporary buffer to hold the formatted string
    snprintf(tempBuffer, sizeof(tempBuffer), "%.1f", number); // Format number without the sign

    // Dynamically fill the output array from the right
    int len = strlen(tempBuffer); // Length of the formatted number
    int startIndex = 5 - len;     // Calculate starting index for right alignment
    for (int i = 0; i < len; i++)
    {
        output[startIndex + i] = tempBuffer[i]; // Copy characters from tempBuffer to output
    }

    // Positions for characters
    int xPos[] = {0, 32, 64, 96, 117, 124}; // Adjust character width dynamically
    for (int i = 0; i < 6; i++)
        xPos[i] += x;

    // Handle color change or refreshBecauseReturningFromOtherPage logic
    if (color != previousColor)
    {
        // If the color has changed, trigger a full redraw
        refreshBecauseReturningFromOtherPage = true;
        previousColor = color; // Update the last used color
    }

    // Handle refreshBecauseReturningFromOtherPage logic for static elements (decimal point)
    if (!isInitiated || refreshBecauseReturningFromOtherPage)
    {

        // Margins around the text
        int horiMargin = 12;
        int topMargin = 14;
        int bottomMargin = 22;
        // Calculate text dimensions
        int textLength = xPos[5] - x + tft.textWidth("8") + 14;
        ;
        // Total width and height of the rectangle
        int rectWidth = textLength + 2 * horiMargin;
        int rectHeight = tft.fontHeight() + topMargin + bottomMargin;
        // X and Y positions for the rectangle
        int rectX = xPos[0] - horiMargin;
        int rectY = y - topMargin;
        // Draw the rectangle with rounded corners
        tft.drawRoundRect(rectX, rectY, rectWidth, rectHeight, 10, TFT_LIGHTGREY);
        // Draw Azimuth label
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.setFreeFont(&FreeSans12pt7b);
        tft.drawString("Azimuth", rectX + rectWidth / 2 - tft.textWidth("Azimuth") / 2, rectY + rectHeight - 12); // Decimal point is fixed at xPos[4]

        tft.setTextFont(7);
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString(".", xPos[4], y); // Decimal point is fixed at xPos[4]
        tft.setFreeFont(&FreeMonoBold12pt7b);
        tft.drawString("o", xPos[4] + 40, y - 5); // Decimal point is fixed at xPos[4]
        tft.setTextFont(7);

        isInitiated = true;
    }

    // Update only changed characters or redraw all if refreshBecauseReturningFromOtherPage is triggered
    for (int i = 0; i < 6; i++)
    { // Loop through each character of the formatted number
        if (output[i] != previousOutput[i] || refreshBecauseReturningFromOtherPage)
        {
            // Print the previous character in black to erase it
            if (previousOutput[i] != ' ')
            {
                tft.setTextColor(TFT_BLACK, TFT_BLACK);
                tft.drawChar(previousOutput[i], xPos[i], y);
            }

            // Draw the new character
            if (output[i] != ' ')
            {
                tft.setTextColor(color, TFT_BLACK);
                tft.drawChar(output[i], xPos[i], y);
            }

            // Update previous state
            previousOutput[i] = output[i];
        }
    }
}
void displayLatitude(float number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage)
{
    tft.setTextSize(1);
    tft.setTextFont(4);

    static char previousOutput[7] = "      ";  // Previous state (6 characters + null terminator)
    static uint16_t previousColor = TFT_GREEN; // Track the last color used
    static bool isInitiated = false;           // Track initialization of static elements
    char output[7] = "      ";                 // Current output (6 characters + null terminator)

    // Format the number with sign and one decimal place (e.g., "+123.4" or "-123.4")
    char tempBuffer[8];                                        // Temporary buffer to hold the formatted string
    snprintf(tempBuffer, sizeof(tempBuffer), "%+.1f", number); // Format number

    // Dynamically fill the output array from the right
    int len = strlen(tempBuffer); // Length of the formatted number
    int startIndex = 6 - len;     // Calculate starting index for right alignment
    for (int i = 0; i < len; i++)
    {
        output[startIndex + i] = tempBuffer[i]; // Copy characters from tempBuffer to output
    }

    // Positions for characters
    int xPos[] = {0, 14, 28, 42, 56, 63};

    for (int i = 0; i < 6; i++)
        xPos[i] += x;

    // Handle color change or refreshBecauseReturningFromOtherPage logic
    if (color != previousColor)
    {
        // If the color has changed, trigger a full redraw
        refreshBecauseReturningFromOtherPage = true;
        previousColor = color; // Update the last used color
    }

    // Handle refreshBecauseReturningFromOtherPage logic for static elements (decimal point)
    if (!isInitiated || refreshBecauseReturningFromOtherPage)
    {
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString(".", xPos[4], y);         // Decimal point is fixed at xPos[4]
        tft.drawString("deg.", xPos[5] + 20, y); // Unit
        tft.drawString("Lat.", xPos[0] - 45, y); // Unit

        isInitiated = true;
    }

    // Update only changed characters or redraw all if refreshBecauseReturningFromOtherPage is triggered
    for (int i = 0; i < 6; i++)
    { // Loop through each character of the formatted number
        if (output[i] != previousOutput[i] || refreshBecauseReturningFromOtherPage)
        {
            // Print the previous character in black to erase it
            if (previousOutput[i] != ' ')
            {
                tft.setTextColor(TFT_BLACK, TFT_BLACK);
                tft.drawChar(previousOutput[i], xPos[i], y);
            }

            // Draw the new character
            if (output[i] != ' ')
            {
                tft.setTextColor(color, TFT_BLACK);
                tft.drawChar(output[i], xPos[i], y);
            }

            // Update previous state
            previousOutput[i] = output[i];
        }
    }
}
void displayLongitude(float number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage)
{
    tft.setTextSize(1);
    tft.setTextFont(4);

    static char previousOutput[7] = "      ";  // Previous state (6 characters + null terminator)
    static uint16_t previousColor = TFT_GREEN; // Track the last color used
    static bool isInitiated = false;           // Track initialization of static elements
    char output[7] = "      ";                 // Current output (6 characters + null terminator)

    // Format the number with sign and one decimal place (e.g., "+123.4" or "-123.4")
    char tempBuffer[8];                                        // Temporary buffer to hold the formatted string
    snprintf(tempBuffer, sizeof(tempBuffer), "%+.1f", number); // Format number

    // Dynamically fill the output array from the right
    int len = strlen(tempBuffer); // Length of the formatted number
    int startIndex = 6 - len;     // Calculate starting index for right alignment
    for (int i = 0; i < len; i++)
    {
        output[startIndex + i] = tempBuffer[i]; // Copy characters from tempBuffer to output
    }

    // Positions for characters
    int xPos[] = {0, 14, 28, 42, 56, 63};

    for (int i = 0; i < 6; i++)
        xPos[i] += x;

    // Handle color change or refreshBecauseReturningFromOtherPage logic
    if (color != previousColor)
    {
        // If the color has changed, trigger a full redraw
        refreshBecauseReturningFromOtherPage = true;
        previousColor = color; // Update the last used color
    }

    // Handle refreshBecauseReturningFromOtherPage logic for static elements (decimal point)
    if (!isInitiated || refreshBecauseReturningFromOtherPage)
    {
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString(".", xPos[4], y);         // Decimal point is fixed at xPos[4]
        tft.drawString("deg.", xPos[5] + 20, y); // Unit
        tft.drawString("Lon.", xPos[0] - 45, y); // Unit
        isInitiated = true;
    }

    // Update only changed characters or redraw all if refreshBecauseReturningFromOtherPage is triggered
    for (int i = 0; i < 6; i++)
    { // Loop through each character of the formatted number
        if (output[i] != previousOutput[i] || refreshBecauseReturningFromOtherPage)
        {
            // Print the previous character in black to erase it
            if (previousOutput[i] != ' ')
            {
                tft.setTextColor(TFT_BLACK, TFT_BLACK);
                tft.drawChar(previousOutput[i], xPos[i], y);
            }

            // Draw the new character
            if (output[i] != ' ')
            {
                tft.setTextColor(color, TFT_BLACK);
                tft.drawChar(output[i], xPos[i], y);
            }

            // Update previous state
            previousOutput[i] = output[i];
        }
    }
}
void displayAltitude(int number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage)
{
    tft.setTextSize(1);
    tft.setTextFont(4);

    static char previousOutput[7] = "      ";  // Previous state (6 characters + null terminator) 12'456
    static uint16_t previousColor = TFT_GREEN; // Track the last color used
    static bool isInitiated = false;           // Track initialization of static elements
    char output[7] = "      ";                 // Current output (6 characters + null terminator)

    char tempBuffer[7] = ""; // Temporary buffer for formatted number
    int len = 0;             // Length of the formatted number
    int index = 5;           // Start filling from the right (5 characters + null terminator)

    // Format the number with thousands separator
    while (number > 0 || len == 0)
    { // Ensure '0' is handled correctly
        if (len > 0 && len % 3 == 0)
        {
            tempBuffer[index--] = '\''; // Insert the separator
        }
        tempBuffer[index--] = '0' + (number % 10);
        number /= 10;
        len++;
    }

    // Fill the output buffer with spaces for right alignment
    for (int i = 0; i <= index; i++)
    {
        output[i] = ' ';
    }
    // Copy the formatted number into the output buffer
    for (int i = index + 1; i < 6; i++)
    {
        output[i] = tempBuffer[i];
    }

    output[6] = '\0'; // Ensure null termination

    // Positions for characters
    int shift = 8;
    int xPos[] = {0, 14, 28, 42 - shift, 56 - shift, 70 - shift};
    int leftmargin = 110;

    for (int i = 0; i < 6; i++)
        xPos[i] += x + leftmargin;

    // Handle color change or refreshBecauseReturningFromOtherPage logic
    if (color != previousColor)
    {
        // If the color has changed, trigger a full redraw
        refreshBecauseReturningFromOtherPage = true;
        previousColor = color; // Update the last used color
    }

    // Handle refreshBecauseReturningFromOtherPage logic for static elements (decimal point)
    if (!isInitiated || refreshBecauseReturningFromOtherPage)
    {
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString("km", xPos[5] + 24, y);
        tft.drawString("Altitude:", x, y);
        isInitiated = true;
    }

    // Update only changed characters or redraw all if refreshBecauseReturningFromOtherPage is triggered
    for (int i = 0; i < 6; i++)
    { // Loop through each character of the formatted number
        // Serial.print(output[i]);
        // Serial.print(" ");
        if (output[i] != previousOutput[i] || refreshBecauseReturningFromOtherPage)
        {
            // Print the previous character in black to erase it
            if (previousOutput[i] != ' ')
            {
                tft.setTextColor(TFT_BLACK, TFT_BLACK);
                tft.drawChar(previousOutput[i], xPos[i], y);
            }

            // Draw the new character
            if (output[i] != ' ')
            {
                tft.setTextColor(color, TFT_BLACK);
                tft.drawChar(output[i], xPos[i], y);
            }

            // Update previous state
            previousOutput[i] = output[i];
        }
    }
    Serial.println();
}
void displayLTLEage(int y, bool refreshBecauseReturningFromOtherPage)
{
    static bool isInitiated = false;

    if (!isInitiated || refreshBecauseReturningFromOtherPage)
    {
        tft.setTextFont(4);
        tft.setTextSize(1);
        tft.setTextColor(TFT_GOLD);
        tft.setCursor(276, y);
        tft.print("TLE age:");
        tft.setCursor(388, y);
        tft.print(TLEelementsAge);
        isInitiated = true;
    }
}
void displayClassicClock()
{
    int y = 10;
    tft.setTextFont(8);
    tft.setTextSize(1);
    static String previousTime = ""; // Track previous time to update only changed characters
    static bool isPositionCalculated = false;
    static int clockXPosition; // Calculated once to center the clock text
    static int clockWidth;     // Width of the time string in pixels
    // Perform initial calculation of clock width and position if not already done
    if (!isPositionCalculated || refreshBecauseReturningFromOtherPage == true)
    {
        String sampleTime = "00:00:00"; // Sample time format for clock width calculation
        clockWidth = tft.textWidth(sampleTime.c_str());
        clockXPosition = (tft.width() - clockWidth) / 2; // Center x position for the clock
        isPositionCalculated = true;                     // Mark as calculated
        previousTime = "";
    }
    // Apply timezone and DST offsets
    unsigned long localTime = unixtime + totalTimeOffset;
    // Convert to human-readable format
    struct tm *timeinfo = gmtime((time_t *)&localTime); // Use gmtime for seconds since epoch
    char timeStr[9];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
    String currentTime = String(timeStr);

    // Only update characters that have changed
    int xPosition = clockXPosition; // Start at the pre-calculated center position
    for (int i = 0; i < currentTime.length(); i++)
    {
        // If character has changed, update it
        if (i >= previousTime.length() || currentTime[i] != previousTime[i])
        {
            // Clear the previous character area by printing a black background
            tft.setCursor(xPosition, y);
            tft.setTextColor(TFT_BLACK, TFT_BLACK); // Black on black to clear
            tft.print(previousTime[i]);

            // Print the new character with the specified color
            tft.setCursor(xPosition, y);
            tft.setTextColor(clockDigitsColor, TFT_BLACK); // Color on black background
            tft.print(currentTime[i]);
        }
        // Increment xPosition by width of the current character
        xPosition += tft.textWidth(String(currentTime[i]).c_str());
    }
    // Update previousTime to the new time
    previousTime = currentTime;
}
void display7segmentClock(int xOffset, int yOffset, uint16_t textColor, bool refreshBecauseReturningFromOtherPage)
{
    // Static variables to track previous state and colon visibility
    static int previousArray[6] = {-1, -1, -1, -1, -1, -1}; // Initialize previous digit array
    if (refreshBecauseReturningFromOtherPage == true)
    {
        for (int i = 0; i < 6; i++)
        {
            previousArray[i] = -1;
        }
        refreshBecauseReturningFromOtherPage = false;
    }
    static bool colonVisible = true;     // Tracks colon visibility
                                         // Define the TFT_MIDGREY color as a local constant
    const uint16_t TFT_MIDGREY = 0x39a7; // Darker grey https://rgbcolorpicker.com/565
    // uint16_t TFT_MIDGREY = TFT_DARKGREY;
    int gap = 68;
    int gap2 = 20;
    int xCoordinates[6] = {xOffset, xOffset + gap, xOffset + 2 * gap + gap2, xOffset + 3 * gap + gap2, xOffset + 4 * gap + 2 * gap2, xOffset + 5 * gap + 2 * gap2};

    // Set the custom font
    tft.setFreeFont(&HB9IIU7segFonts);

    // Toggle colon visibility every second
    if (unixtime % 1 == 0)
    {
        colonVisible = !colonVisible;
    }

    // Display or hide colons based on colonVisible
    uint16_t colonColor = colonVisible ? textColor : TFT_BLACK;
    tft.setTextColor(colonColor, TFT_BLACK);
    tft.setCursor(xCoordinates[2] - 24, yOffset);
    tft.print(":");
    tft.setCursor(xCoordinates[4] - 24, yOffset);
    tft.print(":");

    // Calculate hours, minutes, and seconds
    unsigned long locatime = unixtime + totalTimeOffset;
    int hours = (locatime % 86400L) / 3600; // Hours since midnight XXXX
    int minutes = (locatime % 3600) / 60;   // Minutes
    int seconds = locatime % 60;            // Seconds

    // Current time digit array
    int timeArray[6] = {
        hours / 10,   // Tens digit of hours
        hours % 10,   // Units digit of hours
        minutes / 10, // Tens digit of minutes
        minutes % 10, // Units digit of minutes
        seconds / 10, // Tens digit of seconds
        seconds % 10  // Units digit of seconds
    };

    // Mapped characters for 0-9
    char mappedChars[10] = {'@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I'};

    // Update only changed digits
    for (int i = 0; i < 6; i++)
    {
        if (timeArray[i] != previousArray[i])
        {
            // Clear the previous digit
            tft.setTextColor(TFT_BLACK, TFT_BLACK);
            tft.setCursor(xCoordinates[i], yOffset);
            tft.print(previousArray[i]);

            // Print the new digit
            tft.setTextColor(textColor, TFT_BLACK);
            tft.setCursor(xCoordinates[i], yOffset);
            tft.print(timeArray[i]);

            // Print the mapped character below the digit, but skip if the mapped character is 'H'
            tft.setTextColor(TFT_MIDGREY, TFT_BLACK);

            char mappedChar = mappedChars[timeArray[i]]; // Get the mapped character
            if (mappedChar != 'H')
            {
                tft.setCursor(xCoordinates[i], yOffset); // Adjust Y offset for character display
                tft.print(mappedChar);
            }
        }
    }

    // Update the previous array
    for (int i = 0; i < 6; i++)
    {
        previousArray[i] = timeArray[i];
    }
}
void displayDistance(int number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage)
{
    tft.setTextSize(1);
    tft.setTextFont(4);

    static char previousOutput[7] = "      ";  // Previous state (6 characters + null terminator) 12'456
    static uint16_t previousColor = TFT_GREEN; // Track the last color used
    static bool isInitiated = false;           // Track initialization of static elements
    char output[7] = "      ";                 // Current output (6 characters + null terminator)

    char tempBuffer[7] = ""; // Temporary buffer for formatted number
    int len = 0;             // Length of the formatted number
    int index = 5;           // Start filling from the right (5 characters + null terminator)

    // Format the number with thousands separator
    while (number > 0 || len == 0)
    { // Ensure '0' is handled correctly
        if (len > 0 && len % 3 == 0)
        {
            tempBuffer[index--] = '\''; // Insert the separator
        }
        tempBuffer[index--] = '0' + (number % 10);
        number /= 10;
        len++;
    }

    // Fill the output buffer with spaces for right alignment
    for (int i = 0; i <= index; i++)
    {
        output[i] = ' ';
    }
    // Copy the formatted number into the output buffer
    for (int i = index + 1; i < 6; i++)
    {
        output[i] = tempBuffer[i];
    }

    output[6] = '\0'; // Ensure null termination

    // Positions for characters
    int shift = 8;
    int xPos[] = {0, 14, 28, 42 - shift, 56 - shift, 70 - shift};
    int leftmargin = 110;

    for (int i = 0; i < 6; i++)
        xPos[i] += x + leftmargin;

    // Handle color change or refreshBecauseReturningFromOtherPage logic
    if (color != previousColor)
    {
        // If the color has changed, trigger a full redraw
        refreshBecauseReturningFromOtherPage = true;
        previousColor = color; // Update the last used color
    }

    // Handle refreshBecauseReturningFromOtherPage logic for static elements (decimal point)
    if (!isInitiated || refreshBecauseReturningFromOtherPage)
    {
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString("km", xPos[5] + 24, y);
        tft.drawString("Distance:", x, y);
        isInitiated = true;
    }

    // Update only changed characters or redraw all if refreshBecauseReturningFromOtherPage is triggered
    for (int i = 0; i < 6; i++)
    { // Loop through each character of the formatted number
        // Serial.print(output[i]);
        // Serial.print(" ");
        if (output[i] != previousOutput[i] || refreshBecauseReturningFromOtherPage)
        {
            // Print the previous character in black to erase it
            if (previousOutput[i] != ' ')
            {
                tft.setTextColor(TFT_BLACK, TFT_BLACK);
                tft.drawChar(previousOutput[i], xPos[i], y);
            }

            // Draw the new character
            if (output[i] != ' ')
            {
                tft.setTextColor(color, TFT_BLACK);
                tft.drawChar(output[i], xPos[i], y);
            }

            // Update previous state
            previousOutput[i] = output[i];
        }
    }
    Serial.println();
}
void displayOrbitNumber(int number, int x, int y, uint16_t color, bool refreshBecauseReturningFromOtherPage)
{
    tft.setTextSize(1);
    tft.setTextFont(4);

    static char previousOutput[7] = "      ";  // Previous state (6 characters + null terminator) 12'456
    static uint16_t previousColor = TFT_GREEN; // Track the last color used
    static bool isInitiated = false;           // Track initialization of static elements
    char output[7] = "      ";                 // Current output (6 characters + null terminator)

    char tempBuffer[7] = ""; // Temporary buffer for formatted number
    int len = 0;             // Length of the formatted number
    int index = 5;           // Start filling from the right (5 characters + null terminator)

    // Format the number with thousands separator
    while (number > 0 || len == 0)
    { // Ensure '0' is handled correctly
        if (len > 0 && len % 3 == 0)
        {
            tempBuffer[index--] = '\''; // Insert the separator
        }
        tempBuffer[index--] = '0' + (number % 10);
        number /= 10;
        len++;
    }

    // Fill the output buffer with spaces for right alignment
    for (int i = 0; i <= index; i++)
    {
        output[i] = ' ';
    }
    // Copy the formatted number into the output buffer
    for (int i = index + 1; i < 6; i++)
    {
        output[i] = tempBuffer[i];
    }

    output[6] = '\0'; // Ensure null termination

    // Positions for characters
    int shift = 8;
    int xPos[] = {0, 14, 28, 42 - shift, 56 - shift, 70 - shift};
    int leftmargin = 110;

    for (int i = 0; i < 6; i++)
        xPos[i] += x + leftmargin;

    // Handle color change or refreshBecauseReturningFromOtherPage logic
    if (color != previousColor)
    {
        // If the color has changed, trigger a full redraw
        refreshBecauseReturningFromOtherPage = true;
        previousColor = color; // Update the last used color
    }

    // Handle refreshBecauseReturningFromOtherPage logic for static elements (decimal point)
    if (!isInitiated || refreshBecauseReturningFromOtherPage)
    {
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString("Orbit #:", x, y);
        isInitiated = true;
    }

    // Update only changed characters or redraw all if refreshBecauseReturningFromOtherPage is triggered
    for (int i = 0; i < 6; i++)
    { // Loop through each character of the formatted number
        // Serial.print(output[i]);
        // Serial.print(" ");
        if (output[i] != previousOutput[i] || refreshBecauseReturningFromOtherPage)
        {
            // Print the previous character in black to erase it
            if (previousOutput[i] != ' ')
            {
                tft.setTextColor(TFT_BLACK, TFT_BLACK);
                tft.drawChar(previousOutput[i], xPos[i], y);
            }

            // Draw the new character
            if (output[i] != ' ')
            {
                tft.setTextColor(color, TFT_BLACK);
                tft.drawChar(output[i], xPos[i], y);
            }

            // Update previous state
            previousOutput[i] = output[i];
        }
    }
    Serial.println();
}
void calculateNextPass()
{
    // Serial.println("Calculating Next Pass...");

    // Reset global variables for the next pass
    nextPassStart = 0;
    nextPassEnd = 0;
    nextPassMaxTCA = 0;
    nextPassCulminationTime = 0;
    nextPassAOSAzimuth = 0;
    nextPassLOSAzimuth = 0;
    culminationAzimuth = 0;
    nextPassPerigee = 0;
    passDuration = 0;
    passMinutes = 0;
    passSeconds = 0;

    passinfo overpass;

    sat.initpredpoint(unixtime + 10 * 60, 0); // adding 10 minutes to ensure that next pass is not in the past (experimental)

    // Find the next pass using up to 100 iterations
    bool passFound = sat.nextpass(&overpass, 100);

    if (passFound)
    {
        int year, month, day, hour, minute;
        double second;
        bool daylightSaving = false;

        invjday(overpass.jdstart, 0, false, year, month, day, hour, minute, second);

        struct tm aosTm = {0};
        aosTm.tm_year = year - 1900;
        aosTm.tm_mon = month - 1;
        aosTm.tm_mday = day;
        aosTm.tm_hour = hour;
        aosTm.tm_min = minute;
        aosTm.tm_sec = (int)second;
        nextPassStart = mktime(&aosTm);

        nextPassAOSAzimuth = overpass.azstart;

        // Convert TCA: Time of Closest Approach
        invjday(overpass.jdmax, 0, false, year, month, day, hour, minute, second);

        struct tm tcaTm = {0};
        tcaTm.tm_year = year - 1900;
        tcaTm.tm_mon = month - 1;
        tcaTm.tm_mday = day;
        tcaTm.tm_hour = hour;
        tcaTm.tm_min = minute;
        tcaTm.tm_sec = (int)second;
        nextPassCulminationTime = mktime(&tcaTm);

        nextPassMaxTCA = overpass.maxelevation;
        culminationAzimuth = overpass.azmax;

        // Convert LOS: Loss of Signal
        invjday(overpass.jdstop, 0, false, year, month, day, hour, minute, second);

        struct tm losTm = {0};
        losTm.tm_year = year - 1900;
        losTm.tm_mon = month - 1;
        losTm.tm_mday = day;
        losTm.tm_hour = hour;
        losTm.tm_min = minute;
        losTm.tm_sec = (int)second;
        nextPassEnd = mktime(&losTm);

        nextPassLOSAzimuth = overpass.azstop;

        // Calculate Pass Duration
        passDuration = nextPassEnd - nextPassStart;
        passMinutes = passDuration / 60;
        passSeconds = passDuration % 60;

        /*
                // Debug Output UTC
                Serial.println();
                Serial.println("UTC Times");
                Serial.println("Next Pass Details:");
                Serial.printf("AOS: %s @ Azimuth: %.2f°\n", formatTime(nextPassStart, false).c_str(), nextPassAOSAzimuth);
                Serial.printf("TCA (Max Elevation): %s @ Elevation: %.2f°\n", formatTime(nextPassCulminationTime, false).c_str(), nextPassMaxTCA);
                Serial.printf("LOS: %s @ Azimuth: %.2f°\n", formatTime(nextPassEnd, false).c_str(), nextPassLOSAzimuth);
                Serial.printf("Pass Duration: %02lu minutes, %02lu seconds\n", passMinutes, passSeconds);
                Serial.println();
                // Debug Output LOCAL
                Serial.println("Local Times");
                Serial.println("Next Pass Details:");
                Serial.printf("AOS: %s @ Azimuth: %.2f°\n", formatTime(nextPassStart, true).c_str(), nextPassAOSAzimuth);
                Serial.printf("TCA (Max Elevation): %s @ Elevation: %.2f°\n", formatTime(nextPassCulminationTime, true).c_str(), nextPassMaxTCA);
                Serial.printf("LOS: %s @ Azimuth: %.2f°\n", formatTime(nextPassEnd, true).c_str(), nextPassLOSAzimuth);
                Serial.printf("Pass Duration: %02lu minutes, %02lu seconds\n", passMinutes, passSeconds);
                Serial.println();
                */
    }
    else
    {
        Serial.println("No pass found within specified parameters.");
    }
}
String formatTimeOnly(unsigned long epochTime, bool isLocal = false)
{
    // Adjust the epoch time by adding timezone offset and DST offset if applicable
    time_t t = isLocal ? epochTime + totalTimeOffset : epochTime;
    struct tm *tmInfo = gmtime(&t);
    char buffer[10];
    strftime(buffer, 10, "%H:%M:%S", tmInfo);
    return String(buffer);
}
String formatWithSeparator(unsigned long number)
{
    String result = "";
    int count = 0;

    // Process each digit from right to left
    while (number > 0)
    {
        if (count > 0 && count % 3 == 0)
        {
            result = "'" + result; // Add separator
        }
        result = String(number % 10) + result;
        number /= 10;
        count++;
    }

    return result;
}
String formatDate(unsigned long epochTime, bool isLocal)
{
    // Adjust the epoch time by adding timezone offset and DST offset if applicable
    time_t t = isLocal ? epochTime + totalTimeOffset : epochTime;
    struct tm *tmInfo = gmtime(&t);
    char buffer[10];
    strftime(buffer, 10, "%d.%m.%y", tmInfo);
    return String(buffer);
}
void displayNextPassTime(unsigned long durationInSec, int x, int y, uint16_t color, bool refresh)
{
    tft.setTextSize(1);
    tft.setTextFont(4);

    static char previousOutput[9] = "        "; // Previous state (8 characters + null terminator) "00:00:00"
    static uint16_t previousColor = TFT_GREEN;  // Track the last color used
    static bool isInitiated = false;            // Track initialization of static elements
    char output[9] = "        ";                // Current output (8 characters + null terminator)

    // Calculate hours, minutes, and seconds from durationInSec

    unsigned long hours = durationInSec / 3600;          // Get the number of full hours
    unsigned long minutes = (durationInSec % 3600) / 60; // Get the number of full minutes
    unsigned long seconds = durationInSec % 60;          // Get the remaining seconds

    // Fill the output array with the formatted time "HH:MM:SS"
    sprintf(output, "%02lu:%02lu:%02lu", hours, minutes, seconds);

    // Positions for characters
    String preText = "Next pass in ";
    int lPreText = preText.length(); // XXXXX
    int shift = 7;
    int xPos[] = {0, 14, 28, 42 - shift, 56 - shift, 70 - shift, 84 - 2 * shift, 98 - 2 * shift};
    //  1   2   :    4          5          :         7            8

    int leftmargin = tft.textWidth(preText);

    for (int i = 0; i < 8; i++)
        xPos[i] += x + leftmargin;

    // Handle color change or refresh logic
    if (color != previousColor)
    {
        // If the color has changed, trigger a full redraw
        refresh = true;
        previousColor = color; // Update the last used color
    }

    // Handle refresh logic for static elements (if needed)
    if (!isInitiated || refresh)
    {
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString(preText, x, y);
        isInitiated = true;
    }

    // Update only changed characters or redraw all if refresh is triggered
    for (int i = 0; i < 8; i++)
    {
        if (output[i] != previousOutput[i] || refresh)
        {
            // Print the previous character in black to erase it
            if (previousOutput[i] != ' ')
            {
                tft.setTextColor(TFT_BLACK, TFT_BLACK);
                tft.drawChar(previousOutput[i], xPos[i], y);
            }

            // Draw the new character
            if (output[i] != ' ')
            {
                tft.setTextColor(color, TFT_BLACK);
                tft.drawChar(output[i], xPos[i], y);
            }

            // Update previous state
            previousOutput[i] = output[i];
        }
    }
    Serial.println();
}
String displayRemainingVisibleTimeinMMSS(int delta)
{
    // Ensure delta does not exceed 60 minutes
    delta = constrain(delta, 0, 3600);

    // Calculate minutes and seconds
    int minutes = delta / 60;
    int seconds = delta % 60;

    // Format as MM:SS
    char buffer[6]; // Buffer to hold the formatted string
    snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);

    return String(buffer); // Return as String
}
void beepsBeforeVisibility()
{
    // Run the tone 5 times
    for (int i = 0; i < 3; i++)
    {
        ledcWriteTone(0, 2160); // Play 2.16kHz tone
        delay(400);             // Wait 1 second
        ledcWriteTone(0, 0);    // Stop the tone
        delay(150);             // Wait 0.5 second between repetitions
    }
}
void displayAzElPlotPage()
{
    const int stepsInSeconds = 1; // Step size in seconds
    const int PLOT_X = 38;        // Left margin
    const int PLOT_Y = 20;        // Top margin
    const int PLOT_WIDTH = 410;   // Plot width
    const int PLOT_HEIGHT = 240;  // Plot height
    const int SCREEN_WIDTH = 480;
    const int SCREEN_HEIGHT = 320;

    // Clear Screen
    tft.fillScreen(TFT_BLACK);

    // Draw Axes
    tft.drawRect(PLOT_X, PLOT_Y, PLOT_WIDTH, PLOT_HEIGHT, TFT_WHITE);

    // Draw Azimuth Gridlines and Labels
    int azGridInterval = 30;
    for (int az = 0; az <= 360; az += azGridInterval)
    {
        int y = PLOT_Y + PLOT_HEIGHT - map(az, 0, 360, 0, PLOT_HEIGHT);
        tft.drawLine(PLOT_X, y, PLOT_X + PLOT_WIDTH, y, TFT_DARKGREY);
        if (az % 90 == 0) // Label key azimuths
        {
            tft.setFreeFont(&FreeMono9pt7b);
            tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
            tft.setCursor(2, y - 5);
            tft.printf("%d°", az);
        }
    }

    // Draw Elevation Gridlines and Labels
    int elGridInterval = 15;
    for (int el = 0; el <= 90; el += elGridInterval)
    {
        int y = PLOT_Y + PLOT_HEIGHT - map(el, 0, 90, 0, PLOT_HEIGHT);
        // Draw vertical gridline
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(PLOT_X + PLOT_WIDTH + 10, y - 5);
        tft.printf("%d°", el);
    }

    // Draw Time Gridlines and Labels
    for (int i = 0; i <= 5; i++)
    {
        int x = PLOT_X + map(i, 0, 5, 0, PLOT_WIDTH);
        unsigned long time = nextPassStart + i * (nextPassEnd - nextPassStart) / 5;
        tft.drawLine(x, PLOT_Y, x, PLOT_Y + PLOT_HEIGHT, TFT_DARKGREY);

        String timeStr = formatTimeOnly(time, true).substring(0, 5);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(x - 28, PLOT_Y + PLOT_HEIGHT + 18);
        tft.print(timeStr);
    }

    // Start plotting Azimuth and Elevation
    unsigned long currentTime = nextPassStart;
    int lastAzX = -1, lastAzY = -1, lastElX = -1, lastElY = -1;
    float lastAzimuth = -1;

    while (currentTime <= nextPassEnd)
    {
        sat.findsat(currentTime); // Update satellite position

        // Calculate x position based on time
        int x = PLOT_X + map(currentTime, nextPassStart, nextPassEnd, 0, PLOT_WIDTH);
        int azY = PLOT_Y + PLOT_HEIGHT - map(sat.satAz, 0, 360, 0, PLOT_HEIGHT);
        int elY = PLOT_Y + PLOT_HEIGHT - map(sat.satEl, 0, 90, 0, PLOT_HEIGHT);
        /*
        Serial.print("sat.satAz: ");
        Serial.print(sat.satAz);
        Serial.print("   sat.satEl: ");
        Serial.println(sat.satEl);
    */
        if (lastAzX != -1)
        {
            // Handle azimuth wraparound
            if (lastAzimuth != -1 && abs(sat.satAz - lastAzimuth) > 180)
            {
                if (sat.satAz > lastAzimuth)
                {
                    tft.drawLine(lastAzX, lastAzY, x, PLOT_Y + PLOT_HEIGHT - map(0, 0, 360, 0, PLOT_HEIGHT), TFT_CYAN);
                    tft.drawLine(x, PLOT_Y + PLOT_HEIGHT - map(360, 0, 360, 0, PLOT_HEIGHT), x, azY, TFT_CYAN);
                }
                else
                {
                    tft.drawLine(lastAzX, lastAzY, x, PLOT_Y + PLOT_HEIGHT - map(360, 0, 360, 0, PLOT_HEIGHT), TFT_CYAN);
                    tft.drawLine(x, PLOT_Y + PLOT_HEIGHT - map(0, 0, 360, 0, PLOT_HEIGHT), x, azY, TFT_CYAN);
                }
            }
            else
            {
                tft.drawLine(lastAzX, lastAzY, x, azY, TFT_GREENYELLOW);
            }

            // Draw elevation line (no wraparound needed)
            tft.drawLine(lastElX, lastElY, x, elY, TFT_CYAN);
        }

        // Update for the next iteration
        lastAzimuth = sat.satAz;
        lastAzX = x;
        lastAzY = azY;
        lastElX = x;
        lastElY = elY;

        // Increment time by step size
        currentTime += stepsInSeconds;
    }

    // Display TCA Time
    int tcaX = PLOT_X + map(nextPassCulminationTime, nextPassStart, nextPassEnd, 0, PLOT_WIDTH);
    int tcaY = PLOT_Y + PLOT_HEIGHT - map(nextPassMaxTCA, 0, 90, 0, PLOT_HEIGHT);
    String tcaTimeStr = formatTimeOnly(nextPassCulminationTime, true).substring(0, 5);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(tcaX - 35, tcaY - 8);
    tft.setFreeFont(&FreeMonoBold12pt7b);
    tft.print(tcaTimeStr);
    tft.fillCircle(tcaX, tcaY, 4, TFT_GREEN);

    // Display Pass Duration
    unsigned long duration = nextPassEnd - nextPassStart;
    tft.setCursor(240 - tft.textWidth("Pass Duration: 10m 37s") / 2, 300);
    tft.print("Pass Duration: ");
    tft.print(duration / 60);
    tft.print("m ");
    tft.print(duration % 60);
    tft.println("s");

    // display current position if visible
    // unixtime = timeClient.getEpochTime(); // Get the current UNIX time
    sat.findsat(unixtime);
    if (sat.satEl > 0)
    {
        int x = PLOT_X + map(unixtime, nextPassStart, nextPassEnd, 0, PLOT_WIDTH);
        int elY1 = PLOT_Y + PLOT_HEIGHT - map(0, 0, 90, 0, PLOT_HEIGHT);
        int elY2 = PLOT_Y + PLOT_HEIGHT - map(sat.satEl, 0, 90, 0, PLOT_HEIGHT);
        tft.drawLine(x - 1, elY1, x - 1, elY2, TFT_RED);
        tft.drawLine(x, elY1, x, elY2, TFT_RED);
        tft.drawLine(x + 1, elY1, x + 1, elY2, TFT_RED);
        tft.fillCircle(x, elY2, 3, TFT_CYAN);
        tft.drawCircle(x, elY2, 4, TFT_RED);
    }
}
void displayPolarPlotPage()
{

    getOrbitNumber(nextPassStart);
    // Clear the area to redraw
    tft.fillScreen(TFT_BLACK);
    // Display AOS on TFT screen
    int margin = 5;
    int newline = 8;
    tft.setCursor(margin, 10);
    tft.setTextColor(TFT_GOLD, TFT_BLACK);
    tft.setTextFont(4);                            // Set the desired font
    tft.print("ISS Orbit ");                       // Label for Orbit number
    tft.println(formatWithSeparator(orbitNumber)); // Label for Orbit number
    tft.setCursor(margin, tft.getCursorY() + 5);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);

    tft.print("AOS  "); // Label for AOS

    tft.setTextFont(2);                           // Set the desired font
    tft.println(formatDate(nextPassStart, true)); // Prints just the date
    tft.setCursor(margin, tft.getCursorY() + 8);  // Move to next line at x=5

    tft.setTextFont(4);                               // Set the desired font
    tft.setCursor(margin, tft.getCursorY());          // Move to next line at x=5
    tft.println(formatTimeOnly(nextPassStart, true)); // Prints just the time

    tft.setCursor(margin, tft.getCursorY());          // Move to next line at x=5
    int azimuthInt = (int)(nextPassAOSAzimuth + 0.5); // Rounds to nearest integer
    tft.print(azimuthInt);
    tft.println(" deg.");
    tft.setCursor(10, tft.getCursorY() + newline);

    Serial.print("Azimuth at AOS: ");
    Serial.print(nextPassAOSAzimuth);
    Serial.println("°");

    Serial.print("Max Elevation TCA: ");
    Serial.print(nextPassMaxTCA);
    Serial.println("° at time ");
    Serial.println(formatTime(nextPassCulminationTime, true));
    Serial.print("Azimuth at max elevation:");
    Serial.print(culminationAzimuth);
    Serial.println("°");

    // Display TCA
    tft.setTextFont(4); // Set the desired font
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(margin, tft.getCursorY()); // Move to next line at x=5
    tft.print("TCA ");                       // Label for TCA
    tft.print(nextPassMaxTCA, 1);            // 1 specifies the number of decimal places
    tft.println(" deg.");
    // Set the desired font
    tft.setCursor(margin, tft.getCursorY());                    // Move to next line at x=5
    tft.println(formatTimeOnly(nextPassCulminationTime, true)); // Prints just the time

    tft.setCursor(margin, tft.getCursorY());      // Move to next line at x=5
    azimuthInt = (int)(culminationAzimuth + 0.5); // Rounds to nearest integer
    tft.print(azimuthInt);
    tft.println(" deg.");
    tft.setCursor(10, tft.getCursorY() + newline);

    // Display LOS
    tft.setTextFont(4); // Set the desired font
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(margin, tft.getCursorY()); // Move to next line at x=5

    tft.println("LOS"); // Label for TCA

    tft.setTextFont(2);                             // Set the desired font
    tft.setCursor(margin, tft.getCursorY());        // Move to next line at x=5
    tft.setTextFont(4);                             // Set the desired font
    tft.setCursor(margin, tft.getCursorY());        // Move to next line at x=5
    tft.println(formatTimeOnly(nextPassEnd, true)); // Prints just the time

    tft.setCursor(margin, tft.getCursorY());      // Move to next line at x=5
    azimuthInt = (int)(nextPassLOSAzimuth + 0.5); // Rounds to nearest integer
    tft.print(azimuthInt);
    tft.println(" deg.");
    tft.setTextColor(TFT_GOLD, TFT_BLACK);
    tft.setCursor(margin, tft.getCursorY() + newline);
    tft.print("Pass Duration: ");
    unsigned long duration = nextPassEnd - nextPassStart;

    tft.print(duration / 60);
    tft.print(":");

    tft.print(duration % 60);

    Serial.print("Pass Duration: ");
    Serial.print(duration / 60); // minutes
    Serial.print(" min ");
    Serial.print(duration % 60); // seconds
    Serial.println(" sec");

    Serial.print("LOS (End): ");
    Serial.println(formatTime(nextPassEnd, true));
    Serial.print("Azimuth at LOS: ");
    Serial.print(nextPassLOSAzimuth);
    Serial.println("°");

#define POLAR_CENTER_X 320 // Center of the polar chart
#define POLAR_CENTER_Y 160 // Center of the polar chart
#define POLAR_RADIUS 140   // Maximum radius for the outermost circle

    // Elevations to label and draw circles for
    int elevations[] = {0, 15, 30, 45, 60, 75};

    // Draw concentric circles for elevation markers
    for (int i = 0; i < 6; i++) // Loop through elevations array
    {
        int elevation = elevations[i];

        // Map the elevation to the corresponding radius
        int radius = map(elevation, 0, 90, POLAR_RADIUS, 0); // Corrected mapping

        // Draw the circle for this elevation
        tft.drawCircle(POLAR_CENTER_X, POLAR_CENTER_Y, radius, TFT_LIGHTGREY);

        // Label the elevation on the circle
        int x = POLAR_CENTER_X + radius + 5; // Offset to place labels inside each circle
        int y = POLAR_CENTER_Y - 10;
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextFont(1);
        tft.setCursor(x, y);
        tft.print(elevation); // Display the elevation value
    }

    // Add degree labels around the outer circle at 30° intervals, with 0° at North
    for (int angle = 0; angle < 360; angle += 30)
    {
        if (angle == 0 || angle == 90 || angle == 180 || angle == 270)
            continue;

        float radianAngle = radians(angle); // Angle adjusted to start 0° at North
        int x = POLAR_CENTER_X + (POLAR_RADIUS + 10) * sin(radianAngle);
        int y = POLAR_CENTER_Y - (POLAR_RADIUS + 10) * cos(radianAngle);

        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextFont(1);
        tft.setCursor(x - 5, y - 5);
        tft.print(angle);
    }

    // Draw radial lines every 30° for compass directions
    for (int angle = 0; angle < 360; angle += 30)
    {
        float radianAngle = radians(angle); // Corrected orientation to match 0° at North
        int xEnd = POLAR_CENTER_X + POLAR_RADIUS * sin(radianAngle);
        int yEnd = POLAR_CENTER_Y - POLAR_RADIUS * cos(radianAngle);
        tft.drawLine(POLAR_CENTER_X, POLAR_CENTER_Y, xEnd, yEnd, TFT_DARKGREY);
    }
    // Adding the "90" label in the center
    // tft.setTextColor(TFT_WHITE, TFT_BLACK);  // Set text color
    tft.setCursor(POLAR_CENTER_X - 5, POLAR_CENTER_Y - 10); // Slightly offset for readability
    tft.print("90");                                        // Print "90" in the center
    // Draw compass labels with correct orientation
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("N", POLAR_CENTER_X, POLAR_CENTER_Y - POLAR_RADIUS - 18, 2);
    tft.drawCentreString("S", POLAR_CENTER_X, POLAR_CENTER_Y + POLAR_RADIUS + 5, 2);
    tft.drawCentreString("E", POLAR_CENTER_X + POLAR_RADIUS + 13, POLAR_CENTER_Y, 2);
    tft.drawCentreString("W", POLAR_CENTER_X - POLAR_RADIUS - 10, POLAR_CENTER_Y, 2);

    // Plot the satellite pass path with color dots for AOS, max elevation, and LOS
    int lastX = -1, lastY = -1;
    // Time step for pass prediction
    int timeStep = 1;
    bool AOSdrawm = false;
    int x = 0;
    int y = 0;
    for (unsigned long t = nextPassStart - 30; t <= nextPassEnd + 30; t += timeStep) // just adding some  second 'margin'
    {
        sat.findsat(t);
        float azimuth = sat.satAz;
        float elevation = sat.satEl;
        /*
        Serial.print("azimuth = ");
        Serial.print(azimuth);
        Serial.print("  elevation = ");
        Serial.println(elevation);
        */
        // Serial.printf("Time: %lu | Azimuth: %.2f | Elevation: %.2f\n", t, azimuth, elevation);

        if (elevation >= 0)
        {
            int radius = map(90 - elevation, 0, 90, 0, POLAR_RADIUS);
            float radianAzimuth = radians(azimuth);
            x = POLAR_CENTER_X + radius * sin(radianAzimuth);
            y = POLAR_CENTER_Y - radius * cos(radianAzimuth);

            // Serial.printf("Plotted Point -> x: %d, y: %d\n", x, y);

            if (elevation > 0 && AOSdrawm == false)
            {
                tft.fillCircle(x, y, 3, TFT_GREEN); // Green dot for AOS
                AOSdrawm = true;
            }
            if (t == nextPassCulminationTime)
            {
                tft.fillCircle(x, y, 3, TFT_YELLOW); // Yellow dot for max elevation
                                                     // Serial.println("Plotted TCA (Yellow)");
            }

            if (lastX != -1 && lastY != -1)
            {
                tft.drawLine(lastX, lastY, x, y, TFT_GOLD); // Path
                                                            // Serial.println("Drew Line Segment");
            }
            lastX = x;
            lastY = y;
        }
    }
    tft.fillCircle(x, y, 3, TFT_RED); // Red dot for LOS

    // IF VISIBLE
    // display current position if visible
    sat.findsat(unixtime);

    if (sat.satEl > 0)
    {
        float azimuth = sat.satAz;
        float elevation = sat.satEl;
        int radius = map(90 - elevation, 0, 90, 0, POLAR_RADIUS);
        float radianAzimuth = radians(azimuth);

        x = POLAR_CENTER_X + radius * sin(radianAzimuth);
        y = POLAR_CENTER_Y - radius * cos(radianAzimuth);

        tft.fillCircle(x, y, 3, TFT_CYAN);
        tft.drawCircle(x, y, 4, TFT_RED);
        tft.drawLine(POLAR_CENTER_X, POLAR_CENTER_Y, x, y, TFT_CYAN);
    }
}
void retrieveTLEelementsForSatellite(int catalogNumber)
{
    logWithBoxFrame("Retrieving first of newer TLE Elements from celestrak.com");
    String url = "http://www.celestrak.org/NORAD/elements/gp.php?CATNR=" + String(catalogNumber) + "&FORMAT=TLE";
    HTTPClient http;
    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode == 200)
    {
        String payload = http.getString();

        int firstLineEnd = payload.indexOf('\n');
        int secondLineEnd = payload.indexOf('\n', firstLineEnd + 1);

        if (firstLineEnd > 0 && secondLineEnd > firstLineEnd)
        {
            String satelliteName = payload.substring(0, firstLineEnd);
            satelliteName.trim(); // Trim whitespace after assignment

            String tleLine1 = payload.substring(firstLineEnd + 1, secondLineEnd);
            tleLine1.trim(); // Trim whitespace after assignment

            String tleLine2 = payload.substring(secondLineEnd + 1);
            tleLine2.trim(); // Trim whitespace after assignment

            preferences.begin("tle-storage", false); // Open in write mode
            preferences.putInt("catalogNumber", catalogNumber);
            preferences.putString("satelliteName", satelliteName);
            preferences.putString("tleLine1", tleLine1);
            preferences.putString("tleLine2", tleLine2);
            preferences.putULong("retrievalTime", timeClient.getEpochTime());
            preferences.end();

            Serial.println("Saved new TLE elements to flash memory for later retrieval");
            Serial.println("Satellite Name: " + satelliteName);
            Serial.println("TLE Line 1: " + tleLine1);
            Serial.println("TLE Line 2: " + tleLine2);
            TFTprint("");
            TFTprint("Elements saved to from flash memory.", TFT_GREEN);
            TFTprint("");
            delay(bootingMessagePause);

            // format for sgpd4
            satelliteName.toCharArray(SatNameCharArray, sizeof(SatNameCharArray));
            tleLine1.toCharArray(TLEline1CharArray, sizeof(TLEline1CharArray));
            tleLine2.toCharArray(TLEline2CharArray, sizeof(TLEline2CharArray));
        }
        else
        {
            Serial.println("Error: Unable to parse TLE data.");
        }
    }
    else
    {
        Serial.printf("Error: HTTP response code %d\n", httpResponseCode);
    }
    http.end();
}
void displayTableNext10Passes()
{
    passinfo overpass;
    // Initialize prediction start point
    sat.initpredpoint(unixtime + 10 * 60, 0); // adding 10 minutes to ensure that next pass is not in the past (experimental)

    Serial.println("Next 10 Passes:");
    Serial.println("--------------------");

    int year, month, day, hour, minute;
    double second;
    bool daylightSaving = true;

    // Clear the TFT and set up the screen
    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(4);
    tft.setTextColor(TFT_GOLD, TFT_BLACK);
    int margin = 12;
    // Draw headers
    tft.setCursor(margin, 0);
    tft.print("DATE");
    tft.setCursor(margin + 85, 0);
    tft.print("AOS");
    tft.setCursor(margin + 164, 0);
    tft.print("TCA");
    tft.setCursor(margin + 244, 0);
    tft.print("LOS");
    tft.setCursor(margin + 324, 0);
    tft.print("DUR");
    tft.setCursor(margin + 400, 0);
    tft.print("MEL");

    // Adjust timezone and DST offset
    long adjustedTimezoneOffset = totalTimeOffset; // Correctly adjust for DST and timezone offset

    // Iterate through the next 10 passes
    for (int i = 1; i <= 12; i++) // Loop for next 12 passes (as per your original code)
    {
        bool passFound = sat.nextpass(&overpass, 100);
        if (passFound)
        {
            // Prepare persistent struct tm objects for AOS, TCA, and LOS
            struct tm aosTm = {0}, tcaTm = {0}, losTm = {0};

            // Convert AOS: Acquisition of Signal
            invjday(overpass.jdstart, adjustedTimezoneOffset / 3600, daylightSaving, year, month, day, hour, minute, second);
            aosTm.tm_year = year - 1900;
            aosTm.tm_mon = month - 1;
            aosTm.tm_mday = day;
            aosTm.tm_hour = hour;
            aosTm.tm_min = minute;

            // Convert TCA: Time of Closest Approach
            invjday(overpass.jdmax, adjustedTimezoneOffset / 3600, daylightSaving, year, month, day, hour, minute, second);
            tcaTm.tm_year = year - 1900;
            tcaTm.tm_mon = month - 1;
            tcaTm.tm_mday = day;
            tcaTm.tm_hour = hour;
            tcaTm.tm_min = minute;

            // Convert LOS: Loss of Signal
            invjday(overpass.jdstop, adjustedTimezoneOffset / 3600, daylightSaving, year, month, day, hour, minute, second);
            losTm.tm_year = year - 1900;
            losTm.tm_mon = month - 1;
            losTm.tm_mday = day;
            losTm.tm_hour = hour;
            losTm.tm_min = minute;

            // Calculate pass duration (in seconds)
            unsigned long passDuration = (unsigned long)((overpass.jdstop - overpass.jdstart) * 86400);
            int passMinutes = passDuration / 60;
            int passSeconds = passDuration % 60;

            // Format pass duration as HH:MM
            char durationFormatted[10];
            sprintf(durationFormatted, "%02d:%02d", passMinutes, passSeconds);

            // Maximum elevation (rounded to the nearest integer)
            int maxElevation = (int)round(overpass.maxelevation);

            // Format date without year (just day and month)
            char passDate[6];
            sprintf(passDate, "%02d.%02d", day, month);

            // Format AOS, TCA, and LOS times in HH:MM format
            char aosTime[6], tcaTime[6], losTime[6];
            sprintf(aosTime, "%02d:%02d", aosTm.tm_hour, aosTm.tm_min);
            sprintf(tcaTime, "%02d:%02d", tcaTm.tm_hour, tcaTm.tm_min);
            sprintf(losTime, "%02d:%02d", losTm.tm_hour, losTm.tm_min);

            // Serial output for debugging
            Serial.printf("%02d %s | AOS %s TCA %s LOS %s DUR %s MEL: %d\n",
                          i, passDate, aosTime, tcaTime, losTime, durationFormatted, maxElevation);

            // Highlight if elevation is above 30° for radio ham contact
            if (maxElevation > 35)
            {
                tft.setTextColor(TFT_GREEN, TFT_BLACK); // Highlight in green
            }
            else
            {
                tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK); // Normal color
            }

            // TFT output: Display pass information on screen
            int yPosition = i * 23 + 5; // Adjust vertical spacing for each row
            tft.setCursor(margin, yPosition);
            tft.printf("%s", passDate);
            tft.setCursor(margin + 80, yPosition);
            tft.printf("%s", aosTime);
            tft.setCursor(margin + 160, yPosition);
            tft.printf("%s", tcaTime);
            tft.setCursor(margin + 240, yPosition);
            tft.printf("%s", losTime);
            tft.setCursor(margin + 320, yPosition);
            tft.printf("%s", durationFormatted);
            tft.setCursor(margin + 400, yPosition);
            tft.printf("%.1f", overpass.maxelevation);
        }
        else
        {
            // If no more passes are found, exit the loop
            Serial.println("No more passes found.");
            break;
        }
    }
}
void displayMapWithMultiPasses()
{
    // Constants for map scaling and placement
    const int mapWidth = 480;  // Width of the map
    const int mapHeight = 290; // Height of the map
    const int mapOffsetY = 30; // Y-offset for the map (black banner)
    const int timeStep = 20;   // Time step for plotting points

    // Clear the screen and display the map image
    tft.fillScreen(TFT_BLACK);
    displayEquirectangularWorlsMap();

    // STEP 1: Get satellite position and draw the footprint
    sat.findsat(unixtime);
    float startLat = sat.satLat; // Satellite latitude
    float startLon = sat.satLon; // Satellite longitude
    float satAlt = sat.satAlt;   // Satellite altitude

    // Calculate footprint radius in kilometers
    float earthRadius = 6371.0; // Earth's radius in kilometers
    float footprintRadiusKm = earthRadius * acos(earthRadius / (earthRadius + satAlt));

    // Debug: Print footprint radius
    Serial.print("Footprint radius (km): ");
    Serial.println(footprintRadiusKm);

    // Draw the footprint as an ellipse
    for (int angle = 0; angle < 360; angle++)
    {
        float rad = angle * DEG_TO_RAD; // Convert angle to radians

        // Calculate latitude and longitude for each point on the footprint
        float deltaLat = footprintRadiusKm * cos(rad) / 111.0;                                // Latitude adjustment (1° ≈ 111 km)
        float deltaLon = footprintRadiusKm * sin(rad) / (111.0 * cos(startLat * DEG_TO_RAD)); // Longitude adjustment

        float footprintLat = startLat + deltaLat; // New latitude for the footprint point
        float footprintLon = startLon + deltaLon; // New longitude for the footprint point

        // Longitude wrapping to keep within -180° to 180°
        if (footprintLon > 180.0)
            footprintLon -= 360.0;
        if (footprintLon < -180.0)
            footprintLon += 360.0;

        // Map latitude and longitude to screen coordinates
        int x = map(footprintLon, -180, 180, 0, mapWidth);             // Longitude to X
        int y = map(footprintLat, 90, -90, 0, mapHeight) + mapOffsetY; // Latitude to Y with offset

        // Draw footprint point if within screen bounds
        if (x >= 0 && x < mapWidth && y >= mapOffsetY && y < mapHeight + mapOffsetY)
        {
            // tft.fillCircle(x, y, 1, TFT_GOLD);
            tft.drawPixel(x, y, TFT_GOLD);
        }
    }

    // STEP 2: Plot the starting position
    int startX = map(startLon, -180, 180, 0, mapWidth);             // Longitude to X-coordinate
    int startY = map(startLat, 90, -90, 0, mapHeight) + mapOffsetY; // Latitude to Y-coordinate with offset

    // Mark the starting position
    tft.fillCircle(startX, startY, 3, TFT_YELLOW); // Starting point
    tft.drawCircle(startX, startY, 4, TFT_RED);
    tft.drawCircle(startX, startY, 5, TFT_RED);

    // Debug: Print starting position
    Serial.print("Starting Point (X, Y): ");
    Serial.print(startX);
    Serial.print(", ");
    Serial.println(startY);

    // STEP 3: Plot the satellite's path for three orbits
    unixtime = timeClient.getEpochTime(); // Get the current UNIX timestamp

    unsigned long t = unixtime;
    int passageCount = 0;
    bool hasLeftStartX = false;

    while (passageCount < 3)
    {
        sat.findsat(t);
        float lat = sat.satLat;
        float lon = sat.satLon;

        // Map latitude and longitude to screen coordinates
        int x = map(lon, -180, 180, 0, mapWidth);             // Longitude to X
        int y = map(lat, 90, -90, 0, mapHeight) + mapOffsetY; // Latitude to Y with offset

        // Choose color based on passage count
        uint16_t color = (passageCount == 0) ? TFT_GREEN : (passageCount == 1) ? TFT_YELLOW
                                                                               : TFT_RED;

        // Draw the satellite's path
        // tft.fillCircle(x, y, 1, color);
        tft.drawPixel(x, y, color);

        // Check if the satellite moved away from the starting X position
        if (!hasLeftStartX && abs(x - startX) > 20)
        {
            hasLeftStartX = true;
        }

        // Check if the satellite returned close to the starting X position
        if (hasLeftStartX && abs(x - startX) < 5)
        {
            passageCount++;
            hasLeftStartX = false;
        }

        t += timeStep; // Increment time step
    }
}
void displayEquirectangularWorlsMap()
{
    // https://notisrac.github.io/FileToCArray/
    int16_t rc = png.openFLASH((uint8_t *)worldMap, sizeof(worldMap), pngDraw);
    if (rc == PNG_SUCCESS)
    {
        Serial.println("Successfully opened png file");
        Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
        tft.startWrite();
        uint32_t dt = millis();
        rc = png.decode(NULL, 0);
        Serial.print(millis() - dt);
        Serial.println("ms");
        tft.endWrite();
    }

    String text = String(SatNameCharArray) + " Next 3 Passes";

    // Set the text font to FONT4
    tft.setTextFont(4);

    // Calculate the width of the text to center it
    int textWidth = tft.textWidth(text);

    // Calculate the x position to center the text on the screen
    int xPosition = (tft.width() - textWidth) / 2;

    // Set the cursor position at the center (x) and y = 5
    tft.setCursor(xPosition, 5);

    // Set text color (adjust to your preference)
    tft.setTextColor(TFT_GOLD, TFT_TRANSPARENT); // White text on black background

    // Print the text
    tft.print(text);

    int yPassColor = 30;
    text = "Green";

    // Set the text font to FONT4
    tft.setTextFont(2);

    // Calculate the width of the text to center it
    textWidth = tft.textWidth(text);

    // Calculate the x position to center the text on the screen
    xPosition = (tft.width() - textWidth) / 2 - 50;

    // Set the cursor position at the center (x) and y = 5
    tft.setCursor(xPosition, yPassColor);

    // Set text color (adjust to your preference)
    tft.setTextColor(TFT_GREEN, TFT_TRANSPARENT); // White text on black background

    // Print the text
    tft.print(text);

    text = "- Yellow -";

    // Set the text font to FONT4
    tft.setTextFont(2);

    // Calculate the width of the text to center it
    textWidth = tft.textWidth(text);

    // Calculate the x position to center the text on the screen
    xPosition = (tft.width() - textWidth) / 2;

    // Set the cursor position at the center (x) and y = 5
    tft.setCursor(xPosition, yPassColor);

    // Set text color (adjust to your preference)
    tft.setTextColor(TFT_GOLD, TFT_TRANSPARENT); // White text on black background

    // Print the text
    tft.print(text);

    text = "Red";

    // Set the text font to FONT4
    tft.setTextFont(2);

    // Calculate the width of the text to center it
    textWidth = tft.textWidth(text);

    // Calculate the x position to center the text on the screen
    xPosition = (tft.width() - textWidth) / 2 + 50;

    // Set the cursor position at the center (x) and y = 5
    tft.setCursor(xPosition, yPassColor);

    // Set text color (adjust to your preference)
    tft.setTextColor(TFT_RED, TFT_TRANSPARENT); // White text on black background

    // Print the text
    tft.print(text);
}
void calibrateTFTscreen()
{
    uint16_t calibrationData[5];

    // Display recalibration message
    tft.fillScreen(TFT_BLACK);             // Clear screen
    tft.setTextColor(TFT_BLACK, TFT_GOLD); // Set text color (black on gold)
    tft.setFreeFont(&FreeSansBold12pt7b);  // Use custom free font
    tft.setCursor(10, 22);
    tft.fillRect(0, 0, 480, 30, TFT_GOLD);
    tft.print("   TFT TOUCHSCREEN CALIBRATION");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.setFreeFont(&FreeSans12pt7b);

    String instructions[] = {
        "Welcome to the initial touchscreen calibration.",
        "This procedure will only be required once.",
        "On the next screen you will see arrows",
        "appearing one after the other at each corner.",
        "Just tap them until completion.",
        "",
        "Tap anywhere on the screen to begin"};

    int16_t yPos = 100;

    for (String line : instructions)
    {
        int16_t xPos = (tft.width() - tft.textWidth(line)) / 2;
        tft.setCursor(xPos, yPos);
        tft.print(line);
        yPos += tft.fontHeight();
    }

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor((tft.width() - tft.textWidth(instructions[6])) / 2, yPos);
    tft.print(instructions[6]);

    while (true)
    {
        uint16_t x, y;
        if (tft.getTouch(&x, &y))
        {
            break;
        }
    }

    tft.fillScreen(TFT_BLACK);

    tft.calibrateTouch(calibrationData, TFT_GREEN, TFT_BLACK, 12);

    preferences.begin("TFT", false);
    for (int i = 0; i < 5; i++)
    {
        preferences.putUInt(("calib" + String(i)).c_str(), calibrationData[i]);
    }
    preferences.end();

    // Test calibration with dynamic feedback
    tft.fillScreen(TFT_BLACK);
    uint16_t x, y;
    int16_t lastX = -1, lastY = -1;
    String lastResult = "";

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(4);
    String text = "Tap anywhere to test";
    tft.setCursor((tft.width() - tft.textWidth(text)) / 2, 8);
    tft.print(text);

    text = "Click Here to Exit";
    int16_t btn_w = tft.textWidth(text) + 20;
    int16_t btn_h = tft.fontHeight() + 12;
    int16_t btn_x = (tft.width() - btn_w) / 2;
    int16_t btn_y = 280;
    int16_t btn_r = 10;

    tft.fillRoundRect(btn_x, btn_y, btn_w, btn_h, btn_r, TFT_BLUE);
    tft.drawRoundRect(btn_x, btn_y, btn_w, btn_h, btn_r, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setCursor((tft.width() - tft.textWidth(text)) / 2, btn_y + 10);
    tft.print(text);

    while (true)
    {
        if (tft.getTouch(&x, &y))
        {
            String result = "x=" + String(x) + "  y=" + String(y) + "  p=" + String(tft.getTouchRawZ());

            tft.setTextColor(TFT_BLACK, TFT_BLACK);
            tft.setCursor(lastX, lastY);
            tft.print(lastResult);

            int16_t textWidth = tft.textWidth(result);
            int16_t xPos = (tft.width() - textWidth) / 2;
            int16_t yPos = (tft.height() - tft.fontHeight()) / 2;

            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setCursor(xPos, yPos);
            tft.print(result);

            lastX = xPos;
            lastY = yPos;
            lastResult = result;

            tft.fillCircle(x, y, 2, TFT_RED);

            if (x > btn_x && x < btn_x + btn_w && y > btn_y && y < btn_y + btn_h)
            {
                Serial.println("Exit Touched");
                break;
            }
        }
    }

    tft.fillScreen(TFT_BLACK);
}
void checkAndApplyTFTCalibrationData(bool recalibrate)
{
    if (recalibrate)
    {
        calibrateTFTscreen();
        return;
    }

    uint16_t calibrationData[5];
    preferences.begin("TFT", true);
    bool dataValid = true;

    for (int i = 0; i < 5; i++)
    {
        calibrationData[i] = preferences.getUInt(("calib" + String(i)).c_str(), 0xFFFF);
        if (calibrationData[i] == 0xFFFF)
        {
            dataValid = false;
        }
    }
    preferences.end();

    if (dataValid)
    {
        tft.setTouch(calibrationData);
        Serial.println("Calibration data applied.");
    }
    else
    {
        Serial.println("Invalid calibration data. Recalibrating...");
        calibrateTFTscreen();
    }
}
void drawSpeakerON(int x, int y)
{ // Clear the previous display area with a black rectangle
    int width = 30;
    int height = 30;
    tft.fillRect(x, y, width, height, TFT_BLACK);

    // tft.drawRect(x - 30, y - 30, width + 60, height + 60, TFT_YELLOW); //touch rectangle
    // Function to draw a bold line
    auto drawBoldLine = [&](int x0, int y0, int x1, int y1, uint16_t color, int thickness)
    {
        for (int i = -thickness / 2; i <= thickness / 2; i++)
        {
            tft.drawLine(x0 + i, y0, x1 + i, y1, color);
            tft.drawLine(x0, y0 + i, x1, y1 + i, color);
        }
    };

    float scale = 1.1; // Scaling factor
    int thickness = 1; // Line thickness

    // Semi-transparent filled shape (speaker body)
    tft.fillTriangle(
        x + scale * 13, y + scale * 3,
        x + scale * 7, y + scale * 8,
        x + scale * 5, y + scale * 8,
        TFT_DARKGREY);

    // Main outline of the speaker
    drawBoldLine(x + scale * 13, y + scale * 3, x + scale * 7, y + scale * 8, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 7, y + scale * 8, x + scale * 5, y + scale * 8, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 5, y + scale * 8, x + scale * 3, y + scale * 10, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 3, y + scale * 10, x + scale * 3, y + scale * 14, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 3, y + scale * 14, x + scale * 5, y + scale * 16, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 5, y + scale * 16, x + scale * 7, y + scale * 16, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 7, y + scale * 16, x + scale * 13, y + scale * 21, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 13, y + scale * 21, x + scale * 13, y + scale * 3, TFT_WHITE, thickness);

    // Curved sound waves
    drawBoldLine(x + scale * 16, y + scale * 9, x + scale * 17, y + scale * 12, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 17, y + scale * 12, x + scale * 16, y + scale * 15, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 19, y + scale * 6, x + scale * 20, y + scale * 12, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 20, y + scale * 12, x + scale * 19, y + scale * 18, TFT_WHITE, thickness);
}
void drawSpeakerOFF(int x, int y)
{
    // Clear the previous display area with a black rectangle
    int width = 30;
    int height = 30;
    tft.fillRect(x, y, width, height, TFT_BLACK);

    // Function to draw a bold line
    auto drawBoldLine = [&](int x0, int y0, int x1, int y1, uint16_t color, int thickness)
    {
        for (int i = -thickness / 2; i <= thickness / 2; i++)
        {
            tft.drawLine(x0 + i, y0, x1 + i, y1, color);
            tft.drawLine(x0, y0 + i, x1, y1 + i, color);
        }
    };

    float scale = 1.2; // Scaling factor
    int thickness = 1; // Line thickness

    // Semi-transparent filled shape (speaker body)
    tft.fillTriangle(
        x + scale * 13, y + scale * 3,
        x + scale * 7, y + scale * 8,
        x + scale * 5, y + scale * 8,
        TFT_DARKGREY);

    // Main outline of the speaker
    drawBoldLine(x + scale * 13, y + scale * 3, x + scale * 7, y + scale * 8, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 7, y + scale * 8, x + scale * 5, y + scale * 8, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 5, y + scale * 8, x + scale * 3, y + scale * 10, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 3, y + scale * 10, x + scale * 3, y + scale * 14, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 3, y + scale * 14, x + scale * 5, y + scale * 16, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 5, y + scale * 16, x + scale * 7, y + scale * 16, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 7, y + scale * 16, x + scale * 13, y + scale * 21, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 13, y + scale * 21, x + scale * 13, y + scale * 3, TFT_WHITE, thickness);

    // Cross lines
    drawBoldLine(x + scale * 16, y + scale * 9, x + scale * 22, y + scale * 15, TFT_WHITE, thickness);
    drawBoldLine(x + scale * 22, y + scale * 9, x + scale * 16, y + scale * 15, TFT_WHITE, thickness);
}
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_CONNECTED:
        Serial.printf("Client %u connected\n", num);
        break;
    case WStype_DISCONNECTED:
        Serial.printf("Client %u disconnected\n", num);
        break;
    case WStype_TEXT:
        Serial.printf("Client %u sent: %s\n", num, payload);
        break;
    }
}
void startWebSocket()
{
    Serial.println();
    logWithBoxFrame("Starting WebSocket");

    String wsAddress = "ws://" + WiFi.localIP().toString() + ":" + String(websocketPort);
    Serial.println("WebSocket server available at:");
    Serial.println(wsAddress);
    // Initialize WebSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    newTFTprintPage = true;
    TFTprint("Starting WebSocket...", TFT_YELLOW);
    TFTprint("WebSocket Server Started!");
    TFTprint("");
    TFTprint("Available at " + wsAddress, TFT_YELLOW);
    TFTprint("");
    TFTprint("Test it with 'Simple WebSocket Client'", TFT_WHITE);
    TFTprint("(Chrome Extension)", TFT_WHITE);
}
void setup()
{
    if (DEBUG_ON_TFT)
    {
        bootingMessagePause = 8000;
    }
    Serial.begin(115200);
    initializeTFT();
    initializeBuzzer();
    displaySplashScreen(2500);
    displayWelcomeMessage(2500);
    checkAndApplyTFTCalibrationData(false);
    displaySysInfo();
    connectToWiFi();
    delay(bootingMessagePause);
    getTimezoneData();
    delay(bootingMessagePause);
    while (!syncTimeFromNTP(true))
    {
        Serial.println("Time synchronization failed. Retrying...");
        delay(5000); // Retry every 5 seconds
    }
    delay(bootingMessagePause);
    getTLEelements(satelliteCatalogueNumber);
    delay(bootingMessagePause);
    displayUsedElements();
    delay(bootingMessagePause);
    startWebSocket();
    delay(bootingMessagePause);

    sat.init(SatNameCharArray, TLEline1CharArray, TLEline2CharArray);
    sat.site(OBSERVER_LATITUDE, OBSERVER_LONGITUDE, OBSERVER_ALTITUDE);

    if (beepsNotificationBeforeVisibility == 0)
    {
        speakerisON = false;
    }

    tft.fillScreen(TFT_BLACK);

    unixtime = timeClient.getEpochTime(); // Get the current UNIX timestamp
    calculateNextPass();
    displayMainPage();

    // displayAzElPlotPage();
    // displayPolarPlotPage();
    // displayTableNext10Passes();
    // displayMapWithMultiPasses();
}

void loop()
{

    // Define the speaker touch area
    int rectX = 196; // Top-left corner X
    int rectY = 100; // Top-left corner Y
    int rectW = 60;  // Width of the rectangle
    int rectH = 60;  // Height of the rectangle

    // Main loop code (if needed)
    unixtime = timeClient.getEpochTime(); // Get the current UNIX timestamp
    int deltaHour = 0;
    int deltaMin = 0;
    unixtime = unixtime + deltaHour * 3600 + deltaMin * 60;

    // get new sat data
    sat.findsat(unixtime);
    // calculate orbit number
    getOrbitNumber(unixtime);
    // beeps if its time ()
    if (nextPassStart - unixtime == beepsNotificationBeforeVisibility && beepsNotificationBeforeVisibility != 0 && speakerisON)
    {
        beepsBeforeVisibility();
    }

    static int touchCounter = 1;
    static unsigned long lastTouchTime = 0;
    unsigned long debounceDelay = 200; // debounce delay in milliseconds
    // Flags to track if each page has been displayed
    static bool page1Displayed = true;
    static bool page2Displayed = false;
    static bool page3Displayed = false;
    static bool page4Displayed = false;
    static bool page5Displayed = false;
    static bool page6Displayed = false;
    static unsigned long multipassMaplastRefreshTime = 0;
    static unsigned long PolarPlotlastRefreshTime = 0;
    static unsigned long AzElPlotlastRefreshTime = 0;

    // Get the current touch pressure
    // int touchTFT = tft.getTouchRawZ();
    // Check if the touch pressure exceeds the threshold and debounce
    // if (touchTFT > touchTreshold)
    uint16_t tx, ty;

    if (tft.getTouch(&tx, &ty))
    {
        // Only increment the counter if enough time has passed since the last touch
        if (millis() - lastTouchTime > debounceDelay)
        {

            if (tx >= rectX && tx <= rectX + rectW && ty >= rectY && ty <= rectY + rectH)
            {
                if (page1Displayed == true)
                {
                    if (speakerisON == true)
                    {
                        ledcWriteTone(0, 2000); // Play 2.16kHz beep
                        delay(200);
                        ledcWriteTone(0, 0);
                        drawSpeakerOFF(226, 130);
                        speakerisON = false;
                        delay(600);
                    }
                    else
                    {
                        ledcWriteTone(0, 2000); // Play 2.16kHz beep
                        delay(200);
                        ledcWriteTone(0, 0);
                        speakerisON = true;
                        drawSpeakerON(226, 130);
                        delay(600);
                    }
                }
            }
            else
            {

                touchCounter++; // Increment the counter
                // If counter exceeds 6, reset it to 1
                if (touchCounter > 6)
                {
                    touchCounter = 1;
                    // Reset page display flags so that pages can be displayed again
                    page1Displayed = false;
                    page2Displayed = false;
                    page3Displayed = false;
                    page4Displayed = false;
                    page5Displayed = false;
                    page6Displayed = false;
                }
                // Call the respective functions based on the counter value
                switch (touchCounter)
                {
                case 1:
                    if (!page1Displayed)
                    {
                        tft.fillScreen(TFT_BLACK);
                        refreshBecauseReturningFromOtherPage = true;
                        updateBigClock(true);
                        page1Displayed = true; // Set the flag to prevent re-display
                    }
                    displayMainPage(); // Show page 1
                    break;
                case 2:
                    if (!page2Displayed)
                    {
                        displayAzElPlotPage();
                        AzElPlotlastRefreshTime = millis();
                        page1Displayed = false; // Set the flag to prevent re-display
                        page2Displayed = true;  // Set the flag to prevent re-display
                    }
                    break;
                case 3:

                    if (!page3Displayed)
                    {
                        displayPolarPlotPage();
                        page3Displayed = true; // Set the flag to prevent re-display
                        PolarPlotlastRefreshTime = millis();
                    }
                    break;
                case 4:
                    if (!page4Displayed)
                    {
                        displayTableNext10Passes(); // Show page 3
                        page4Displayed = true;      // Set the flag to prevent re-display
                    }
                    break;
                case 5:
                    if (!page5Displayed)
                    {
                        displayMapWithMultiPasses(); // Show page 4
                        page5Displayed = true;       // Set the flag to prevent re-display
                        multipassMaplastRefreshTime = millis();
                    }
                    break;

                case 6:
                    if (!page6Displayed)
                    {
                        if (DISPLAY_ISS_CREW == true)
                        {
                            displayPExpedition72image();
                            page6Displayed = true; // Set the flag to prevent re-display
                        }
                        else
                        {
                            touchCounter = 7;
                            tft.fillScreen(TFT_BLACK);
                            refreshBecauseReturningFromOtherPage = true;
                            displayMainPage(); // Show page 1
                        }
                    }
                    break;
                }
            }
            lastTouchTime = millis(); // Update the time of the last touch
        }
    }

    //---------------------------------------------------------------------------------------------
    // Refresh logic  outside touch handling
    if (touchCounter == 2) // AZel Plot
    {
        // Serial.println(sat.satEl);
        //  Check if 5 seconds (5000 ms) have passed since the last refresh
        if (millis() - AzElPlotlastRefreshTime >= 15000)
        {
            displayAzElPlotPage();              // Refresh the display
            AzElPlotlastRefreshTime = millis(); // Update the last refresh time
        }
    }

    if (touchCounter == 3) // Polar Plot
    {
        // Check if 5 seconds (5000 ms) have passed since the last refresh
        if (millis() - PolarPlotlastRefreshTime >= 15000)
        {
            displayPolarPlotPage();              // Refresh the display
            PolarPlotlastRefreshTime = millis(); // Update the last refresh time
        }
    }

    if (touchCounter == 5)
    {
        // Check if 5 seconds (5000 ms) have passed since the last refresh
        if (millis() - multipassMaplastRefreshTime >= 5000)
        {
            displayMapWithMultiPasses();            // Refresh the display
            multipassMaplastRefreshTime = millis(); // Update the last refresh time
        }
    }
    //--------------------------------------------------------------------------------
         // Process WebSocket events
        webSocket.loop();
   
    static unsigned long lastLoopTime = millis();
    if (millis() - lastLoopTime >= 1000 && touchCounter == 1)
    {
        displayMainPage();
        lastLoopTime = millis();
        String data = String("{\"satName\":\"") + sat.satName + "\"," +
                      "\"time\":\"" + formatTimeOnly(unixtime, true) + "\"," +
                      "\"altitude\":" + sat.satAlt + "," +
                      "\"azimuth\":" + sat.satAz + "," +
                      "\"elevation\":" + sat.satEl + "," +
                      "\"latitude\":" + sat.satLat + "," +
                      "\"longitude\":" + sat.satLon + "," +
                      "\"distance\":" + sat.satDist + "," +
                      "\"sunAzimuth\":" + sat.sunAz + "," +
                      "\"sunElevation\":" + sat.sunEl + "}";

        webSocket.broadcastTXT(data); // Send the JSON data over WebSocket
    }
    refreshBecauseReturningFromOtherPage = false;
}
