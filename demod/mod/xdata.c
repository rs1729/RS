/* Rewritten from SondeHub XDATA Parser Library (Authors: Mark Jessop & Luke Prior)
 *
 * Pavol Gajdos
 */

// Pump Efficiency Correction Parameters for ECC-6A Ozone Sensor, with 3.0cm^3 volume.
// We are using these as a nominal correction value for pump efficiency vs pressure
// 

#include "xdata.h"

void prn_test(char *xdata,float press, float temperature){
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
                printf("serial: %s\n",output.serial);
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
            printf("temp: %f\n",output.internal_temperature);
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
            printf("mirror_temperature: %d\n",output.mirror_temperature);
            printf("scattered_light: %g\n",output.scattered_light);
            printf("peltier_current: %g\n",output.peltier_current);
            printf("heatsink_temperature: %g\n",output.heatsink_temperature);
            printf("board_temperature: %g\n",output.circuit_board_temperature);
            printf("battery: %d\n",output.battery);
            printf("pid: %d\n",output.pid);
        
            switch (output.parameterType){
                case 0: 
                    printf("serial_number: %d\n",output.serial_number);
                    break;
                case 1: 
                    printf("coefficient_b: %d\n",output.coefficient_b);
                    break;
                case 2: 
                    printf("coefficient_c: %d\n",output.coefficient_c);
                    break;
                case 3: 
                    printf("coefficient_d: %d\n",output.coefficient_d);
                    break;
                case 4: 
                    printf("coefficient_e: %d\n",output.coefficient_e);
                    break;
                case 5: 
                    printf("firmware_version: %d\n",output.firmware_version);
                    break;
            }
        } 
        else if(strcmp(instrument,"FLASH-B") == 0){ 
            output_flashb output={0};
            parseFLASHB(&output,data,press,temperature); 
            printf("number: %d\n",output.instrument_number);
            printf("background_counts: %d\n",output.photomultiplier_background_counts);
            printf("counts: %d\n",output.photomultiplier_counts);
            printf("temperature: %g\n",output.photomultiplier_temperature);
            printf("battery_voltage: %g\n",output.battery_voltage);
            printf("yuv_current: %g\n",output.yuv_current);
            printf("pmt_voltage: %g\n",output.pmt_voltage);
            printf("firmware_version: %g\n",output.firmware_version);
            printf("production_year: %d\n",output.production_year);
            printf("hardware_version: %d\n",output.hardware_version);
        } 
        printf("-----------------\n");    
    } 
    free(tofree);
} 

 
void main(int argc, char *argv[]){ 
  char *fpname = NULL; 
  float out_type=0;  //0-basic, 1-full (-v), 2-json, 3-test
  int fileloaded = 0;
  char *input;
    
  float press=-1;
  float temperature=0;
  
  fpname = argv[0];
  ++argv;
  
  if (argc==1) {
    fprintf(stderr, "%s [options] xdata (pressure/-altitude) temperature\n", fpname);
    fprintf(stderr, "    or    \n");
    fprintf(stderr, "%s [options] --file filename // with xdata (pressure/-altitude) temperature\n\n", fpname);
    fprintf(stderr, "  options:\n");
    fprintf(stderr, "       -v, --verbose  (info)\n");
    fprintf(stderr, "or     --json\n");
    fprintf(stderr, "or     --test (output with desc.)\n");
    return;
  }
  
  while (*argv) {
    if ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
        fprintf(stderr, "%s [options] xdata (pressure/-altitude) temperature\n", fpname);
        fprintf(stderr, "    or    \n");
        fprintf(stderr, "%s [options] --file filename // with xdata (pressure/-altitude) temperature\n\n", fpname);
        fprintf(stderr, "  options:\n");
        fprintf(stderr, "       -v, --verbose  (info)\n");
        fprintf(stderr, "or     --json\n");
        fprintf(stderr, "or     --test (output with desc.)\n");
        return;
    }
    else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {out_type = 1; }
    else if (strcmp(*argv, "--json") == 0) {out_type = 2; }
    else if (strcmp(*argv, "--test") == 0) {out_type = 3; }
    else if (strcmp(*argv, "--file") == 0) {fileloaded = 1; }
    else{
        asprintf(&input,"%s",*argv);
        ++argv;
        if ((!fileloaded) && (*argv)) { 
            press=(float)strtof(*argv, NULL); 
            ++argv;
            if (*argv) {temperature=(float)strtof(*argv, NULL); }  
        }
        break;
    }     
    ++argv;
  }
  
  if (fileloaded){
      FILE *f;
      f = fopen(input, "rb");
      char buf[500];
      char xdata[500]; 
      char* x=fgets(buf, sizeof buf, f);
      int n=sscanf(buf,"%s %g %g", xdata,&press, &temperature);   //number of cols
      fclose(f);
      
      int first=1;  //first frame or not 1/0
      if (out_type==2) { fprintf(stdout, "[\n"); }
      
      f = fopen(input, "rb");
      int r;
      if (n==3) {r=fscanf(f,"%s %g %g\n", xdata, &press, &temperature);}
      else if (n== 2) {r=fscanf(f,"%s %g\n", xdata, &press);}
      else if (n== 1) {r=fscanf(f,"%s\n", xdata);}
      while (r!= EOF) {
          if (n== 2) {
            temperature=0;
          }
          else if (n== 1) {
            press=-1;
            temperature=0;
          }
          else { break; }
          
          if (out_type==2) { if (!first) fprintf(stdout, ",\n"); }
          first=0;
          
          if (out_type==0) { prn_aux(xdata,press,temperature); fprintf(stdout, "\n"); }
          else if (out_type==1) { prn_full(xdata,press,temperature); fprintf(stdout, "\n");}
          else if (out_type==2) { fprintf(stdout, "{"); prn_jsn(xdata,press,temperature); fprintf(stdout, "}"); }
          else if (out_type==3) { prn_test(xdata,press,temperature); fprintf(stdout, "\n"); }   
          
          if (n== 3) {r=fscanf(f,"%s %g %g\n", xdata, &press, &temperature);}
          else if (n== 2) {r=fscanf(f,"%s %g\n", xdata, &press);}
          else if (n== 1) {r=fscanf(f,"%s\n", xdata);}
      }
      fclose(f);
      if (out_type==2) { fprintf(stdout, "\n]\n"); }
  }
  else {  
    char* xdata;
    xdata=input;
    
    if (out_type==0) { prn_aux(xdata,press,temperature); fprintf(stdout, "\n"); }
    else if (out_type==1) { prn_full(xdata,press,temperature); fprintf(stdout, "\n");}
    else if (out_type==2) { fprintf(stdout, "{"); prn_jsn(xdata,press,temperature); fprintf(stdout, "}"); }
    else if (out_type==3) { prn_test(xdata,press,temperature); fprintf(stdout, "\n"); } 
     
  }
}

