#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FixtureModel.h"

/**
    Fila editable de un canal dentro del editor de equipos: indice, nombre,
    tipo (preset), valor por defecto (0-255) y boton de borrado.
*/
class ChannelRow : public juce::Component
{
public:
    ChannelRow (int index, const ChannelDef& def);

    void setIndex (int index);
    ChannelDef getChannelDef() const;

    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onDelete;
    std::function<void()> onChange;

private:
    ChannelDef      source;   // def original (preserva colour + keyframes al editar)
    juce::Label     idxLabel;
    juce::TextEditor nameEditor;
    juce::ComboBox  typeCombo;
    juce::Slider    defaultSlider;
    juce::TextButton deleteButton { "X" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRow)
};
