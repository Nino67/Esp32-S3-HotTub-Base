
#include "esp_err.h"
#include "esp_log.h"
#include "hot_tub_globals.h"
#include "hot_tub_struct_io.h"    
#include "hot_tub_sim.h"
#include "hot_tub_ds18b20.h"

const char *TAG = "hot_tub_sim";

#define SIM_TRIANGLE_MIN_TEMP 30.0f
#define SIM_TRIANGLE_MAX_TEMP 42.0f
#define SIM_TRIANGLE_STEP 0.1f


void hot_tub_controller_set_simulation_mode(sim_mode_t mode);
sim_mode_t hot_tub_controller_get_simulation_mode(void);
void hot_tub_controller_set_simulation_mode(sim_mode_t mode);




// typedef enum {
//     SIM_NONE,
//     SIM_MANUAL,
//     SIM_PHYSICS,
//     SIM_TRIANGLE
// } sim_mode_t;

// typedef struct {
//     sim_mode_t mode;
//     float manual_temp;
//     float simulated_temp;
//     float ambient_temp;
//     int triangle_dir; // 1 for heating/up, -1 for cooling/down
// } temp_sim_t;

// static temp_sim_t g_sim = {
//     .mode = SIM_NONE,
//     .manual_temp = 38.0f,
//     .simulated_temp = 20.0f,
//     .ambient_temp = 15.0f,
//     .triangle_dir = 1
// };

// // Call this periodically in your sensor sampling task (e.g., every 1 second)
// float read_water_temperature(bool heater_is_on) {
//     // switch (g_sim.mode) {
//     //     case SIM_MANUAL:
//     //         return g_sim.manual_temp;

//     //     case SIM_TRIANGLE: {
//     //         // Sweep between 30°C and 42°C
//     //         g_sim.simulated_temp += g_sim.triangle_dir * 0.1f;
//     //         if (g_sim.simulated_temp >= SIM_TRIANGLE_MAX_TEMP) g_sim.triangle_dir = -1;
//     //         if (g_sim.simulated_temp <= SIM_TRIANGLE_MIN_TEMP) g_sim.triangle_dir = 1;
//     //         return g_sim.simulated_temp;
//     //     }

//     //     case SIM_PHYSICS: {
//     //         // First-order thermal transfer model
//     //         const float heat_rate = 0.0015f;   // ~5.4°C / hour heating rise
//     //         const float loss_coeff = 0.00005f;  // Thermal leak to ambient
            
//     //         float heating = heater_is_on ? heat_rate : 0.0f;
//     //         float cooling = (g_sim.simulated_temp - g_sim.ambient_temp) * loss_coeff;
            
//     //         g_sim.simulated_temp += (heating - cooling);
//     //         return g_sim.simulated_temp;
//     //     }

//     //     case SIM_NONE:
//     //     default:
//     //         // Read hardware sensor (DS18B20 / ADC / MAX31865)
//     //         return read_hardware_sensor();
//     // }
// }



float get_simulated_temperature(void) {

    sim_mode_t mode = hot_tub_controller_get_simulation_mode();
    
    static int direction = 1; // 1 for heating, -1 for cooling    
    float temp = 0.0f;
    hot_tub_ds18b20_read_temperature(&temp);


    switch (mode) 
    {
        case SIM_MANUAL:
        {
            float manual_temp = 0.0f;
            return manual_temp; // Return the manually set temperature
        }

        case SIM_TRIANGLE: 
        {
            // Sweep between SIM_TRIANGLE_MIN_TEMP and SIM_TRIANGLE_MAX_TEMP
            temp += direction * 0.1f;
            if (temp >= SIM_TRIANGLE_MAX_TEMP) direction = -1;
            if (temp <= SIM_TRIANGLE_MIN_TEMP) direction = 1;
            return temp;
        }  

        case SIM_PHYSICS:
            return temp;
        case SIM_NONE:
            return temp;
        default:
            // If simulation mode is off, read the actual hardware sensor
            return temp;
    }

  
    return temp;
}