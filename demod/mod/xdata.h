/* Rewritten from SondeHub XDATA Parser Library (Authors: Mark Jessop & Luke Prior)
 *
 * Pavol Gajdos
 */

// Pump Efficiency Correction Parameters for ECC-6A Ozone Sensor, with 3.0cm^3 volume.
// We are using these as a nominal correction value for pump efficiency vs pressure
// 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "asprintf.h"

float OIF411_Cef_Pressure[] =    {    0,     2,     3,      5,    10,    20,    30,    50,   100,   200,   300,   500, 1000, 1100};
float OIF411_Cef[] = { 1.171, 1.171, 1.131, 1.092, 1.055, 1.032, 1.022, 1.015, 1.011, 1.008, 1.006, 1.004,    1,    1};

typedef struct {
    int instrument_number;
    char *serial;
    char *diagnostics;
    int version;
    float ozone_pump_temp;
    float ozone_current_uA;
    float ozone_battery_v;
    float ozone_pump_curr_mA;
    float ext_voltage;
    float O3_partial_pressure; 
    char *data_type;
} output_oif411;

typedef struct {
    int instrument_number;
} output_cfh;

typedef struct {
    int instrument_number;
    int sonde_number;
    int internal_temperature;
    int blue_backscatter;
    int red_backscatter;
    int blue_monitor;
    int red_monitor;
} output_cobald;

typedef struct {
    int instrument_number;
    int mirror_temperature;
    float scattered_light;
    float peltier_current;
    float heatsink_temperature;
    float circuit_board_temperature;
    int battery;
    int pid;
    char parameterType;
    int serial_number;
    int coefficient_b;
    int coefficient_c;
    int coefficient_d;
    int coefficient_e;
    int firmware_version;
} output_skydew;

typedef struct {
    int instrument_number;
    char *packetID;
    int serial_number;
    char *temperature_pcb_date;
    char *main_pcb_date;
    char *controller_fw_date;
    char *fpga_fw_date;
    float frost_point_mirror_temperature;
    float peltier_hot_side_temperature;
    float air_temperature;
    float anticipated_frost_point_mirror_temperature;
    float frost_point_mirror_reflectance;
    float reference_surface_reflectance;
    float reference_surface_heating_current;
    float peltier_current;
    float heat_sink_temperature_01;
    float reference_surface_temperature_01;
    float heat_sink_temperature_02;
    float reference_surface_temperature_02;
    float thermocouple_reference_temperature;
    float reserved_temperature;
    float clean_frost_point_mirror_reflectance_01;
    float clean_reference_surface_reflectance_01;
    float clean_frost_point_mirror_reflectance_02;
    float clean_reference_surface_reflectance_02;
    float v6_analog_supply_battery_voltage;
    float v45_logic_supply_battery_voltage;
    float v45_peltier_and_heater_supply_battery_voltage;
} output_pcfh;

typedef struct {
    int instrument_number;
    int photomultiplier_background_counts;
    int photomultiplier_counts;
    float photomultiplier_temperature;
    float battery_voltage;
    float yuv_current;
    float pmt_voltage;
    float firmware_version;
    int production_year;
    int hardware_version;
} output_flashb;

float alt2press(float h){
    //convert altitude (m) to pressure (hPa) based on NOAA ISA model
    float press=1013.25*pow(1-h/44307.694,5.2553);    
    return press;
}

float lerp(float x, float y, float a){
    // Helper function for linear interpolation between two points
    return x * (1 - a) + y * a;
}

float get_oif411_Cef(float pressure){
    // Get the Pump efficiency correction value for a given pressure.
    
    int length = sizeof OIF411_Cef_Pressure / sizeof *OIF411_Cef_Pressure;
    
    // without value
    if (pressure <= 0){
        return 1;
    }

    // Off-scale use bottom-end value
    if (pressure <= OIF411_Cef_Pressure[0]){
        return OIF411_Cef[0];
    }
    
    // Off-scale top, use top-end value
    if (pressure >= OIF411_Cef_Pressure[length-1]){
        return OIF411_Cef[length-1];
    }
    

    // Within the correction range, perform linear interpolation.
    int i;
    for(i= 1; i<length; i++){
        if (pressure < OIF411_Cef_Pressure[i]) {
            return lerp(OIF411_Cef[i-1], OIF411_Cef[i],  ( (pressure-OIF411_Cef_Pressure[i-1]) / (OIF411_Cef_Pressure[i]-OIF411_Cef_Pressure[i-1])) );
        }
    }

    // Otherwise, bomb out and return 1.0
    return 1;
}

