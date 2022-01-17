/* Rewritten from SondeHub XDATA Parser Library (Authors: Mark Jessop & Luke Prior)
 *
 * Pavol Gajdos
 */

// Pump Efficiency Correction Parameters for ECC-6A Ozone Sensor, with 3.0cm^3 volume.
// We are using these as a nominal correction value for pump efficiency vs pressure
// 

#include "xdata.h"
 
void main(int argc, char *argv[]){
  char* xdata;
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
  free(tofree); 
}

