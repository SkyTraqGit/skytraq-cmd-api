/*
 * messages.c - SkyTraq AN0037 message database (curated)
 *
 * The set covers the most commonly used messages from AN0037 v1.4.69:
 *   - System control: System Restart, Set Factory Defaults, System Reboot
 *   - Software info:  Query Software Version / CRC, plus their responses
 *   - Serial / output: Configure Serial Port, Configure Message Type, Query Message Type
 *   - Position rate: Configure / Query Position Update Rate
 *   - Power: Configure (0xC) / Query (0x15) Power Mode
 *   - Masks:  DOP, Elevation & CNR
 *   - Position pinning: 0x39, 0x3A, 0x3B
 *   - NMEA talker ID: 0x4B, 0x4F
 *   - SBAS / QZSS / SAEE: 0x62 sub IDs, 0x63 sub IDs
 *   - 0x64 sub IDs: nav mode (0x17/0x18), datum (0x27/0x28), GPS time (0x20),
 *     PSTI interval (0x21/0x22), boot status (0x1)
 *   - Output IDs: 0x80, 0x81, 0x83 (ACK), 0x84 (NACK), 0x86, 0x8C, 0x93,
 *     0xAF, 0xB0, 0xB4, 0xB9
 *
 * Messages outside this list can still be sent: gnss_tool will just emit
 * the bytes the user gave on the command line and dump the response
 * frame as hex. -h won't have a structured description for them.
 */
#include "messages.h"
#include <stddef.h>

int field_type_size(field_type_t t) {
    switch (t) {
        case FT_U8: case FT_S8: return 1;
        case FT_U16: case FT_S16: return 2;
        case FT_U32: case FT_S32: case FT_SPFP: return 4;
        case FT_DPFP: return 8;
        case FT_BYTES: return 0; /* caller provides .size */
    }
    return 0;
}

/* --- 0x01  SYSTEM RESTART -------------------------------------------------- */
static const msg_field_t f_0x01[] = {
    {"1",     "Message ID",  FT_U8,  0, "",         ""},
    {"2",     "Start Mode",  FT_U8,  0, "",         "00=Reserved 01=Hot start 02=Warm 03=Cold 04=Reserved"},
    {"3-4",   "UTC Year",    FT_U16, 0, "",         ">=1980"},
    {"5",     "UTC Month",   FT_U8,  0, "",         "1~12"},
    {"6",     "UTC Day",     FT_U8,  0, "",         "1~31"},
    {"7",     "UTC Hour",    FT_U8,  0, "",         "0~23"},
    {"8",     "UTC Minute",  FT_U8,  0, "",         "0~59"},
    {"9",     "UTC Second",  FT_U8,  0, "",         "0~59"},
    {"10-11", "Latitude",    FT_S16, 0, "1/100 deg","-9000..9000  (+N -S)"},
    {"12-13", "Longitude",   FT_S16, 0, "1/100 deg","-18000..18000 (+E -W)"},
    {"14-15", "Altitude",    FT_S16, 0, "meter",    "-1000..18300"},
};

/* --- 0x02  QUERY SOFTWARE VERSION ----------------------------------------- */
static const msg_field_t f_0x02[] = {
    {"1", "Message ID",    FT_U8, 0, "", ""},
    {"2", "Software Type", FT_U8, 0, "", "00=Reserved 01=System code"},
};

/* --- 0x03  QUERY SOFTWARE CRC --------------------------------------------- */
static const msg_field_t f_0x03[] = {
    {"1", "Message ID",    FT_U8, 0, "", ""},
    {"2", "Software Type", FT_U8, 0, "", "00=Reserved 01=System code"},
};

/* --- 0x04  SET FACTORY DEFAULTS ------------------------------------------- */
static const msg_field_t f_0x04[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Type",       FT_U8, 0, "", "00=Reserved 01=Reboot after setting"},
};

/* --- 0x05  CONFIGURE SERIAL PORT ------------------------------------------ */
static const msg_field_t f_0x05[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "COM port",   FT_U8, 0, "", "00=COM1"},
    {"3", "Baud Rate",  FT_U8, 0, "", "0=4800 1=9600 2=19200 3=38400 4=57600 5=115200 6=230400 7=460800 8=921600"},
    {"4", "Attributes", FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH 2=temporary"},
};