void parseOIF411(output_oif411 *output, char *xdata, float pressure){
    // Attempt to parse an XDATA string from an OIF411 Ozone Sounder
    // Returns an object with parameters to be added to the sondes telemetry.
    //
    // References: 
    // https://www.vaisala.com/sites/default/files/documents/Ozone%20Sounding%20with%20Vaisala%20Radiosonde%20RS41%20User%27s%20Guide%20M211486EN-C.pdf
    //
    // Sample data:      0501036402B958B07500   (length = 20 characters)
    // More sample data: 0501R20234850000006EI  (length = 21 characters)

    // Run some checks over the input
    int length=strlen(xdata);
    if (length < 20){
        // Invalid OIF411 dataset
        return;
    }

    char tmp[30];
    memcpy(tmp,&xdata[0],2);
    tmp[2]='\0';
    //printf("-%s-\n",tmp);
    if (strcmp(tmp,"05") != 0){
        // Not an OIF411 (shouldn't get here)
        return;
    }

    // Instrument number is common to all XDATA types.
    memcpy(tmp,&xdata[2],2);
    tmp[2]='\0';
    output->instrument_number = (int)strtol(tmp, NULL, 16);

    if(length == 21){
        // ID Data (Table 19)
        output->data_type = "ID Data";
        // Serial number
        memcpy(tmp,&xdata[4],8);
        tmp[8]='\0';
        asprintf(&output->serial,"%s",tmp);

        // Diagnostics word. 
        char diagnostics_word[4];
        memcpy(diagnostics_word,&xdata[12],4);
        diagnostics_word[4]='\0';
        if(strcmp(diagnostics_word,"0000") == 0){
            output->diagnostics = "All OK";
        }else if(strcmp(diagnostics_word,"0004") == 0){
            output->diagnostics = "Ozone pump temperature below -5 C.";
        }else if(strcmp(diagnostics_word,"0400") == 0){
            output->diagnostics = "Ozone pump battery voltage (+VBatt) is not connected to OIF411";
        }else if (strcmp(diagnostics_word,"0404") == 0){
            output->diagnostics = "Ozone pump temp low, and +VBatt not connected.";
        }else {
            asprintf(&output->diagnostics,"Unknown State: %s",diagnostics_word);
        }

        // Version number  
        memcpy(tmp,&xdata[16],4);
        tmp[4]='\0';
        output->version = (int)strtol(tmp, NULL, 16);
    } 
    else if (length == 20){
        // Measurement Data (Table 18)
        output->data_type = "Measurement Data";
        // Ozone pump temperature - signed int16  
        memcpy(tmp,&xdata[4],4);
        tmp[4]='\0';
        int ozone_pump_temp = (int)strtol(tmp, NULL, 16);
        if ((ozone_pump_temp & 0x8000) > 0) {
            ozone_pump_temp = ozone_pump_temp - 0x10000;
        }
        output->ozone_pump_temp = (float)ozone_pump_temp*0.01; // Degrees C (5 - 35)
        
        // Ozone Current
        memcpy(tmp,&xdata[8],5);
        tmp[5]='\0';
        output->ozone_current_uA = ((int)strtol(tmp, NULL, 16))*0.0001; // micro-Amps (0.05 - 30)
        
        // Battery Voltage
        memcpy(tmp,&xdata[13],2);
        tmp[2]='\0';
        output->ozone_battery_v = ((int)strtol(tmp, NULL, 16))*0.1; // Volts (14 - 19)
        
        // Ozone Pump Current
        memcpy(tmp,&xdata[15],3);
        tmp[3]='\0';
        output->ozone_pump_curr_mA = (float)((int)strtol(tmp, NULL, 16)); // mA (30 - 110)

        // External Voltage
        memcpy(tmp,&xdata[18],2);
        tmp[2]='\0';
        output->ext_voltage = ((int)strtol(tmp, NULL, 16))*0.1; // Volts

        //Now attempt to calculate the O3 partial pressure

        // Calibration values
        float Ibg = 0.0; // The BOM appear to use a Ozone background current value of 0 uA
        float Cef = get_oif411_Cef(pressure); // Calculate the pump efficiency correction.
        float FlowRate = 28.5; // Use a 'nominal' value for Flow Rate (seconds per 100mL).

        output->O3_partial_pressure = (4.30851e-4)*(output->ozone_current_uA - Ibg)*(output->ozone_pump_temp+273.15)*FlowRate*Cef; // mPa
    }   
}

void parseCFH(output_cfh *output, char* xdata){
    // Attempt to parse an XDATA string from a CFH Cryogenic Frostpoint Hygrometer
    // Returns an object with parameters to be added to the sondes telemetry.
    //
    // References: 
    // https://eprints.lib.hokudai.ac.jp/dspace/bitstream/2115/72249/1/GRUAN-TD-5_MeiseiRadiosondes_v1_20180221.pdf
    //
    // Sample data:      0802E21FFD85C8CE078A0193   (length = 24 characters)

    // Run some checks over the input
    int length=strlen(xdata);
    if (length != 24){
        // Invalid CFH dataset
        return;
    }

    char tmp[30];
    memcpy(tmp,&xdata[0],2);
    tmp[2]='\0';
    //printf("-%s-\n",tmp);
    if (strcmp(tmp,"08") != 0){
        // Not an CFH (shouldn't get here)
        return;
    }

    // Instrument number is common to all XDATA types.
    memcpy(tmp,&xdata[2],2);
    tmp[2]='\0';
    output->instrument_number = (int)strtol(tmp, NULL, 16);
}

