#include "FixtureEditor.h"

//==============================================================================
// Contenedor interno: apila las filas de canal y se redimensiona para el viewport.
class FixtureEditor::ChannelList : public juce::Component
{
public:
    std::vector<std::unique_ptr<ChannelRow>> rows;

    static constexpr int rowHeight = 38;

    void layout (int width)
    {
        setSize (width, juce::jmax (1, (int) rows.size() * rowHeight));
        resized();
    }

    void resized() override
    {
        int y = 0;
        for (auto& r : rows)
        {
            r->setBounds (0, y, getWidth(), rowHeight - 4);
            y += rowHeight;
        }
    }
};

//==============================================================================
namespace
{
    // Plantillas rapidas (inspiradas en tipos de luz comunes, sin nombres de marca).
    std::vector<ChannelDef> makeTemplate (int id)
    {
        auto ch = [] (ChannelType t, const juce::String& name, int def = 0)
        {
            ChannelDef c; c.type = t; c.label = name; c.defaultValue = def;
            c.colour = defaultColourForChannelType (t);
            return c;
        };

        switch (id)
        {
            case 1: // Dimmer simple
                return { ch (ChannelType::Dimmer, "Intensidad") };

            case 2: // PAR RGB
                return { ch (ChannelType::Red, "Rojo"), ch (ChannelType::Green, "Verde"),
                         ch (ChannelType::Blue, "Azul") };

            case 3: // PAR RGBW + Dimmer
                return { ch (ChannelType::Dimmer, "Intensidad"), ch (ChannelType::Red, "Rojo"),
                         ch (ChannelType::Green, "Verde"), ch (ChannelType::Blue, "Azul"),
                         ch (ChannelType::White, "Blanco") };

            case 4: // Strobe / Blinder
                return { ch (ChannelType::Dimmer, "Intensidad"), ch (ChannelType::Strobe, "Estrobo") };

            case 5: // Cabeza movil basica (RGBW + Pan/Tilt + Dimmer + Gobo)
                return { ch (ChannelType::Pan, "Pan"), ch (ChannelType::PanFine, "Pan fino"),
                         ch (ChannelType::Tilt, "Tilt"), ch (ChannelType::TiltFine, "Tilt fino"),
                         ch (ChannelType::Dimmer, "Intensidad"), ch (ChannelType::Shutter, "Shutter"),
                         ch (ChannelType::Red, "Rojo"), ch (ChannelType::Green, "Verde"),
                         ch (ChannelType::Blue, "Azul"), ch (ChannelType::White, "Blanco"),
                         ch (ChannelType::Gobo, "Gobo"), ch (ChannelType::Color, "Color"),
                         ch (ChannelType::Zoom, "Zoom") };

            default:
                return {};
        }
    }
}

