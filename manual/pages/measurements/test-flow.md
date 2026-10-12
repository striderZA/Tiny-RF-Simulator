# Test Flow

A test flow runs the circuit through a set of conditions and records measurements for each combination of condition values. Open it from **View > Test Flow**.

A flow has three parts:

- **Conditions** set a parameter of a component to each value in a list.
- **Measurements** read a metric from a component, and from one of its ports when a port is given.
- **Results** hold one row for each combination of condition values. A flow with no conditions makes a single pass.

## Build a flow

Choose **New from Circuit** to start a flow for the circuit that is open. Then add conditions and measurements:

- **Add condition** adds a condition. Choose the component, enter the parameter path, and enter the values. **Update condition** replaces the condition you are editing.
- **Add measurement** adds a measurement. Choose the component, and a port if the measurement needs one, then choose a metric from the **Metric** list.

The metrics are:

| Metric | Unit | Reads |
| --- | --- | --- |
| `power_dBm` | dBm | total power |
| `peak_power_dBm` | dBm | peak power |
| `peak_freq_Hz` | Hz | frequency of the peak |
| `noise_floor_dBm_per_Hz` | dBm/Hz | noise floor |

## Save, open, and reload

**Save Flow...** saves the flow to a JSON file. **Open Flow...** opens a flow file. **Reload** reads the open flow file from disk again.

## Run and export

**Run** runs the flow on the circuit. The run produces one row for each combination of condition values. A run with more than 10000 rows is refused, with the message "Run refused: the sweep exceeds the panel's 10000-row limit."

Results report the value a component applied, after the component adjusts the requested value to one it supports. For example, a requested ADC decimation of 3 is reported as 2 when the component applies 2.

**Export...** saves the results of a successful run to a JSON file. It is available only after a run succeeds.

## The flow file

A flow file is JSON with these keys:

- `version` is required and must be `1`.
- `name` is optional text.
- `conditions` is an optional list. Each item has `component` (a whole number), `path` (text), and `values` (a list of at least one number). A component and path pair may appear only once.
- `measure` is required and must not be empty. Each item has `component` (a whole number), an optional `port` (a whole number of zero or more), and `metric` (one of the names in the table). A metric may appear only once for the same component and port.

The `component` numbers identify components in the circuit the flow was saved from. A flow made for one circuit can refer to different components in another, so build a flow from the circuit that is open, with **New from Circuit**.