void parseCOBALD(output_cobald *output,char* xdata) {
    // Attempt to parse an XDATA string from a COBALD Compact Optical Backscatter Aerosol Detector
    // Returns an object with parameters to be added to the sondes telemetry.
    //
    // References: 
    // https://hobbydocbox.com/Radio/83430839-Cobald-operating-instructions-imet-configuration.html
    //
    // Sample data:      190213fffe005fcf00359943912cca   (length = 30 characters)

    // Run some checks over the input
    int length=strlen(xdata);
    if (length != 30){
        // Invalid COBALD dataset
        return;
    }

    char tmp[30];
    memcpy(tmp,&xdata[0],2);
    tmp[2]='\0';
    //printf("-%s-\n",tmp);
    if (strcmp(tmp,"19") != 0){
        // Not a COBALD (shouldn't get here)
        return;
    }


    // Instrument number is common to all XDATA types.
    memcpy(tmp,&xdata[2],2);
    tmp[2]='\0';
    output->instrument_number = (int)strtol(tmp, NULL, 16);

    // Sonde number
    memcpy(tmp,&xdata[4],3);
    tmp[3]='\0';
    output->sonde_number = (int)strtol(tmp, NULL, 16);

    // Internal temperature
    memcpy(tmp,&xdata[7],3);
    tmp[3]='\0';
    int internal_temperature = (int)strtol(tmp, NULL, 16);
    if ((internal_temperature  & 0x800) > 0) {
        internal_temperature  = internal_temperature  - 0x1000;
    }
    output->internal_temperature = internal_temperature/8; // Degrees C (-40 - 50)

    // Blue backscatter
    memcpy(tmp,&xdata[10],6);
    tmp[6]='\0';
    int blue_backscatter = (int)strtol(tmp, NULL, 16);
    if ((blue_backscatter  & 0x800000) > 0) {
        blue_backscatter  = blue_backscatter  - 0x1000000;
    }
    output->blue_backscatter = blue_backscatter; // (0 - 1000000)
    
    // Red backckatter
    memcpy(tmp,&xdata[16],6);
    tmp[6]='\0';
    int red_backscatter = (int)strtol(tmp, NULL, 16);
    if ((red_backscatter  & 0x800000) > 0) {
        red_backscatter  = red_backscatter  - 0x1000000;
    }
    output->red_backscatter = red_backscatter; // (0 - 1000000)

    // Blue monitor
    memcpy(tmp,&xdata[22],4);
    tmp[4]='\0';
    int blue_monitor = (int)strtol(tmp, NULL, 16);
    if ((blue_monitor  & 0x8000) > 0) {
        blue_monitor  = blue_monitor  - 0x10000;
    }
    output->blue_monitor = blue_monitor; // (-32768 - 32767)
    
    // Red monitor
    memcpy(tmp,&xdata[26],4);
    tmp[4]='\0';
    int red_monitor = (int)strtol(tmp, NULL, 16);
    if ((red_monitor  & 0x8000) > 0) {
        red_monitor  = red_monitor  - 0x10000;
    }
    output->red_monitor = red_monitor; // (-32768 - 32767)
}