//==============================================================================
FixtureEditor::FixtureEditor()
{
    channelList = std::make_unique<ChannelList>();

    titleLabel.setText ("Anadir equipo", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffb020));
    addAndMakeVisible (titleLabel);

    auto setupField = [this] (juce::Label& lbl, juce::TextEditor& ed,
                              const juce::String& text, const juce::String& placeholder)
    {
        lbl.setText (text, juce::dontSendNotification);
        lbl.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        lbl.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (lbl);

        ed.setTextToShowWhenEmpty (placeholder, juce::Colours::grey);
        addAndMakeVisible (ed);
    };

    setupField (nameLabel,  nameEditor,  "Nombre",     "Mi luz");
    setupField (manuLabel,  manuEditor,  "Fabricante", "opcional");
    setupField (modelLabel, modelEditor, "Modelo",     "opcional");

    templateLabel.setText ("Plantilla rapida", juce::dontSendNotification);
    templateLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    templateLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (templateLabel);

    templateCombo.addItem ("Dimmer (1ch)",         1);
    templateCombo.addItem ("PAR RGB (3ch)",        2);
    templateCombo.addItem ("PAR RGBW + Dim (5ch)", 3);
    templateCombo.addItem ("Strobe (2ch)",         4);
    templateCombo.addItem ("Cabeza movil (13ch)",  5);
    templateCombo.setTextWhenNothingSelected ("Elegir plantilla...");
    templateCombo.onChange = [this]
    {
        const int id = templateCombo.getSelectedId();
        if (id > 0)
            applyTemplate (id);
    };
    addAndMakeVisible (templateCombo);

    channelsHeader.setText ("Canales (en el orden del manual)", juce::dontSendNotification);
    channelsHeader.setColour (juce::Label::textColourId, juce::Colours::white);
    channelsHeader.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (channelsHeader);

    addChannelButton.onClick = [this]
    {
        ChannelDef c;   // canal nuevo: Dimmer por defecto, ya coloreado por tipo.
        c.colour = defaultColourForChannelType (c.type);
        addChannel (c);
    };
    addAndMakeVisible (addChannelButton);

    viewport.setViewedComponent (channelList.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    auto setupSpin = [this] (juce::Label& lbl, juce::Slider& s, const juce::String& text,
                             double min, double max, double value)
    {
        lbl.setText (text, juce::dontSendNotification);
        lbl.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        lbl.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (lbl);

        s.setSliderStyle (juce::Slider::IncDecButtons);
        s.setRange (min, max, 1.0);
        s.setValue (value, juce::dontSendNotification);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 22);
        s.onValueChange = [this] { updateFootprint(); };
        addAndMakeVisible (s);
    };

    setupSpin (universeLabel, universeSlider, "Universo",  0, 63,  0);
    setupSpin (addressLabel,  addressSlider,  "Direccion", 1, 512, 1);
    setupSpin (quantityLabel, quantitySlider, "Cantidad",  1, 64,  1);

    footprintLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffb020));
    footprintLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (footprintLabel);

    saveButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a6a2a));
    saveButton.onClick = [this]
    {
        if (onCommit)
            onCommit (buildFixtures());
    };
    addAndMakeVisible (saveButton);

    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (cancelButton);

    updateFootprint();
}

FixtureEditor::~FixtureEditor() = default;

void FixtureEditor::loadFixture (const Fixture& f)
{
    isEditing = true;
    titleLabel.setText ("Editar equipo", juce::dontSendNotification);

    nameEditor.setText (f.name);
    manuEditor.setText (f.manufacturer);
    modelEditor.setText (f.model);
    universeSlider.setValue (f.universe, juce::dontSendNotification);
    addressSlider.setValue (f.startAddress, juce::dontSendNotification);
    quantitySlider.setValue (1, juce::dontSendNotification);
    quantitySlider.setEnabled (false);

    channelList->rows.clear();
    for (const auto& c : f.channels)
        addChannel (c);

    updateFootprint();
}

void FixtureEditor::applyTemplate (int templateId)
{
    channelList->rows.clear();
    for (const auto& c : makeTemplate (templateId))
        addChannel (c);
    updateFootprint();
}

void FixtureEditor::addChannel (const ChannelDef& def)
{
    auto row = std::make_unique<ChannelRow> ((int) channelList->rows.size(), def);
    auto* raw = row.get();

    row->onDelete = [this, raw]
    {
        auto& rows = channelList->rows;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].get() == raw)
            {
                rows.erase (rows.begin() + (long) i);
                break;
            }
        }
        rebuildIndices();
        channelList->layout (viewport.getWidth());
        updateFootprint();
    };
    row->onChange = [this] { updateFootprint(); };

    channelList->addAndMakeVisible (raw);
    channelList->rows.push_back (std::move (row));

    rebuildIndices();
    channelList->layout (viewport.getWidth());
    updateFootprint();
}

void FixtureEditor::rebuildIndices()
{
    for (size_t i = 0; i < channelList->rows.size(); ++i)
        channelList->rows[i]->setIndex ((int) i);
}

void FixtureEditor::updateFootprint()
{
    const int count = (int) channelList->rows.size();
    const int qty   = (int) quantitySlider.getValue();
    const int start = (int) addressSlider.getValue();
    const int total = count * qty;
    const int last  = start + total - 1;

    juce::String s;
    s << count << " canales";
    if (qty > 1)
        s << " x " << qty << " = " << total << " canales";
    s << "   |   ocupa DMX " << start << " - " << last;
    if (last > 512)
        s << "  (EXCEDE 512!)";

    footprintLabel.setText (s, juce::dontSendNotification);
    footprintLabel.setColour (juce::Label::textColourId,
                              last > 512 ? juce::Colours::orangered : juce::Colour (0xffffb020));
}