/* --- 0x09  CONFIGURE MESSAGE TYPE ----------------------------------------- */
static const msg_field_t f_0x09[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Type",       FT_U8, 0, "", "00=No output 01=NMEA 02=Binary"},
    {"3", "Attributes", FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x0C  CONFIGURE POWER MODE ------------------------------------------- */
static const msg_field_t f_0x0C[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Mode",       FT_U8, 0, "", "00=Normal(disable) 01=Power Save(enable)"},
    {"3", "Attributes", FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH 2=temporary"},
};

/* --- 0x0E  CONFIGURE POSITION UPDATE RATE --------------------------------- */
static const msg_field_t f_0x0E[] = {
    {"1", "Message ID", FT_U8, 0, "Hz", ""},
    {"2", "Rate",       FT_U8, 0, "Hz", "1,2,4,5,8,10,20,25,40,50"},
    {"3", "Attributes", FT_U8, 0, "",   "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x10  QUERY POSITION UPDATE RATE ------------------------------------- */
static const msg_field_t f_0x10[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
};

/* --- 0x15  QUERY POWER MODE ----------------------------------------------- */
static const msg_field_t f_0x15[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
};

/* --- 0x16  QUERY MESSAGE TYPE --------------------------------------------- */
static const msg_field_t f_0x16[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
};

/* --- 0x2A  CONFIGURE DOP MASK --------------------------------------------- */
static const msg_field_t f_0x2A[] = {
    {"1",   "Message ID",    FT_U8,  0, "",  ""},
    {"2",   "DOP Mode",      FT_U8,  0, "",  "1=Auto 2=PDOP 3=HDOP 4=GDOP"},
    {"3-4", "PDOP value",    FT_U16, 0, "",  "Default 30; range 5..30 (0.1 step?)"},
    {"5-6", "HDOP value",    FT_U16, 0, "",  "Default 30"},
    {"7-8", "GDOP value",    FT_U16, 0, "",  "Default 30"},
    {"9",   "Attributes",    FT_U8,  0, "",  "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x2B  CONFIGURE ELEVATION AND CNR MASK ------------------------------- */
static const msg_field_t f_0x2B[] = {
    {"1", "Message ID",     FT_U8, 0, "",     ""},
    {"2", "Mode",           FT_U8, 0, "",     "1=Both 2=Elevation 3=CNR"},
    {"3", "Elevation Mask", FT_U8, 0, "deg",  "0..90 default 5"},
    {"4", "CNR Mask",       FT_U8, 0, "dBHz", "0..60 default 10"},
    {"5", "Attributes",     FT_U8, 0, "",     "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x2E  QUERY DOP MASK ------------------------------------------------- */
static const msg_field_t f_0x2E[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
};

/* --- 0x2F  QUERY ELEVATION AND CNR MASK ----------------------------------- */
static const msg_field_t f_0x2F[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
};

/* --- 0x39  CONFIGURE POSITION PINNING ------------------------------------- */
static const msg_field_t f_0x39[] = {
    {"1", "Message ID",       FT_U8, 0, "", ""},
    {"2", "Position pinning", FT_U8, 0, "", "0=default 1=enable 2=disable"},
    {"3", "Attributes",       FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x3A  QUERY POSITION PINNING ----------------------------------------- */
static const msg_field_t f_0x3A[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
};

/* --- 0x3B  CONFIGURE POSITION PINNING PARAMETERS -------------------------- */
static const msg_field_t f_0x3B[] = {
    {"1",     "Message ID",         FT_U8,  0, "",      ""},
    {"2-3",   "Pinning speed",      FT_U16, 0, "Km/Hr", ""},
    {"4-5",   "Pinning cnt",        FT_U16, 0, "second",""},
    {"6-7",   "Unpinning speed",    FT_U16, 0, "Km/Hr", ""},
    {"8-9",   "Unpinning cnt",      FT_U16, 0, "second",""},
    {"10-11", "Unpinning distance", FT_U16, 0, "meter", ""},
    {"12",    "Attributes",         FT_U8,  0, "",      "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x30  GET GPS EPHEMERIS ---------------------------------------------- */
static const msg_field_t f_0x30[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "SV #",       FT_U8, 0, "", "0=All SVs, 1..32=specific SV"},
};

/* --- 0x4B  CONFIGURE NMEA TALKER ID --------------------------------------- */
static const msg_field_t f_0x4B[] = {
    {"1", "Message ID",     FT_U8, 0, "", ""},
    {"2", "Talker ID type", FT_U8, 0, "", "0=GP 1=GN 2=Auto(NMEA 4.11)"},
    {"3", "Attributes",     FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x45  CONFIGURE 1PPS CABLE DELAY ------------------------------------- */
static const msg_field_t f_0x45[] = {
    {"1",   "Message ID",  FT_U8,  0, "",        ""},
    {"2-5", "Cable Delay", FT_S32, 0, "1/100 ns","-500000..+500000"},
    {"6",   "Attributes",  FT_U8,  0, "",        "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x54  CONFIGURE 1PPS TIMING ------------------------------------------ */
static const msg_field_t f_0x54[] = {
    {"1",     "Message ID",         FT_U8,   0, "",      ""},
    {"2",     "Timing Mode",        FT_U8,   0, "",      "0=PVT 1=Survey 2=Static"},
    {"3-6",   "Survey Length",      FT_U32,  0, "",      "60..1209600 (Survey mode)"},
    {"7-10",  "Standard Deviation", FT_U32,  0, "",      "3..100 (Survey mode)"},
    {"11-18", "Latitude",           FT_DPFP, 0, "deg",   "Static mode only"},
    {"19-26", "Longitude",          FT_DPFP, 0, "deg",   "Static mode only"},
    {"27-30", "Altitude",           FT_SPFP, 0, "meter", "Static mode only"},
    {"31",    "Attributes",         FT_U8,   0, "",      "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x55  CONFIGURE 1PPS OUTPUT MODE ------------------------------------- */
static const msg_field_t f_0x55[] = {
    {"1", "Message ID",   FT_U8, 0, "", ""},
    {"2", "Output Mode",  FT_U8, 0, "", "0=Reserved 1=When time available 2=Always"},
    {"3", "Align Source", FT_U8, 0, "", "0=GNSS 1=UTC"},
    {"4", "Attributes",   FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x4F  QUERY NMEA TALKER ID ------------------------------------------- */
static const msg_field_t f_0x4F[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
};

/* --- 0x5B  GET GLONASS EPHEMERIS ------------------------------------------ */
static const msg_field_t f_0x5B[] = {
    {"1", "Message ID",          FT_U8, 0, "", ""},
    {"2", "GLONASS SV slot num", FT_U8, 0, "", "0=All SVs, 1..32=specific SV"},
};

/* --- 0x5F  GET GLONASS TIME CORRECTION PARAMETERS ------------------------- */
static const msg_field_t f_0x5F[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
};

/* --- 0x67/0x02  GET BEIDOU EPHEMERIS -------------------------------------- */
static const msg_field_t f_0x67_02[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x02"},
    {"3", "SV #",       FT_U8, 0, "", "0=All SVs, 1..32=specific SV"},
};

/* --- 0x6E/0x02  GET GALILEO EPHEMERIS ------------------------------------- */
static const msg_field_t f_0x6E_02[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x02"},
    {"3", "SV #",       FT_U8, 0, "", "0=All SVs, 1..36=specific SV"},
};

/* --- 0x6F/0x04  GET IRNSS / NAVIC EPHEMERIS ------------------------------- */
static const msg_field_t f_0x6F_04[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x04"},
    {"3", "SV #",       FT_U8, 0, "", "0=All SVs, 1..14=specific SV"},
};

/* --- 0x62/0x01 CONFIGURE SBAS --------------------------------------------- */
static const msg_field_t f_0x62_01[] = {
    {"1", "Message ID",          FT_U8, 0, "", ""},
    {"2", "Sub ID",              FT_U8, 0, "", "0x01"},
    {"3", "SBAS enable",         FT_U8, 0, "", "0=Disable 1=Enable"},
    {"4", "Ranging",             FT_U8, 0, "", "0=Disable 1=Enable"},
    {"5", "Ranging URA Mask",    FT_U8, 0, "", ""},
    {"6", "Correction enable",   FT_U8, 0, "", ""},
    {"7", "Num of trk channels", FT_U8, 0, "", "1..3"},
    {"8", "Subsystem mask",      FT_U8, 0, "", "bitmap WAAS=1 EGNOS=2 MSAS=4 GAGAN=8"},
    {"9", "Attributes",          FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x62/0x02  QUERY SBAS STATUS ----------------------------------------- */
static const msg_field_t f_0x62_02[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x02"},
};

/* --- 0x62/0x03  CONFIGURE QZSS -------------------------------------------- */
static const msg_field_t f_0x62_03[] = {
    {"1", "Message ID",      FT_U8, 0, "", ""},
    {"2", "Sub ID",          FT_U8, 0, "", "0x03"},
    {"3", "QZSS Enable",     FT_U8, 0, "", "0=Disable 1=Enable"},
    {"4", "Num of channels", FT_U8, 0, "", "1..3"},
    {"5", "Attributes",      FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x62/0x04  QUERY QZSS STATUS ----------------------------------------- */
static const msg_field_t f_0x62_04[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x04"},
};

/* --- 0x62/0x05  CONFIGURE SBAS ADVANCED ----------------------------------- */
/* PRN groups are 3-byte BYTES fields. Users may pass them either as a
 * single hex literal like "838587h" (== 0x83,0x85,0x87) or as three
 * separate decimal/hex byte arguments. Either way they decode to the
 * same wire bytes.
 *
 * The on-wire layout has two firmware-dependent variants:
 *   - 27 bytes (older firmware): 13 user values
 *   - 30 bytes (newer firmware with SouthPAN PRN support): 14 user values
 *     where the 13th value is a 3-byte SouthPAN PRN, inserted between
 *     BDSBAS PRN and Attributes.
 *
 * The DB declares the 27-byte form below as the primary layout; the
 * SouthPAN PRN field is attached as an "optional trailing field" so
 * users may supply 13 OR 14 values. The runtime inserts the optional
 * field's bytes just before the Attributes byte. */
static const msg_field_t f_0x62_05[] = {
    {"1",     "Message ID",          FT_U8,    0, "", ""},
    {"2",     "Sub ID",              FT_U8,    0, "", "0x05"},
    {"3",     "Enable",              FT_U8,    0, "", "0=Disable 1=Enable"},
    {"4",     "Ranging",             FT_U8,    0, "", "0=Off 1=On 2=Auto"},
    {"5",     "Ranging URA Mask",    FT_U8,    0, "", "0..15 default 8"},
    {"6",     "Correction",          FT_U8,    0, "", "0=Disable 1=Enable"},
    {"7",     "Num of trk channels", FT_U8,    0, "", "0..3"},
    {"8",     "Subsystem mask",      FT_U8,    0, "", "bit0:WAAS bit1:EGNOS bit2:MSAS bit3:GAGAN bit4:SDCM bit5:BDSBAS bit6:SouthPAN bit7:All"},
    {"9-11",  "WAAS PRN",            FT_BYTES, 3, "", "3 PRN bytes (default 131,133,135)"},
    {"12-14", "EGNOS PRN",           FT_BYTES, 3, "", "3 PRN bytes (default 123,136,0)"},
    {"15-17", "MSAS PRN",            FT_BYTES, 3, "", "3 PRN bytes (default 137,0,0)"},
    {"18-20", "GAGAN PRN",           FT_BYTES, 3, "", "3 PRN bytes (default 127,128,132)"},
    {"21-23", "SDCM PRN",            FT_BYTES, 3, "", "3 PRN bytes (default 125,140,141)"},
    {"24-26", "BDSBAS PRN",          FT_BYTES, 3, "", "3 PRN bytes (default 130,143,144)"},
    {"27",    "Attributes",          FT_U8,    0, "", "0=SRAM 1=SRAM+FLASH"},
};
/* Optional trailing field: 3-byte SouthPAN PRN, present on newer
 * firmware. When the user supplies one extra value, the runtime
 * inserts these 3 bytes between BDSBAS PRN and Attributes, producing
 * the 30-byte payload form. */
static const msg_field_t f_0x62_05_optional_southpan = {
    "27-29 (opt)", "SouthPAN PRN", FT_BYTES, 3, "",
    "3 PRN bytes (default 122,0,0); only if firmware supports it"
};

/* --- 0x63/0x01  CONFIGURE SAEE -------------------------------------------- */
static const msg_field_t f_0x63_01[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x01"},
    {"3", "SAEE",       FT_U8, 0, "", "0=Disable 1=Enable"},
    {"4", "Attributes", FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x63/0x02  QUERY SAEE ------------------------------------------------ */
static const msg_field_t f_0x63_02[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x02"},
};

/* --- 0x64/0x01  QUERY GNSS BOOT STATUS ------------------------------------ */
static const msg_field_t f_0x64_01[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x01"},
};

/* --- 0x64/0x02  CONFIGURE EXTENDED NMEA MESSAGE INTERVAL ------------------ */
static const msg_field_t f_0x64_02[] = {
    {"1",  "Message ID",   FT_U8, 0, "",       ""},
    {"2",  "Sub ID",       FT_U8, 0, "",       "0x02"},
    {"3",  "GGA Interval", FT_U8, 0, "second", "0..255 (0=disable)"},
    {"4",  "GSA Interval", FT_U8, 0, "second", "0..255"},
    {"5",  "GSV Interval", FT_U8, 0, "second", "0..255"},
    {"6",  "GLL Interval", FT_U8, 0, "second", "0..255"},
    {"7",  "RMC Interval", FT_U8, 0, "second", "0..255"},
    {"8",  "VTG Interval", FT_U8, 0, "second", "0..255"},
    {"9",  "ZDA Interval", FT_U8, 0, "second", "0..255"},
    {"10", "GNS Interval", FT_U8, 0, "second", "0..255"},
    {"11", "GBS Interval", FT_U8, 0, "second", "0..255"},
    {"12", "GRS Interval", FT_U8, 0, "second", "0..255"},
    {"13", "DTM Interval", FT_U8, 0, "second", "0..255"},
    {"14", "GST Interval", FT_U8, 0, "second", "0..255"},
    {"15", "Attributes",   FT_U8, 0, "",       "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x64/0x06  CONFIGURE INTERFERENCE DETECTION -------------------------- */
static const msg_field_t f_0x64_06[] = {
    {"1", "Message ID",                FT_U8, 0, "", ""},
    {"2", "Sub ID",                    FT_U8, 0, "", "0x06"},
    {"3", "Interference Detect Ctrl",  FT_U8, 0, "", "0=Disable 1=Enable"},
    {"4", "Attributes",                FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x64/0x11  CONFIGURE POSITION FIX NAVIGATION MASK -------------------- */
static const msg_field_t f_0x64_11[] = {
    {"1", "Message ID",                FT_U8, 0, "", ""},
    {"2", "Sub ID",                    FT_U8, 0, "", "0x11"},
    {"3", "First fix navigation mask", FT_U8, 0, "", "0=3D 1=2D"},
    {"4", "Subsequent navigation mask",FT_U8, 0, "", "0=3D 1=2D"},
    {"5", "Attributes",                FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x64/0x17  CONFIGURE GNSS NAVIGATION MODE ---------------------------- */
static const msg_field_t f_0x64_17[] = {
    {"1", "Message ID",      FT_U8, 0, "", ""},
    {"2", "Sub ID",          FT_U8, 0, "", "0x17"},
    {"3", "Navigation mode", FT_U8, 0, "", "0=Auto 1=Pedestrian 2=Car 3=Marine 4=Balloon 5=Airborne"},
    {"4", "Attributes",      FT_U8, 0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x64/0x18  QUERY GNSS NAVIGATION MODE -------------------------------- */
static const msg_field_t f_0x64_18[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x18"},
};

/* --- 0x64/0x20  QUERY GPS TIME -------------------------------------------- */
static const msg_field_t f_0x64_20[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x20"},
};

/* --- 0x64/0x27  CONFIGURE GNSS DATUM INDEX -------------------------------- */
static const msg_field_t f_0x64_27[] = {
    {"1",   "Message ID", FT_U8,  0, "", ""},
    {"2",   "Sub ID",     FT_U8,  0, "", "0x27"},
    {"3-4", "Datum index",FT_U16, 0, "", "0..223 (default 0=WGS84)"},
    {"5",   "Attributes", FT_U8,  0, "", "0=SRAM 1=SRAM+FLASH"},
};

/* --- 0x64/0x28  QUERY GNSS DATUM INDEX ------------------------------------ */
static const msg_field_t f_0x64_28[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x28"},
};

/* --- 0x64/0x3F  SYSTEM REBOOT --------------------------------------------- */
static const msg_field_t f_0x64_3F[] = {
    {"1",     "Message ID",   FT_U8,  0, "", ""},
    {"2",     "Sub ID",       FT_U8,  0, "", "0x3F"},
    {"3",     "Reboot mode",  FT_U8,  0, "", "1=Hot 2=Warm 3=Cold"},
    {"4-5",   "UTC Year",     FT_U16, 0, "", ">=1980"},
    {"6",     "UTC Month",    FT_U8,  0, "", "1..12"},
    {"7",     "UTC Day",      FT_U8,  0, "", "1..31"},
    {"8",     "UTC Hour",     FT_U8,  0, "", "0..23"},
    {"9",     "UTC Minute",   FT_U8,  0, "", "0..59"},
    {"10",    "UTC Second",   FT_U8,  0, "", "0..59"},
    {"11-12", "Latitude",     FT_S16, 0, "1/100 deg", "-9000..9000"},
    {"13-14", "Longitude",    FT_S16, 0, "1/100 deg", "-18000..18000"},
    {"15-16", "Altitude",     FT_S16, 0, "meter",     "-1000..18300"},
};

/* --- 0x64/0x7D  QUERY VERSION EXTENSION STRING ---------------------------- */
static const msg_field_t f_0x64_7D[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x7D"},
};

/* ===========================================================================
 *   Output / response messages
 * =========================================================================== */

/* --- 0x80  SOFTWARE VERSION ----------------------------------------------- */
static const msg_field_t f_0x80[] = {
    {"1",    "Message ID",     FT_U8,  0, "", ""},
    {"2",    "Software Type",  FT_U8,  0, "", "0=Reserved 1=System"},
    {"3-6",  "Kernel Version", FT_U32, 0, "", "X1.Y1.Z1 (each byte)"},
    {"7-10", "ODM Version",    FT_U32, 0, "", "X1.Y1.Z1"},
    {"11-14","Revision",       FT_U32, 0, "", "YYMMDD"},
};

/* --- 0x81  SOFTWARE CRC --------------------------------------------------- */
static const msg_field_t f_0x81[] = {
    {"1",   "Message ID",    FT_U8,  0, "", ""},
    {"2",   "Software Type", FT_U8,  0, "", "0=Reserved 1=System"},
    {"3-4", "CRC",           FT_U16, 0, "", "CRC value"},
};

/* --- 0x83  ACK ------------------------------------------------------------ */
static const msg_field_t f_0x83[] = {
    {"1", "Message ID",                FT_U8, 0, "", ""},
    {"2", "ACK ID (request msg ID)",   FT_U8, 0, "", ""},
    /* Optional 3rd byte may carry a sub-id when ACKing sub-id messages. */
    {"3", "ACK Sub-ID (optional)",     FT_U8, 0, "", "present only when request used a Sub-ID"},
};

/* --- 0x84  NACK ----------------------------------------------------------- */
static const msg_field_t f_0x84[] = {
    {"1", "Message ID",                FT_U8, 0, "", ""},
    {"2", "NACK ID (request msg ID)",  FT_U8, 0, "", ""},
    {"3", "NACK Sub-ID (optional)",    FT_U8, 0, "", "present only when request used a Sub-ID"},
};

/* --- 0x86  POSITION UPDATE RATE ------------------------------------------- */
static const msg_field_t f_0x86[] = {
    {"1", "Message ID",      FT_U8, 0, "",   ""},
    {"2", "Update Rate",     FT_U8, 0, "Hz", "1,2,4,5,8,10,20,25,40,50"},
};

/* --- 0x90  GLONASS EPHEMERIS DATA (response to 0x5B) ---------------------- */
static const msg_field_t f_0x90[] = {
    {"1",     "Message ID",         FT_U8,    0,  "", ""},
    {"2",     "Slot number",        FT_U8,    0,  "", "GLONASS SV slot number (1..32)"},
    {"3",     "K number",           FT_S8,    0,  "", "GLONASS frequency channel (-7..+6)"},
    {"4-13",  "glo_eph_data0",      FT_BYTES, 10, "", "string 1: stuffing zeros + bits 85..1"},
    {"14-23", "glo_eph_data1",      FT_BYTES, 10, "", "string 2"},
    {"24-33", "glo_eph_data2",      FT_BYTES, 10, "", "string 3"},
    {"34-43", "glo_eph_data3",      FT_BYTES, 10, "", "string 4"},
};

/* --- 0xB1  GPS EPHEMERIS DATA (response to 0x30) -------------------------- */
/* Layout: ID(1) + SV ID(2) + Reserved(1) + 3x[ subframe payload(27) + Reserved(1) ]
 *       except the last subframe has no trailing reserved byte (87 bytes total).
 * We collapse the three subframes into one big BYTES field for readability;
 * the file output (-o) preserves every byte exactly. */
static const msg_field_t f_0xB1[] = {
    {"1",     "Message ID",     FT_U8,    0,  "", ""},
    {"2-3",   "SV ID",          FT_U16,   0,  "", "GPS satellite id (1..32)"},
    {"4",     "Reserved",       FT_U8,    0,  "", ""},
    {"5-87",  "Subframe data",  FT_BYTES, 83, "", "3 subframes, each 27B payload + 1B reserved (last has no reserved)"},
};

/* --- 0x67/0x80  BEIDOU EPHEMERIS DATA (response to 0x67/0x02) ------------- */
/* Variable size: GEO is 126 bytes payload, MEO/IGSO is 87 bytes. We declare
 * the fixed header (6 bytes) and let the trailing BYTES field swallow the
 * rest of whatever payload arrives. The proto-level reader/decoder uses the
 * payload length to bound the BYTES field at runtime. */
static const msg_field_t f_0x67_80[] = {
    {"1",     "Message ID",     FT_U8,    0,  "", ""},
    {"2",     "Sub ID",         FT_U8,    0,  "", "0x80"},
    {"3-4",   "SV ID",          FT_U16,   0,  "", "Beidou satellite id"},
    {"5",     "Type",           FT_U8,    0,  "", "0=GEO 1=MEO/IGSO"},
    {"6",     "Valid",          FT_U8,    0,  "", "0=invalid 1=valid"},
    {"7+",    "Subframe data",  FT_BYTES, 0,  "", "remaining bytes (81 for MEO/IGSO, 120 for GEO)"},
};

/* --- 0x6E/0x80  GALILEO EPHEMERIS (response to 0x6E/0x02) ----------------- */
/* 85-byte payload. */
static const msg_field_t f_0x6E_80[] = {
    {"1",     "Message ID",     FT_U8,    0,  "", ""},
    {"2",     "Sub ID",         FT_U8,    0,  "", "0x80"},
    {"3-4",   "SV ID",          FT_U16,   0,  "", "Galileo satellite id (1..36)"},
    {"5",     "Valid",          FT_U8,    0,  "", "0=invalid 1=valid"},
    {"6+",    "Subframe data",  FT_BYTES, 0,  "", "Galileo E1-B subframe data (80 bytes)"},
};

/* --- 0x6F/0x81  IRNSS EPHEMERIS DATA (response to 0x6F/0x04) -------------- */
/* 77-byte payload. */
static const msg_field_t f_0x6F_81[] = {
    {"1",     "Message ID",     FT_U8,    0,  "", ""},
    {"2",     "Sub ID",         FT_U8,    0,  "", "0x81"},
    {"3-4",   "SV ID",          FT_U16,   0,  "", "IRNSS / NavIC satellite id (1..14)"},
    {"5",     "Valid",          FT_U8,    0,  "", "0=invalid 1=valid"},
    {"6+",    "Subframe data",  FT_BYTES, 0,  "", "NavIC subframe data (72 bytes)"},
};

/* --- 0x8C  GNSS MESSAGE TYPE ---------------------------------------------- */
static const msg_field_t f_0x8C[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Type",       FT_U8, 0, "", "0=No output 1=NMEA 2=Binary"},
};

/* --- 0x93  GNSS NMEA TALKER ID -------------------------------------------- */
static const msg_field_t f_0x93[] = {
    {"1", "Message ID",     FT_U8, 0, "", ""},
    {"2", "Talker ID type", FT_U8, 0, "", "0=GP 1=GN 2=Auto"},
};

/* --- 0xAF  GNSS DOP MASK -------------------------------------------------- */
static const msg_field_t f_0xAF[] = {
    {"1",   "Message ID", FT_U8,  0, "",  ""},
    {"2",   "DOP Mode",   FT_U8,  0, "",  "1=Auto 2=PDOP 3=HDOP 4=GDOP"},
    {"3-4", "PDOP value", FT_U16, 0, "",  ""},
    {"5-6", "HDOP value", FT_U16, 0, "",  ""},
    {"7-8", "GDOP value", FT_U16, 0, "",  ""},
};

/* --- 0xB0  GNSS ELEVATION AND CNR MASK ------------------------------------ */
static const msg_field_t f_0xB0[] = {
    {"1", "Message ID",     FT_U8, 0, "",     ""},
    {"2", "Mode",           FT_U8, 0, "",     "1=Both 2=Elevation 3=CNR"},
    {"3", "Elevation Mask", FT_U8, 0, "deg",  ""},
    {"4", "CNR Mask",       FT_U8, 0, "dBHz", ""},
};

/* --- 0xB4  GNSS POSITION PINNING STATUS ----------------------------------- */
static const msg_field_t f_0xB4[] = {
    {"1", "Message ID",      FT_U8, 0, "", ""},
    {"2", "Pinning status",  FT_U8, 0, "", "0=disable 1=enable"},
};

/* --- 0xB9  GNSS POWER MODE STATUS ----------------------------------------- */
static const msg_field_t f_0xB9[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Mode",       FT_U8, 0, "", "0=Normal 1=Power save"},
};

/* --- 0x62/0x80 SBAS STATUS (response) ------------------------------------- */
static const msg_field_t f_0x62_80[] = {
    {"1", "Message ID",          FT_U8, 0, "", ""},
    {"2", "Sub ID",              FT_U8, 0, "", "0x80"},
    {"3", "SBAS enable",         FT_U8, 0, "", "0=Disable 1=Enable"},
    {"4", "Ranging",             FT_U8, 0, "", ""},
    {"5", "Ranging URA Mask",    FT_U8, 0, "", ""},
    {"6", "Correction enable",   FT_U8, 0, "", ""},
    {"7", "Num of trk channels", FT_U8, 0, "", ""},
    {"8", "Subsystem mask",      FT_U8, 0, "", "WAAS=1 EGNOS=2 MSAS=4 GAGAN=8"},
};

/* --- 0x62/0x81 QZSS STATUS (response) ------------------------------------- */
static const msg_field_t f_0x62_81[] = {
    {"1", "Message ID",      FT_U8, 0, "", ""},
    {"2", "Sub ID",          FT_U8, 0, "", "0x81"},
    {"3", "QZSS Enable",     FT_U8, 0, "", ""},
    {"4", "Num of channels", FT_U8, 0, "", ""},
};

/* --- 0x63/0x80 SAEE STATUS (response) ------------------------------------- */
static const msg_field_t f_0x63_80[] = {
    {"1", "Message ID", FT_U8, 0, "", ""},
    {"2", "Sub ID",     FT_U8, 0, "", "0x80"},
    {"3", "SAEE",       FT_U8, 0, "", "0=Disable 1=Enable"},
};

/* --- 0x64/0x80 GNSS BOOT STATUS (response) -------------------------------- */
static const msg_field_t f_0x64_80[] = {
    {"1", "Message ID",  FT_U8, 0, "", ""},
    {"2", "Sub ID",      FT_U8, 0, "", "0x80"},
    {"3", "Boot Status", FT_U8, 0, "", "0=Boot loader 1=Internal flash 2=External flash"},
};

/* --- 0x64/0x8B GNSS NAVIGATION MODE (response) ---------------------------- */
static const msg_field_t f_0x64_8B[] = {
    {"1", "Message ID",      FT_U8, 0, "", ""},
    {"2", "Sub ID",          FT_U8, 0, "", "0x8B"},
    {"3", "Navigation mode", FT_U8, 0, "", "0=Auto 1=Pedestrian 2=Car 3=Marine 4=Balloon 5=Airborne"},
};

/* --- 0x64/0x8E GPS TIME (response) ---------------------------------------- */
static const msg_field_t f_0x64_8E[] = {
    {"1",     "Message ID",          FT_U8,  0, "",       ""},
    {"2",     "Sub ID",               FT_U8,  0, "",       "0x8E"},
    {"3",     "Default leap seconds", FT_U8,  0, "second", ""},
    {"4-5",   "Week number",          FT_U16, 0, "",       "GPS week"},
    {"6-9",   "TOW",                  FT_U32, 0, "1/100 s","Time of week"},
    {"10",    "Current leap seconds", FT_U8,  0, "second", ""},
    {"11",    "Validation",           FT_U8,  0, "",       "0=Default leap 1=From SV"},
};

/* --- 0x64/0x92  GNSS DATUM INDEX (response) -------------------------------- */
static const msg_field_t f_0x64_92[] = {
    {"1",   "Message ID", FT_U8,  0, "", ""},
    {"2",   "Sub ID",     FT_U8,  0, "", "0x92"},
    {"3-4", "Datum index",FT_U16, 0, "", ""},
};

/* --- 0x64/0xFE  VERSION EXTENSION STRING ---------------------------------- */
static const msg_field_t f_0x64_FE[] = {
    {"1",   "Message ID", FT_U8,  0, "", ""},
    {"2",   "Sub ID",     FT_U8,  0, "", "0xFE"},
    {"3+",  "Extension string (ASCII)", FT_BYTES, 0, "", "Variable length, terminated by frame end"},
};

/* ===========================================================================
 *   Master table
 * =========================================================================== */
#define E(arr) (arr), (int)(sizeof(arr)/sizeof((arr)[0]))

static const msg_def_t TABLE[] = {
    /* Input - Set/Configure */
    { 0x01, 0,0, MSG_INPUT_SET,   "SYSTEM RESTART",
      "Force system to restart",                          -1,0,0, E(f_0x01) },
    { 0x04, 0,0, MSG_INPUT_SET,   "SET FACTORY DEFAULTS",
      "Set system to factory default values",             -1,0,0, E(f_0x04) },
    { 0x05, 0,0, MSG_INPUT_SET,   "CONFIGURE SERIAL PORT",
      "Configure serial port baud rate",                  -1,0,0, E(f_0x05) },
    { 0x09, 0,0, MSG_INPUT_SET,   "CONFIGURE MESSAGE TYPE",
      "Configure output message type",                    -1,0,0, E(f_0x09) },
    { 0x0C, 0,0, MSG_INPUT_SET,   "CONFIGURE POWER MODE",
      "Set system power mode",                            -1,0,0, E(f_0x0C) },
    { 0x0E, 0,0, MSG_INPUT_SET,   "CONFIGURE SYSTEM POSITION RATE",
      "Configure position update rate",                   -1,0,0, E(f_0x0E) },
    { 0x2A, 0,0, MSG_INPUT_SET,   "CONFIGURE DOP MASK",
      "Configure values of DOP mask",                     -1,0,0, E(f_0x2A) },
    { 0x2B, 0,0, MSG_INPUT_SET,   "CONFIGURE ELEVATION AND CNR MASK",
      "Configure values of Elevation and CNR mask",       -1,0,0, E(f_0x2B) },
    { 0x39, 0,0, MSG_INPUT_SET,   "CONFIGURE POSITION PINNING",
      "Enable / disable position pinning",                -1,0,0, E(f_0x39) },
    { 0x3B, 0,0, MSG_INPUT_SET,   "CONFIGURE POSITION PINNING PARAMETERS",
      "Set pinning parameters",                           -1,0,0, E(f_0x3B) },
    { 0x45, 0,0, MSG_INPUT_SET,   "CONFIGURE 1PPS CABLE DELAY",
      "Configure cable delay of 1PPS timing",             -1,0,0, E(f_0x45) },
    { 0x4B, 0,0, MSG_INPUT_SET,   "CONFIGURE NMEA TALKER ID",
      "Configure NMEA talker ID",                         -1,0,0, E(f_0x4B) },
    { 0x54, 0,0, MSG_INPUT_SET,   "CONFIGURE 1PPS TIMING",
      "Configure 1PPS timing of GNSS receiver",           -1,0,0, E(f_0x54) },
    { 0x55, 0,0, MSG_INPUT_SET,   "CONFIGURE 1PPS OUTPUT MODE",
      "Configure 1PPS output mode of GNSS receiver",      -1,0,0, E(f_0x55) },

    /* Input - Query */
    { 0x02, 0,0, MSG_INPUT_QUERY, "QUERY SOFTWARE VERSION",
      "Query loaded software version", 0x80,0,0,             E(f_0x02) },
    { 0x03, 0,0, MSG_INPUT_QUERY, "QUERY SOFTWARE CRC",
      "Query loaded software CRC",     0x81,0,0,             E(f_0x03) },
    { 0x10, 0,0, MSG_INPUT_QUERY, "QUERY POSITION UPDATE RATE",
      "Query position update rate",    0x86,0,0,             E(f_0x10) },
    { 0x15, 0,0, MSG_INPUT_QUERY, "QUERY POWER MODE",
      "Query power mode status",       0xB9,0,0,             E(f_0x15) },
    { 0x16, 0,0, MSG_INPUT_QUERY, "QUERY MESSAGE TYPE",
      "Query output message type",     0x8C,0,0,             E(f_0x16) },
    { 0x2E, 0,0, MSG_INPUT_QUERY, "QUERY DOP MASK",
      "Query DOP mask",                0xAF,0,0,             E(f_0x2E) },
    { 0x2F, 0,0, MSG_INPUT_QUERY, "QUERY ELEVATION AND CNR MASK",
      "Query elevation/CNR mask",      0xB0,0,0,             E(f_0x2F) },
    { 0x30, 0,0, MSG_INPUT_QUERY, "GET GPS EPHEMERIS",
      "Retrieve GPS ephemeris data (SV=0 dumps all SVs as multiple 0xB1 frames)",
                                        0xB1,0,0,             E(f_0x30) },
    { 0x3A, 0,0, MSG_INPUT_QUERY, "QUERY POSITION PINNING",
      "Query position pinning status", 0xB4,0,0,             E(f_0x3A) },
    { 0x4F, 0,0, MSG_INPUT_QUERY, "QUERY NMEA TALKER ID",
      "Query NMEA talker ID",          0x93,0,0,             E(f_0x4F) },
    { 0x5B, 0,0, MSG_INPUT_QUERY, "GET GLONASS EPHEMERIS",
      "Retrieve GLONASS ephemeris data (slot=0 dumps all SVs as multiple 0x90 frames)",
                                        0x90,0,0,             E(f_0x5B) },
    { 0x5F, 0,0, MSG_INPUT_QUERY, "GET GLONASS TIME CORRECTION PARAMETERS",
      "Retrieve GLONASS time correction parameters",
                                        0x92,0,0,             E(f_0x5F) },

    /* Input with sub-IDs */
    { 0x62, 1,0x01, MSG_INPUT_SET,   "CONFIGURE SBAS",
      "Configure SBAS",                                   -1,0,0,    E(f_0x62_01) },
    { 0x62, 1,0x02, MSG_INPUT_QUERY, "QUERY SBAS STATUS",
      "Query SBAS status",                                0x62,1,0x80, E(f_0x62_02) },
    { 0x62, 1,0x03, MSG_INPUT_SET,   "CONFIGURE QZSS",
      "Configure QZSS",                                   -1,0,0,    E(f_0x62_03) },
    { 0x62, 1,0x05, MSG_INPUT_SET,   "CONFIGURE SBAS ADVANCED",
      "Configure SBAS advanced functions",                -1,0,0,    E(f_0x62_05),
      &f_0x62_05_optional_southpan },
    { 0x62, 1,0x04, MSG_INPUT_QUERY, "QUERY QZSS STATUS",
      "Query QZSS status",                                0x62,1,0x81, E(f_0x62_04) },
    { 0x63, 1,0x01, MSG_INPUT_SET,   "CONFIGURE SAEE",
      "Configure SAEE",                                   -1,0,0,    E(f_0x63_01) },
    { 0x63, 1,0x02, MSG_INPUT_QUERY, "QUERY SAEE STATUS",
      "Query SAEE status",                                0x63,1,0x80, E(f_0x63_02) },
    { 0x67, 1,0x02, MSG_INPUT_QUERY, "GET BEIDOU EPHEMERIS",
      "Retrieve Beidou ephemeris data (SV=0 dumps all SVs as multiple 0x67/0x80 frames)",
                                                          0x67,1,0x80, E(f_0x67_02) },
    { 0x6E, 1,0x02, MSG_INPUT_QUERY, "GET GALILEO EPHEMERIS",
      "Retrieve Galileo ephemeris data (SV=0 dumps all SVs as multiple 0x6E/0x80 frames)",
                                                          0x6E,1,0x80, E(f_0x6E_02) },
    { 0x6F, 1,0x04, MSG_INPUT_QUERY, "GET IRNSS / NAVIC EPHEMERIS",
      "Retrieve IRNSS/NavIC ephemeris data (SV=0 dumps all SVs as multiple 0x6F/0x81 frames)",
                                                          0x6F,1,0x81, E(f_0x6F_04) },
    { 0x64, 1,0x01, MSG_INPUT_QUERY, "QUERY GNSS BOOT STATUS",
      "Query boot status",                                0x64,1,0x80, E(f_0x64_01) },
    { 0x64, 1,0x17, MSG_INPUT_SET,   "CONFIGURE GNSS NAVIGATION MODE",
      "Configure navigation mode",                        -1,0,0,    E(f_0x64_17) },
    { 0x64, 1,0x02, MSG_INPUT_SET,   "CONFIGURE EXTENDED NMEA MESSAGE INTERVAL",
      "Configure extended NMEA message intervals",        -1,0,0,    E(f_0x64_02) },
    { 0x64, 1,0x06, MSG_INPUT_SET,   "CONFIGURE INTERFERENCE DETECTION",
      "Configure interference detection",                 -1,0,0,    E(f_0x64_06) },
    { 0x64, 1,0x11, MSG_INPUT_SET,   "CONFIGURE POSITION FIX NAVIGATION MASK",
      "Configure 2D/3D position fix navigation mask",     -1,0,0,    E(f_0x64_11) },
    { 0x64, 1,0x18, MSG_INPUT_QUERY, "QUERY GNSS NAVIGATION MODE",
      "Query navigation mode",                            0x64,1,0x8B, E(f_0x64_18) },
    { 0x64, 1,0x20, MSG_INPUT_QUERY, "QUERY GPS TIME",
      "Query GPS time",                                   0x64,1,0x8E, E(f_0x64_20) },
    { 0x64, 1,0x27, MSG_INPUT_SET,   "CONFIGURE GNSS DATUM INDEX",
      "Configure datum index",                            -1,0,0,    E(f_0x64_27) },
    { 0x64, 1,0x28, MSG_INPUT_QUERY, "QUERY GNSS DATUM INDEX",
      "Query datum index",                                0x64,1,0x92, E(f_0x64_28) },
    { 0x64, 1,0x3F, MSG_INPUT_SET,   "SYSTEM REBOOT",
      "Reboot system",                                    -1,0,0,    E(f_0x64_3F) },
    { 0x64, 1,0x7D, MSG_INPUT_QUERY, "QUERY VERSION EXTENSION STRING",
      "Query version extension string",                   0x64,1,0xFE, E(f_0x64_7D) },

    /* Outputs / responses */
    { 0x80, 0,0,    MSG_OUTPUT, "SOFTWARE VERSION",
      "Software version of the GNSS receiver",            -1,0,0, E(f_0x80) },
    { 0x81, 0,0,    MSG_OUTPUT, "SOFTWARE CRC",
      "Software CRC of the GNSS receiver",                -1,0,0, E(f_0x81) },
    { 0x83, 0,0,    MSG_OUTPUT, "ACK",
      "Acknowledgement to a request",                     -1,0,0, E(f_0x83) },
    { 0x84, 0,0,    MSG_OUTPUT, "NACK",
      "Negative acknowledgement",                         -1,0,0, E(f_0x84) },
    { 0x86, 0,0,    MSG_OUTPUT, "POSITION UPDATE RATE",
      "Position update rate",                             -1,0,0, E(f_0x86) },
    { 0x90, 0,0,    MSG_OUTPUT, "GLONASS EPHEMERIS DATA",
      "GLONASS ephemeris data (response to 0x5B)",        -1,0,0, E(f_0x90) },
    { 0x8C, 0,0,    MSG_OUTPUT, "GNSS MESSAGE TYPE",
      "Output message type",                              -1,0,0, E(f_0x8C) },
    { 0x93, 0,0,    MSG_OUTPUT, "GNSS NMEA TALKER ID",
      "NMEA talker ID",                                   -1,0,0, E(f_0x93) },
    { 0xAF, 0,0,    MSG_OUTPUT, "GNSS DOP MASK",
      "DOP mask in use",                                  -1,0,0, E(f_0xAF) },
    { 0xB0, 0,0,    MSG_OUTPUT, "GNSS ELEVATION AND CNR MASK",
      "Elevation / CNR mask",                             -1,0,0, E(f_0xB0) },
    { 0xB4, 0,0,    MSG_OUTPUT, "GNSS POSITION PINNING STATUS",
      "Position pinning status",                          -1,0,0, E(f_0xB4) },
    { 0xB9, 0,0,    MSG_OUTPUT, "GNSS POWER MODE STATUS",
      "Power mode status",                                -1,0,0, E(f_0xB9) },
    { 0xB1, 0,0,    MSG_OUTPUT, "GPS EPHEMERIS DATA",
      "GPS ephemeris data (response to 0x30)",            -1,0,0, E(f_0xB1) },
    { 0x62, 1,0x80, MSG_OUTPUT, "SBAS STATUS",
      "SBAS status",                                      -1,0,0, E(f_0x62_80) },
    { 0x62, 1,0x81, MSG_OUTPUT, "QZSS STATUS",
      "QZSS status",                                      -1,0,0, E(f_0x62_81) },
    { 0x63, 1,0x80, MSG_OUTPUT, "SAEE STATUS",
      "SAEE status",                                      -1,0,0, E(f_0x63_80) },
    { 0x67, 1,0x80, MSG_OUTPUT, "BEIDOU EPHEMERIS DATA",
      "Beidou ephemeris data (response to 0x67/0x02)",    -1,0,0, E(f_0x67_80) },
    { 0x6E, 1,0x80, MSG_OUTPUT, "GALILEO EPHEMERIS",
      "Galileo ephemeris (response to 0x6E/0x02)",        -1,0,0, E(f_0x6E_80) },
    { 0x6F, 1,0x81, MSG_OUTPUT, "IRNSS EPHEMERIS DATA",
      "IRNSS / NavIC ephemeris (response to 0x6F/0x04)",  -1,0,0, E(f_0x6F_81) },
    { 0x64, 1,0x80, MSG_OUTPUT, "GNSS BOOT STATUS",
      "Boot status",                                      -1,0,0, E(f_0x64_80) },
    { 0x64, 1,0x8B, MSG_OUTPUT, "GNSS NAVIGATION MODE",
      "Navigation mode",                                  -1,0,0, E(f_0x64_8B) },
    { 0x64, 1,0x8E, MSG_OUTPUT, "GPS TIME",
      "GPS time",                                         -1,0,0, E(f_0x64_8E) },
    { 0x64, 1,0x92, MSG_OUTPUT, "GNSS DATUM INDEX",
      "Datum index",                                      -1,0,0, E(f_0x64_92) },
    { 0x64, 1,0xFE, MSG_OUTPUT, "VERSION EXTENSION STRING",
      "Version extension string",                         -1,0,0, E(f_0x64_FE) },
};

const msg_def_t *msg_lookup(uint8_t id, int has_subid, uint8_t sub_id) {
    for (size_t i = 0; i < sizeof(TABLE)/sizeof(TABLE[0]); i++) {
        const msg_def_t *m = &TABLE[i];
        if (m->id != id) continue;
        if (has_subid != m->has_subid) continue;
        if (has_subid && m->sub_id != sub_id) continue;
        return m;
    }
    return NULL;
}

const msg_def_t *msg_lookup_id(uint8_t id) {
    for (size_t i = 0; i < sizeof(TABLE)/sizeof(TABLE[0]); i++) {
        if (TABLE[i].id == id) return &TABLE[i];
    }
    return NULL;
}

const msg_def_t *msg_iter(int index) {
    int n = (int)(sizeof(TABLE)/sizeof(TABLE[0]));
    if (index < 0 || index >= n) return NULL;
    return &TABLE[index];
}