void parseSKYDEW(output_skydew *output,char* xdata) {
    // Attempt to parse an XDATA string from a SKYDEW Peltier-based chilled-mirror hygrometer
    // Returns an object with parameters to be added to the sondes telemetry.
    //
    // References: 
    // https://www.gruan.org/gruan/editor/documents/meetings/icm-12/pres/pres_306_Sugidachi_SKYDEW.pdf
    //
    // Sample data:      3F0141DF73B940F600150F92FF27D5C8304102   (length = 38 characters)

    // Run some checks over the input
    
    int length=strlen(xdata);
    if (length != 38){
        // Invalid SKYDEW dataset
        return;
    }

    char tmp[40];
    memcpy(tmp,&xdata[0],2);
    tmp[2]='\0';
    //printf("-%s-\n",tmp);
    if (strcmp(tmp,"3F") != 0){
        // Not a SKYDEW (shouldn't get here)
        return;
    }

    // Instrument number is common to all XDATA types.
    memcpy(tmp,&xdata[2],2);
    tmp[2]='\0';
    output->instrument_number = (int)strtol(tmp, NULL, 16);

    
    // Mirror temperature value
    // This requires the four coefficients to actually get a value
    memcpy(tmp,&xdata[4],4);
    tmp[4]='\0';
    output->mirror_temperature = (int)strtol(tmp, NULL, 16);

    // Scattered light level
    memcpy(tmp,&xdata[8],4);
    tmp[4]='\0';
    output->scattered_light = (int)strtol(tmp, NULL, 16)*0.0000625; // V

    // Reference resistance
    // Used to calculate mirror temperature
    memcpy(tmp,&xdata[12],4);
    tmp[4]='\0';
    int reference_resistance = (int)strtol(tmp, NULL, 16);

    // Offset value
    // Used to calculate mirror temperature
    memcpy(tmp,&xdata[16],4);
    tmp[4]='\0';
    int offset_value = (int)strtol(tmp, NULL, 16);

    // Peltier current
    memcpy(tmp,&xdata[20],4);
    tmp[4]='\0';
    output->peltier_current = ((int)strtol(tmp, NULL, 16)*0.00040649414 - 1.5)*2; // A

    // Heatsink temperature
    memcpy(tmp,&xdata[24],2);
    tmp[2]='\0';
    output->heatsink_temperature = 1./((((log((((int)strtol(tmp, NULL, 16)/8192.)*141.9)/(3.3-((int)strtol(tmp, NULL, 16)/8192.)*3.3)/6))/3390)+1)/273.16)-276.16; // Degrees C

    // Circuit board temperature
    memcpy(tmp,&xdata[26],2);
    tmp[2]='\0';
    output->circuit_board_temperature = 1./((((log((((int)strtol(tmp, NULL, 16)/8192.)*39.6)/(3.3-((int)strtol(tmp, NULL, 16)/8192.)*3.3)/6))/3390)+1)/273.16)-276.16; // Degrees C

    // Battery
    memcpy(tmp,&xdata[28],2);
    tmp[2]='\0';
    output->battery = (int)strtol(tmp, NULL, 16);

    // PID
    memcpy(tmp,&xdata[30],2);
    tmp[2]='\0';
    output->pid = (int)strtol(tmp, NULL, 16);

    // Parameter
    memcpy(tmp,&xdata[32],4);
    tmp[4]='\0';
    int parameter = (int)strtol(tmp, NULL, 16);

    // Coefficent type
    memcpy(tmp,&xdata[36],2);
    tmp[2]='\0';
    output->parameterType = (int)strtol(tmp, NULL, 16);

    // Parameter Type
    switch(output->parameterType) {
        case 0: 
            output->serial_number = parameter;
            break;
        case 1: 
            output->coefficient_b = parameter;
            break;
        case 2: 
            output->coefficient_c = parameter;
            break;
        case 3: 
            output->coefficient_d = parameter;
            break;
        case 4: 
            output->coefficient_e = parameter;
            break;
        case 5: 
            output->firmware_version = parameter;
            break;
    }
}

char* getPCFHdate(char *code) {
    // months reference list
    char* PCFHmonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // Get year from first character
    char tmp[2];
    
    memcpy(tmp,&code[0],1);
    tmp[1]='\0';
    int year = (int)strtol(tmp, NULL, 16);
    year = year + 2016;

    // Get month from second character
    memcpy(tmp,&code[1],1);
    tmp[1]='\0';
    int month = (int)strtol(tmp, NULL, 16);

    // Generate string
    char* part_date;
    asprintf(&part_date,"%s %d",PCFHmonths[month-1],year);
    return part_date;
}

