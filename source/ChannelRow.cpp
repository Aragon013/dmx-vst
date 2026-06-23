#include "ChannelRow.h"

ChannelRow::ChannelRow (int index, const ChannelDef& def)
{
    source = def;

    idxLabel.setJustificationType (juce::Justification::centred);
    idxLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffb020));
    idxLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (idxLabel);

    nameEditor.setText (def.label, juce::dontSendNotification);
    nameEditor.setTextToShowWhenEmpty ("nombre del canal", juce::Colours::grey);
    nameEditor.onTextChange = [this] { if (onChange) onChange(); };
    addAndMakeVisible (nameEditor);

    int id = 1;
    for (const auto& name : allChannelTypeNames())
        typeCombo.addItem (name, id++);
    typeCombo.setText (channelTypeToString (def.type), juce::dontSendNotification);
    typeCombo.setSelectedId ((int) allChannelTypeNames().indexOf (channelTypeToString (def.type)) + 1,
                             juce::dontSendNotification);
    typeCombo.onChange = [this] { if (onChange) onChange(); };
    addAndMakeVisible (typeCombo);

    defaultSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    defaultSlider.setRange (0.0, 255.0, 1.0);
    defaultSlider.setValue (def.defaultValue, juce::dontSendNotification);
    defaultSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    defaultSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xffffb020));
    defaultSlider.onValueChange = [this] { if (onChange) onChange(); };
    addAndMakeVisible (defaultSlider);

    deleteButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff5a2020));
    deleteButton.onClick = [this] { if (onDelete) onDelete(); };
    addAndMakeVisible (deleteButton);

    setIndex (index);
}

void ChannelRow::setIndex (int index)
{
    idxLabel.setText (juce::String (index + 1), juce::dontSendNotification);
}

ChannelDef ChannelRow::getChannelDef() const
{
    ChannelDef c = source;   // conserva colour + keyframes existentes
    c.type         = channelTypeFromString (typeCombo.getText());
    c.label        = nameEditor.getText();
    c.defaultValue = (int) defaultSlider.getValue();

    // Si el tipo cambio respecto al original, recolorea con el color del nuevo tipo.
    if (c.type != source.type)
        c.colour = defaultColourForChannelType (c.type);

    return c;
}

void ChannelRow::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff1a1d23));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
}

void ChannelRow::resized()
{
    auto r = getLocalBounds().reduced (4, 3);
    idxLabel.setBounds      (r.removeFromLeft (30));
    r.removeFromLeft (4);
    deleteButton.setBounds  (r.removeFromRight (30));
    r.removeFromRight (6);
    typeCombo.setBounds     (r.removeFromLeft (120));
    r.removeFromLeft (6);
    nameEditor.setBounds    (r.removeFromLeft (juce::jmax (90, r.getWidth() / 3)));
    r.removeFromLeft (6);
    defaultSlider.setBounds (r);
}
