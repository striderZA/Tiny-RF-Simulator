# PFB Filter Calculator

The PFB Filter Calculator shows how a polyphase filter bank (PFB) channelizer performs for a set of parameters, and whether it meets a rejection target. Open it from **View > Filter Calculator**. It needs a PFB Channelizer in the circuit. Add one from the Node Editor canvas menu, **Add PFB Channelizer**.

## Choose the target

**Target PFB** selects the channelizer the calculator works on. **Auto (graph selection)** uses the PFB selected in the Node Editor when exactly one node is selected and that node is a PFB Channelizer. Otherwise it uses the first PFB Channelizer in the circuit. To use a particular channelizer, choose it by name.

## Set the parameters

- **Channels M** sets the number of channels, from 2 to 2048.
- **Taps/branch K** sets the filter taps in each branch, from 1 to 64.
- **Kaiser beta** sets the shape of the Kaiser window, from 0 to 20.
- **Target rejection (dB)** sets the rejection the filter should reach, from 20 to 140 dB.

## Apply to the PFB

**Apply to PFB** writes **Channels M**, **Taps/branch K**, and **Kaiser beta** to the target channelizer. The button appears when a target channelizer is selected.

## Read the results

- **Adjacent rejection: <x> dB (target <y> dB)** compares the rejection at the adjacent channel with the target.
- **-3 dB half-width** is shown in channels.
- **Band-edge loss** is shown in dB.
- **Far floor (x in 1.0..1.5)** reports the response between 1.0 and 1.5 channel widths.
- **Taps N = M*K** is the total number of filter taps.
- **Flat-noise tilt** is shown in dB.

When the adjacent rejection meets the target, the calculator shows **Rejection target met.** Otherwise it shows a hint in the left column.