void parsePCFH(output_pcfh *output, char *xdata) {
    // Attempt to parse an XDATA string from a Peltier Cooled Frost point Hygrometer (PCFH)
    // Returns an object with parameters to be added to the sondes telemetry.
    //
    // References: 
    // Peltier Cooled Frost point Hygrometer (PCFH) Telemetry Interface PDF
    //
    // Sample data:      3c0101434a062c5cd4a5747b81486c93   (length = 32 characters)        
    //                   3c0103456076175ec5fc9df9b1         (length = 26 characters) 
    //                   3c0104a427104e203a9861a8ab6a65     (length = 30 characters) 
    //                   3c010000011b062221                 (length = 18 characters) 

    // Run some checks over the input
    int length=strlen(xdata);
    if (length > 32){
        // Invalid PCFH dataset
        return;
    }

    char tmp[40];
    memcpy(tmp,&xdata[0],2);
    tmp[2]='\0';
    //printf("-%s-\n",tmp);
    if (strcmp(tmp,"3C") != 0){
        // Not a PCFH (shouldn't get here)
        return;
    }

    // Instrument number is common to all XDATA types.
    memcpy(tmp,&xdata[2],2);
    tmp[2]='\0';
    output->instrument_number = (int)strtol(tmp, NULL, 16);

    // Packet ID
    char id[3];
    memcpy(id,&xdata[4],2);
    id[2]='\0';
    asprintf(&output->packetID,"%s",id);
    
    // Packet type
    if (strcmp(id,"00") == 0) { // Individual instrument identification (10 s)
        // Serial number
        memcpy(tmp,&xdata[6],4);
        tmp[4]='\0';
        output->serial_number = (int)strtol(tmp, NULL, 16);

        // Temperature PCB date
        memcpy(tmp,&xdata[10],2);
        tmp[2]='\0';
        asprintf(&output->temperature_pcb_date,"%s",getPCFHdate(tmp));

        // Main PCB date
        memcpy(tmp,&xdata[12],2);
        tmp[2]='\0';
        asprintf(&output->main_pcb_date,"%s",getPCFHdate(tmp));

        // Controller FW date
        memcpy(tmp,&xdata[14],2);
        tmp[2]='\0';
        asprintf(&output->controller_fw_date,"%s",getPCFHdate(tmp));

        // FPGA FW date
        memcpy(tmp,&xdata[16],2);
        tmp[2]='\0';
        asprintf(&output->fpga_fw_date,"%s",getPCFHdate(tmp));
    } else if ((strcmp(id,"01") == 0) || (strcmp(id,"02") == 0)) { // Regular one second data, sub-sensor 1/2
        // Frost point mirror temperature
        memcpy(tmp,&xdata[8],3);
        tmp[3]='\0';
        output->frost_point_mirror_temperature = ((int)strtol(tmp, NULL, 16)*0.05) - 125;
        
        // Peltier hot side temperature
        memcpy(tmp,&xdata[11],3);
        tmp[3]='\0';
        output->peltier_hot_side_temperature = ((int)strtol(tmp, NULL, 16)*0.05) - 125;

        // Air temperature
        memcpy(tmp,&xdata[14],3);
        tmp[3]='\0';
        output->air_temperature = ((int)strtol(tmp, NULL, 16)*0.05) - 125;
        
        // Anticipated frost point mirror temperature
        memcpy(tmp,&xdata[17],3);
        tmp[3]='\0';
        output->anticipated_frost_point_mirror_temperature = ((int)strtol(tmp, NULL, 16)*0.05) - 125;
 
        // Frost point mirror reflectance
        memcpy(tmp,&xdata[20],4);
        tmp[4]='\0';
        output->frost_point_mirror_reflectance = (int)strtol(tmp, NULL, 16)/32768.;

        // Reference surface reflectance
        memcpy(tmp,&xdata[24],4);
        tmp[4]='\0';
        output->reference_surface_reflectance = (int)strtol(tmp, NULL, 16)/32768.;

        // Reference surface heating current
        memcpy(tmp,&xdata[28],2);
        tmp[2]='\0';
        output->reference_surface_heating_current = (int)strtol(tmp, NULL, 16)/2.56;

        // Peltier current
        memcpy(tmp,&xdata[30],2);
        tmp[2]='\0';
        int pelt = (int)strtol(tmp, NULL, 16);
        if ((pelt  & 0x80) > 0) {
            pelt  = pelt  - 0x100;
        }
        output->peltier_current = pelt/64.;
    } else if (strcmp(id,"03") == 0) { // Regular five second data
        // Heat sink temperature 1
        memcpy(tmp,&xdata[8],3);
        tmp[3]='\0';
        output->heat_sink_temperature_01 = ((int)strtol(tmp, NULL, 16)*0.05) - 125;

        // Reference surface temperature 1
        memcpy(tmp,&xdata[11],3);
        tmp[3]='\0';
        output->reference_surface_temperature_01 = ((int)strtol(tmp, NULL, 16)*0.05) - 125;

        // Heat sink temperature 2
        memcpy(tmp,&xdata[14],3);
        tmp[3]='\0';
        output->heat_sink_temperature_02 = ((int)strtol(tmp, NULL, 16)*0.05) - 125;

        // Reference surface temperature 2
        memcpy(tmp,&xdata[17],3);
        tmp[3]='\0';
        output->reference_surface_temperature_02 = ((int)strtol(tmp, NULL, 16)*0.05) - 125;

        // Thermocouple reference temperature
        memcpy(tmp,&xdata[20],3);
        tmp[3]='\0';
        output->thermocouple_reference_temperature = ((int)strtol(tmp, NULL, 16)*0.05) - 125;

        // Reserved temperature
        memcpy(tmp,&xdata[23],3);
        tmp[3]='\0';
        output->reserved_temperature = ((int)strtol(tmp, NULL, 16)*0.05) - 125;
    } else if (strcmp(id,"04") == 0) { // Instrument status (10 s)
        // Clean frost point mirror reflectance 1
        memcpy(tmp,&xdata[8],4);
        tmp[4]='\0';
        output->clean_frost_point_mirror_reflectance_01 = (int)strtol(tmp, NULL, 16)*0.001;

        // Clean reference surface reflectance 1
        memcpy(tmp,&xdata[12],4);
        tmp[4]='\0';
        output->clean_reference_surface_reflectance_01 = (int)strtol(tmp, NULL, 16)*0.001;

        // Clean frost point mirror reflectance 2
        memcpy(tmp,&xdata[16],4);
        tmp[4]='\0';
        output->clean_frost_point_mirror_reflectance_02 = (int)strtol(tmp, NULL, 16)*0.001;

        // Clean reference surface reflectance 2
        memcpy(tmp,&xdata[20],4);
        tmp[4]='\0';
        output->clean_reference_surface_reflectance_02 = (int)strtol(tmp, NULL, 16)*0.001;

        // 6V Analog supply battery voltage
        memcpy(tmp,&xdata[24],2);
        tmp[2]='\0';
        output->v6_analog_supply_battery_voltage = ((int)strtol(tmp, NULL, 16)*0.02) + 2.5;

        // 4.5V Logic supply battery voltage
        memcpy(tmp,&xdata[26],2);
        tmp[2]='\0';
        output->v45_logic_supply_battery_voltage = ((int)strtol(tmp, NULL, 16)*0.02) + 2.5;

        // 4.5V Peltier and heater supply battery voltage
        memcpy(tmp,&xdata[28],2);
        tmp[2]='\0';
        output->v45_peltier_and_heater_supply_battery_voltage = ((int)strtol(tmp, NULL, 16)*0.02) + 2.5;
    }     
}

