# hot_tub_controller Component

Back to project guide: [../../README.md](../../README.md)

## What This Component Does

`hot_tub_controller` contains the core control logic for water temperature, pump behavior, and heater decisions.

## Control Loop Overview

```mermaid
flowchart LR
  Sensors[Temp/Humidity sensors] --> Filter[Low-pass filter]
  Filter --> Logic[Hysteresis + safety logic]
  Logic --> Outputs[Pump/Heater outputs]
  Outputs --> Publish[Publish status JSON]
```

## Beginner Explanation

The control loop repeats forever:
1. read sensors
2. smooth noisy readings
3. compare against setpoint
4. toggle outputs with hysteresis rules

## Key Data Fields

- `waterTemp`
- `filteredWaterTemp`
- `airTemp`
- `setpointTemp`
- pump/heater state

## Example Behavior Plot

```mermaid
xychart-beta
  title "Example Temperature vs Setpoint"
  x-axis [t0, t1, t2, t3, t4]
  y-axis "C" 20 --> 45
  line [34, 34.5, 35.2, 35.8, 36.0]
```

## Troubleshooting

- Oscillation usually means hysteresis thresholds are too tight.
- Flatline temperatures suggest sensor path or conversion issue.