std::vector<Fixture> FixtureEditor::buildFixtures() const
{
    std::vector<ChannelDef> channels;
    for (const auto& r : channelList->rows)
        channels.push_back (r->getChannelDef());

    const int qty   = isEditing ? 1 : (int) quantitySlider.getValue();
    const int start = (int) addressSlider.getValue();
    const int count = (int) channels.size();

    const auto baseName = nameEditor.getText().trim().isEmpty() ? juce::String ("Equipo")
                                                                : nameEditor.getText().trim();

    std::vector<Fixture> out;
    for (int i = 0; i < qty; ++i)
    {
        Fixture f;
        f.name         = qty > 1 ? baseName + " " + juce::String (i + 1) : baseName;
        f.manufacturer = manuEditor.getText().trim();
        f.model        = modelEditor.getText().trim();
        f.universe     = (int) universeSlider.getValue();
        f.startAddress = juce::jlimit (1, 512, start + i * count);
        f.channels     = channels;
        out.push_back (f);
    }
    return out;
}

void FixtureEditor::paint (juce::Graphics& g)
{
    // Backdrop semitransparente + tarjeta central.
    g.fillAll (juce::Colours::black.withAlpha (0.6f));

    g.setColour (juce::Colour (0xff1e2128));
    g.fillRoundedRectangle (getLocalBounds().reduced (20).toFloat(), 10.0f);
    g.setColour (juce::Colour (0xff3a3f4a));
    g.drawRoundedRectangle (getLocalBounds().reduced (20).toFloat(), 10.0f, 1.0f);
}

void FixtureEditor::resized()
{
    auto area = getLocalBounds().reduced (20).reduced (16);

    titleLabel.setBounds (area.removeFromTop (30));
    area.removeFromTop (8);

    // Datos del equipo (3 columnas)
    auto info = area.removeFromTop (44);
    auto col  = info.getWidth() / 3;
    auto field = [] (juce::Rectangle<int> r, juce::Label& lbl, juce::TextEditor& ed)
    {
        lbl.setBounds (r.removeFromTop (16));
        ed.setBounds (r.reduced (0, 1).withTrimmedRight (8));
    };
    field (info.removeFromLeft (col), nameLabel,  nameEditor);
    field (info.removeFromLeft (col), manuLabel,  manuEditor);
    field (info,                      modelLabel, modelEditor);

    area.removeFromTop (8);

    // Plantilla
    auto tmpl = area.removeFromTop (44);
    templateLabel.setBounds (tmpl.removeFromTop (16));
    templateCombo.setBounds (tmpl.removeFromLeft (260).reduced (0, 1));

    area.removeFromTop (8);

    // Cabecera de canales + boton
    auto chHeader = area.removeFromTop (26);
    addChannelButton.setBounds (chHeader.removeFromRight (90));
    channelsHeader.setBounds (chHeader);

    // Patch + footprint + botones (parte inferior)
    auto bottom = area.removeFromBottom (96);

    auto patch = bottom.removeFromTop (44);
    auto spin = [] (juce::Rectangle<int> r, juce::Label& lbl, juce::Slider& s)
    {
        lbl.setBounds (r.removeFromTop (16));
        s.setBounds (r);
    };
    spin (patch.removeFromLeft (140), universeLabel, universeSlider);
    spin (patch.removeFromLeft (160), addressLabel,  addressSlider);
    spin (patch.removeFromLeft (140), quantityLabel, quantitySlider);

    bottom.removeFromTop (4);
    auto buttons = bottom.removeFromBottom (34);
    cancelButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    saveButton.setBounds (buttons.removeFromRight (110));
    footprintLabel.setBounds (buttons);

    // El viewport ocupa el espacio central restante
    area.removeFromTop (4);
    area.removeFromBottom (8);
    viewport.setBounds (area);
    channelList->layout (viewport.getWidth() - viewport.getScrollBarThickness());
}