float calculateFLASHBWaterVapour(float S, float B, float P, float T) {
    //todo...
    float K1 = 0;
    float K2 = 0;
    float U = 0;

    float F = S - B + K2*(S-B);

    if (P < 36) {
        U = K1*F*0.956*(1+((0.00781*(T+273.16))/P));
    } else if (36 <= 36 < 300) {
        U = K1*F*(1 + 0.00031*P);
    }

    //return U;
    return S;
}

void parseFLASHB(output_flashb *output, char *xdata, float pressure, float temperature) {
    // Attempt to parse an XDATA string from a Fluorescent Lyman-Alpha Stratospheric Hygrometer for Balloon (FLASH-B)
    // Returns an object with parameters to be added to the sondes telemetry.
    //
    //
    // Sample data:      3D0204E20001407D00E4205DC24406B1012   (length = 35 characters)

    // Run some checks over the input
    int length=strlen(xdata);
    if (length > 35){
        // Invalid FLASH-B dataset
        return;
    }

    char tmp[40];
    memcpy(tmp,&xdata[0],2);
    tmp[2]='\0';
    //printf("-%s-\n",tmp);
    if (strcmp(tmp,"3D") != 0){
        // Not a FLASH-B (shouldn't get here)
        return;
    }

    // Instrument number is common to all XDATA types.
    memcpy(tmp,&xdata[2],2);
    tmp[2]='\0';
    output->instrument_number = (int)strtol(tmp, NULL, 16);

    memcpy(tmp,&xdata[5],4);
    tmp[4]='\0';
    int photomultiplier_counts = (int)strtol(tmp, NULL, 16);

    memcpy(tmp,&xdata[9],4);
    tmp[4]='\0';
    output->photomultiplier_background_counts = (int)strtol(tmp, NULL, 16);

    output->photomultiplier_counts = calculateFLASHBWaterVapour(photomultiplier_counts, output->photomultiplier_background_counts, pressure, temperature);

    memcpy(tmp,&xdata[13],4);
    tmp[4]='\0';
    output->photomultiplier_temperature = (-21.103*log(((int)strtol(tmp, NULL, 16)*0.0183)/(2.49856 - ((int)strtol(tmp, NULL, 16)*0.00061)))) + 97.106;

    memcpy(tmp,&xdata[17],4);
    tmp[4]='\0';
    output->battery_voltage = (int)strtol(tmp, NULL, 16)*0.005185;

    memcpy(tmp,&xdata[21],4);
    tmp[4]='\0';
    output->yuv_current = (int)strtol(tmp, NULL, 16)*0.0101688;

    memcpy(tmp,&xdata[25],4);
    tmp[4]='\0';
    output->pmt_voltage = (int)strtol(tmp, NULL, 16)*0.36966;

    memcpy(tmp,&xdata[25],4);
    tmp[4]='\0';
    output->firmware_version = (int)strtol(tmp, NULL, 16)*0.1;

    memcpy(tmp,&xdata[31],2);
    tmp[2]='\0';
    output->production_year = (int)strtol(tmp, NULL, 16);
    
    memcpy(tmp,&xdata[33],2);
    tmp[2]='\0';
    output->hardware_version = (int)strtol(tmp, NULL, 16);
}


char* parseType(char *data){
    int i=0;
    
    while(data[i]) {
      data[i]=toupper(data[i]);
      i++;
    } 

    char instrument[3];    
    memcpy(instrument,&data[0],2);
    instrument[2]='\0';
    
    if (strcmp(instrument,"01") == 0) { return "V7"; } 
    else if (strcmp(instrument,"05") == 0) { return "OIF411"; }
    else if (strcmp(instrument,"08") == 0) { return "CFH"; }  
    else if (strcmp(instrument,"10") == 0) { return "FPH"; } 
    else if (strcmp(instrument,"19") == 0) { return "COBALD"; } 
    else if (strcmp(instrument,"28") == 0) { return "SLW"; } 
    else if (strcmp(instrument,"38") == 0) { return "POPS"; } 
    else if (strcmp(instrument,"39") == 0) { return "OPC"; } 
    else if (strcmp(instrument,"3C") == 0) { return "PCFH"; } 
    else if (strcmp(instrument,"3D") == 0) { return "FLASH-B"; } 
    else if (strcmp(instrument,"3E") == 0) { return "TRAPS"; } 
    else if (strcmp(instrument,"3F") == 0) { return "SKYDEW"; } 
    else if (strcmp(instrument,"41") == 0) { return "CICANUM"; } 
    else if (strcmp(instrument,"45") == 0) { return "POPS"; } 
    else if (strcmp(instrument,"80") == 0) { return "Unknown"; }
    else { return "Unknown"; }
}  

