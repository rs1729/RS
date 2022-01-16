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
        output->serial = tmp;

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

    // Other fields may include
    // Serial number
    // Mirror temperature (-120 - 30)
    // Mixing ratio V (ppmV)
    // PT100 (Ohm 60 - 120)
    // SCA light
    // SCA base
    // PLT current
    // HS temp
    // CB temp
    // PID
    // Battery
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

void main(int argc, char *argv[]){
  char* xdata;
  char** data_split;  
  float press=-1;
  
  xdata=argv[1];
  
  printf("input_data: %s\n\n",xdata);

  char* instrument; 
  char *data, *str, *tofree;

  tofree = str = strdup(xdata);  // We own str's memory now.
  while ((data = strsep(&str, "#"))) {
      instrument=parseType(data);
      printf("data: %s\n",data);
      printf("\n%s\n\n",instrument);
      
      if(strcmp(instrument,"OIF411") == 0){          
        output_oif411 output={0};
        parseOIF411(&output,data,press);
         
        printf("diagnostics: %s\n",output.data_type);
        if(strcmp(output.data_type,"ID Data") == 0){   
            printf("serial: %d\n",output.serial);
            printf("diagnostics: %s\n",output.diagnostics);
            printf("version: %d\n",output.version);
        } 
        else { 
            printf("number: %d\n",output.instrument_number);
            printf("pump_temp: %g\n",output.ozone_pump_temp);
            printf("current: %g\n",output.ozone_current_uA);
            printf("battery: %g\n",output.ozone_battery_v);
            printf("pump_current: %g\n",output.ozone_pump_curr_mA);
            printf("voltage: %g\n",output.ext_voltage);
            printf("O3_pressure: %g\n",output.O3_partial_pressure);
        }
      }
      else if(strcmp(instrument,"CFH") == 0){ 
        output_cfh output={0};
        parseCFH(&output,data); 
        printf("number: %d\n",output.instrument_number);
      } 
      else if(strcmp(instrument,"COBALD") == 0){ 
        output_cobald output={0};
        parseCOBALD(&output,data); 
        printf("number: %d\n",output.instrument_number);
        printf("sonde: %d\n",output.sonde_number);
        printf("temp: %d\n",output.internal_temperature);
        printf("blue_scatt: %d\n",output.blue_backscatter);
        printf("red_scatt: %d\n",output.red_backscatter);
        printf("blue_monitor: %d\n",output.blue_monitor);
        printf("red_monitor: %d\n",output.red_monitor);
      }
      else if(strcmp(instrument,"PCFH") == 0){ 
        output_pcfh output={0};
        parsePCFH(&output,data); 
        printf("number: %d\n",output.instrument_number);
        printf("packetID: %s\n",output.packetID);
        if (strcmp(output.packetID,"00") == 0) {
            printf("serial: %d\n",output.serial_number);
            printf("temp_pcb: %s\n",output.temperature_pcb_date);
            printf("main_pcb: %s\n",output.main_pcb_date);
            printf("controller: %s\n",output.controller_fw_date);
            printf("fpga: %s\n",output.fpga_fw_date);
        }
        else if ((strcmp(output.packetID,"01") == 0) || (strcmp(output.packetID,"02") == 0)) {
            printf("frost_point_mirror_temperature: %g\n",output.frost_point_mirror_temperature);
            printf("peltier_hot_side_temperature: %g\n",output.peltier_hot_side_temperature);
            printf("air_temperature: %g\n",output.air_temperature);
            printf("anticipated_frost_point_mirror_temperature: %g\n",output.anticipated_frost_point_mirror_temperature);
            printf("frost_point_mirror_reflectance: %g\n",output.frost_point_mirror_reflectance);
            printf("reference_surface_reflectance: %g\n",output.reference_surface_reflectance);
            printf("reference_surface_heating_current: %g\n",output.reference_surface_heating_current);
            printf("peltier_current: %g\n",output.peltier_current);
        }
        else if (strcmp(output.packetID,"03") == 0) {
            printf("heat_sink_temperature_01: %g\n",output.heat_sink_temperature_01);
            printf("reference_surface_temperature_01: %g\n",output.reference_surface_temperature_01);
            printf("heat_sink_temperature_02: %g\n",output.heat_sink_temperature_02);
            printf("reference_surface_temperature_02: %g\n",output.reference_surface_temperature_02);
            printf("thermocouple_reference_temperature: %g\n",output.thermocouple_reference_temperature);
            printf("reserved_temperature: %g\n",output.reserved_temperature);
        }
        else if (strcmp(output.packetID,"04") == 0) {
            printf("clean_frost_point_mirror_reflectance_01: %g\n",output.clean_frost_point_mirror_reflectance_01);
            printf("clean_reference_surface_reflectance_01: %g\n",output.clean_reference_surface_reflectance_01);
            printf("clean_frost_point_mirror_reflectance_02: %g\n",output.clean_frost_point_mirror_reflectance_02);
            printf("clean_reference_surface_reflectance_02: %g\n",output.clean_reference_surface_reflectance_02);
            printf("6v_analog_supply_battery_voltage: %g\n",output.v6_analog_supply_battery_voltage);
            printf("4.5v_logic_supply_battery_voltage: %g\n",output.v45_logic_supply_battery_voltage);
            printf("4.5v_peltier_and_heater_supply_battery_voltage: %g\n",output.v45_peltier_and_heater_supply_battery_voltage);
        }
      } 
      else if(strcmp(instrument,"SKYDEW") == 0){ 
        output_skydew output={0};
        parseSKYDEW(&output,data); 
        printf("number: %d\n",output.instrument_number);
      } 
      printf("-----------------\n");    
  }  
}
