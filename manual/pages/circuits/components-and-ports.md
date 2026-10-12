# Components and ports

A circuit is a set of components joined by links. Each component appears as a node in the **Node Editor**, and its ports are drawn as pins. A signal flows from an output pin to an input pin.

## Add a component

Right-click an empty part of the Node Editor canvas, then choose one of the **Add** entries:

- Add Amplifier
- Add Attenuator
- Add Splitter
- Add Ideal Filter
- Add Mixer
- Add Equalizer
- Add Combiner
- Add SPDT Switch
- Add SPDT Switch (2:1)
- Add RF ADC
- Add Generator
- Add Coax Cable
- Add PFB Channelizer

You can also click a part in the **Component Library** panel (**View > Component Library**) to insert it.

## Connect ports

Drag from an output pin to an input pin, then release the mouse button. The new link carries the signal from the output to the input.

## Remove a link or a component

To remove a link, right-click it and choose **Remove**, or select it and press Delete. To remove a component, right-click it and choose **Remove**, or select it and press Delete.

If you press Delete with both links and components selected, the links are removed first.

## Set parameters

Select a component to show its parameters in the **Properties** panel (**View > Properties**). Changes take effect immediately.

Edits are clamped into a parameter's limits. A value loaded from a file that lies outside its limits is shown in an amber field, with a tooltip that names the allowed range. The value stays as loaded until you edit it.