int prn_jsn(char *xdata,float press, float temperature){
    char *data, *str, *tofree;    
    char* instrument;
    int i=0;
    char *linst="";
    
    if ((press<0) && (press!=-1)) { press=alt2press(-press); }  //altitude given -> convert to pressure
    
    tofree = str = strdup(xdata);  // We own str's memory now.
    while ((data = strsep(&str, "#"))) {
      instrument=parseType(data);
      if (strcmp(instrument,linst)!=0) {                  
          if (i>0) {fprintf(stdout, ", ");}
          i++;
          fprintf(stdout, "\"aux_inst_%d\": \"%s\"",i,instrument);
          asprintf(&linst,"%s",instrument);
      }
      if(strcmp(instrument,"OIF411") == 0){          
          output_oif411 output={0};
          parseOIF411(&output,data,press);                                        
          
          if(strcmp(output.data_type,"ID Data") == 0){   
              fprintf(stdout, ", \"O3_serial\": \"%s\"",output.serial);
              fprintf(stdout, ", \"O3_diagnostics\": \"%s\"",output.diagnostics);
              fprintf(stdout, ", \"O3_version\": %d",output.version);
          } 
          else {
              fprintf(stdout, ", \"O3_pump_temp\": %.2f",output.ozone_pump_temp);
              fprintf(stdout, ", \"O3_current\": %.3f",output.ozone_current_uA);
              fprintf(stdout, ", \"O3_battery_volt\": %.1f",output.ozone_battery_v);
              fprintf(stdout, ", \"O3_pump_current\": %.1f",output.ozone_pump_curr_mA);
              fprintf(stdout, ", \"O3_external_volt\": %.2f",output.ext_voltage);
              fprintf(stdout, ", \"O3_pressure\": %.3f",output.O3_partial_pressure);
          }
      }
    }
    free(tofree);
    return 0;
}

int prn_aux(char *xdata,float press, float temperature){
    char *data, *str, *tofree;    
    char* instrument;
    
    if ((press<0) && (press!=-1)) { press=alt2press(-press); }  //altitude given -> convert to pressure
    
    tofree = str = strdup(xdata);  // We own str's memory now.
    while ((data = strsep(&str, "#"))) {
        instrument=parseType(data);
        if(strcmp(instrument,"OIF411") == 0){          
            output_oif411 output={0};
            parseOIF411(&output,data,press);
             
            if(strcmp(output.data_type,"Measurement Data") == 0){ 
                fprintf(stdout," O3_P=%.3fmPa ",output.O3_partial_pressure);
            }
        }
    }
    free(tofree); 
    
    return 0;
}


void prn_full(char *xdata,float press, float temperature){
    char* instrument; 
    char *data, *str, *tofree;    

    tofree = str = strdup(xdata);  // We own str's memory now.
    while ((data = strsep(&str, "#"))) {
        instrument=parseType(data);
        fprintf(stdout," %s:",instrument);
        
        if(strcmp(instrument,"OIF411") == 0){          
            output_oif411 output={0};
            parseOIF411(&output,data,press);
            
            if(strcmp(output.data_type,"ID Data") == 0){   
                fprintf(stdout," serial=%s",output.serial);
                fprintf(stdout," diagnostics=%s",output.diagnostics);
                fprintf(stdout," version=%d",output.version);
            } 
            else { 
                fprintf(stdout," pump_temp=%.2fC",output.ozone_pump_temp);
                fprintf(stdout," current=%.4fuA",output.ozone_current_uA);
                fprintf(stdout," battery_volt=%.1fV",output.ozone_battery_v);
                fprintf(stdout," pump_current=%.1fmA",output.ozone_pump_curr_mA);
                fprintf(stdout," external_volt=%.1fV",output.ext_voltage);
                fprintf(stdout," pressure=%.3fmPa",output.O3_partial_pressure);
            }
        }
        else if(strcmp(instrument,"CFH") == 0){ 
            output_cfh output={0};
            parseCFH(&output,data); 
        } 
        else if(strcmp(instrument,"COBALD") == 0){ 
            output_cobald output={0};
            parseCOBALD(&output,data); 
            fprintf(stdout," sonde=%d",output.sonde_number);
            fprintf(stdout," temp=%.2fC",output.internal_temperature);
            fprintf(stdout," blue_scatt=%d",output.blue_backscatter);
            fprintf(stdout," red_scatt=%d",output.red_backscatter);
            fprintf(stdout," blue_monitor=%d",output.blue_monitor);
            fprintf(stdout," red_monitor=%d",output.red_monitor);
        }
        else if(strcmp(instrument,"PCFH") == 0){ 
            output_pcfh output={0};
            parsePCFH(&output,data); 
            if (strcmp(output.packetID,"00") == 0) {
                fprintf(stdout," serial=%d",output.serial_number);
                fprintf(stdout," temp_pcb=%s",output.temperature_pcb_date);
                fprintf(stdout," main_pcb=%s",output.main_pcb_date);
                fprintf(stdout," controller=%s",output.controller_fw_date);
                fprintf(stdout," fpga=%s",output.fpga_fw_date);
            }
            else if ((strcmp(output.packetID,"01") == 0) || (strcmp(output.packetID,"02") == 0)) {
                fprintf(stdout," frost_point_mirror_temperature_%s=%.2Cf",output.packetID,output.frost_point_mirror_temperature);
                fprintf(stdout," peltier_hot_side_temperature_%s=%.2Cf",output.packetID,output.peltier_hot_side_temperature);
                fprintf(stdout," air_temperature_%s=%.2Cf",output.packetID,output.air_temperature);
                fprintf(stdout," anticipated_frost_point_mirror_temperature_%s=%.2Cf",output.packetID,output.anticipated_frost_point_mirror_temperature);
                fprintf(stdout," frost_point_mirror_reflectance_%s=%.3f",output.packetID,output.frost_point_mirror_reflectance);
                fprintf(stdout," reference_surface_reflectance_%s=%.3f",output.packetID,output.reference_surface_reflectance);
                fprintf(stdout," reference_surface_heating_current_%s=%.2uAf",output.packetID,output.reference_surface_heating_current);
                fprintf(stdout," peltier_current_%s=%.3uAf",output.packetID,output.peltier_current);
            }
            else if (strcmp(output.packetID,"03") == 0) {
                fprintf(stdout," heat_sink_temperature_01=%.2Cf",output.heat_sink_temperature_01);
                fprintf(stdout," reference_surface_temperature_01=%.2Cf",output.reference_surface_temperature_01);
                fprintf(stdout," heat_sink_temperature_02=%.2Cf",output.heat_sink_temperature_02);
                fprintf(stdout," reference_surface_temperature_02=%.2Cf",output.reference_surface_temperature_02);
                fprintf(stdout," thermocouple_reference_temperature=%.2Cf",output.thermocouple_reference_temperature);
                fprintf(stdout," reserved_temperature=%.2Cf",output.reserved_temperature);
            }
            else if (strcmp(output.packetID,"04") == 0) {
                fprintf(stdout," clean_frost_point_mirror_reflectance_01=%.3f",output.clean_frost_point_mirror_reflectance_01);
                fprintf(stdout," clean_reference_surface_reflectance_01=%.3f",output.clean_reference_surface_reflectance_01);
                fprintf(stdout," clean_frost_point_mirror_reflectance_02=%.3f",output.clean_frost_point_mirror_reflectance_02);
                fprintf(stdout," clean_reference_surface_reflectance_02=%.3f",output.clean_reference_surface_reflectance_02);
                fprintf(stdout," 6v_analog_supply_battery_voltage=%.2Vf",output.v6_analog_supply_battery_voltage);
                fprintf(stdout," 4.5v_logic_supply_battery_voltage=%.2Vf",output.v45_logic_supply_battery_voltage);
                fprintf(stdout," 4.5v_peltier_and_heater_supply_battery_voltage=%.2Vf",output.v45_peltier_and_heater_supply_battery_voltage);
            }
        } 
        else if(strcmp(instrument,"SKYDEW") == 0){ 
            output_skydew output={0};
            parseSKYDEW(&output,data); 
            fprintf(stdout," mirror_temperature=%d",output.mirror_temperature);
            fprintf(stdout," scattered_light=%.4f",output.scattered_light);
            fprintf(stdout," peltier_current=%.4fA",output.peltier_current);
            fprintf(stdout," heatsink_temperature=%.2fC",output.heatsink_temperature);
            fprintf(stdout," board_temperature=%.2fC",output.circuit_board_temperature);
            fprintf(stdout," battery=%d",output.battery);
            fprintf(stdout," pid=%d",output.pid);
        
            switch (output.parameterType){
                case 0:
                    fprintf(stdout," serial_number=%d",output.serial_number);
                    break;
                case 1:
                    fprintf(stdout," coefficient_b=%d",output.coefficient_b);
                    break;
                case 2:
                    fprintf(stdout," coefficient_c=%d",output.coefficient_c);
                    break;
                case 3:
                    fprintf(stdout," coefficient_d=%d",output.coefficient_d);
                    break;
                case 4:
                    fprintf(stdout," coefficient_e=%d",output.coefficient_e);
                    break;
                case 5:
                    fprintf(stdout," firmware_version=%d",output.firmware_version);
                    break;
            }
        } 
        else if(strcmp(instrument,"FLASH-B") == 0){ 
            output_flashb output={0};
            parseFLASHB(&output,data,press,temperature); 
            fprintf(stdout," background_counts=%d",output.photomultiplier_background_counts);
            fprintf(stdout," counts=%d",output.photomultiplier_counts);
            fprintf(stdout," temperature=%.2fC",output.photomultiplier_temperature);
            fprintf(stdout," battery_voltage=%.2fV",output.battery_voltage);
            fprintf(stdout," yuv_current=%.2fuA",output.yuv_current);
            fprintf(stdout," pmt_voltage=%.1fmV",output.pmt_voltage);
            fprintf(stdout," firmware_version=%.1f",output.firmware_version);
            fprintf(stdout," production_year=%d",output.production_year);
            fprintf(stdout," hardware_version=%d",output.hardware_version);
        }     
    } 
    free(tofree);
}
