#include "ReactivePanel.h"
#include "LuxLookAndFeel.h"

//==============================================================================
// Fila editable de una regla reactiva. Edita directamente la regla del processor.
class ReactivePanel::RuleRow : public juce::Component
{
public:
    RuleRow (DmxVstAudioProcessor& p, int ruleIndex)
        : processorRef (p), index (ruleIndex)
    {
        enabledToggle.setToggleState (rule().enabled, juce::dontSendNotification);
        enabledToggle.onClick = [this] { rule().enabled = enabledToggle.getToggleState();
                                         processorRef.markFixturesDirty(); };
        addAndMakeVisible (enabledToggle);

        // Equipo
        const auto& fixtures = processorRef.getFixtures();
        for (int i = 0; i < (int) fixtures.size(); ++i)
            fixtureCombo.addItem (fixtures[(size_t) i].name, i + 1);
        fixtureCombo.setSelectedId (juce::jlimit (0, (int) fixtures.size(), rule().fixtureIndex + 1),
                                    juce::dontSendNotification);
        fixtureCombo.onChange = [this]
        {
            rule().fixtureIndex = fixtureCombo.getSelectedId() - 1;
            populateChannels();
            processorRef.markFixturesDirty();
        };
        addAndMakeVisible (fixtureCombo);

        // Pista origen: audio propio o un bus publicado por un Connector.
        busCombo.addItem ("Audio propio", 1);
        {
            const auto buses = processorRef.getAvailableBuses();
            int id = 2;
            for (const auto& b : buses)
                busCombo.addItem (b, id++);
            // Si la regla apunta a un bus que ya no esta en la lista, lo anadimos igual.
            if (rule().busName.isNotEmpty() && ! buses.contains (rule().busName))
                busCombo.addItem (rule().busName + " (?)", id++);
        }
        if (rule().busName.isEmpty())
            busCombo.setSelectedId (1, juce::dontSendNotification);
        else
            busCombo.setText (rule().busName, juce::dontSendNotification);
        busCombo.onChange = [this]
        {
            rule().busName = (busCombo.getSelectedId() == 1)
                           ? juce::String()
                           : busCombo.getText().upToLastOccurrenceOf (" (?)", false, false);
            processorRef.markFixturesDirty();
        };
        addAndMakeVisible (busCombo);

        // Fuente
        sourceCombo.addItem ("Volumen",     1);
        sourceCombo.addItem ("Graves",      2);
        sourceCombo.addItem ("Transitorio", 3);
        sourceCombo.setSelectedId ((int) rule().source + 1, juce::dontSendNotification);
        sourceCombo.onChange = [this]
        {
            rule().source = (ReactiveRule::Source) (sourceCombo.getSelectedId() - 1);
            processorRef.markFixturesDirty();
        };
        addAndMakeVisible (sourceCombo);

        // Modo: Canal o Color
        modeCombo.addItem ("-> Canal", 1);
        modeCombo.addItem ("-> Color RGB", 2);
        modeCombo.setSelectedId (rule().colorMode ? 2 : 1, juce::dontSendNotification);
        modeCombo.onChange = [this]
        {
            rule().colorMode = (modeCombo.getSelectedId() == 2);
            updateVisibility();
            processorRef.markFixturesDirty();
        };
        addAndMakeVisible (modeCombo);

        // Canal (solo modo canal)
        channelCombo.onChange = [this]
        {
            rule().channelIndex = channelCombo.getSelectedId() - 1;
            processorRef.markFixturesDirty();
        };
        addAndMakeVisible (channelCombo);
        populateChannels();

        // Rango de salida
        auto setupRange = [this] (juce::Slider& s, int value)
        {
            s.setSliderStyle (juce::Slider::IncDecButtons);
            s.setRange (0.0, 255.0, 1.0);
            s.setValue (value, juce::dontSendNotification);
            s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 20);
            addAndMakeVisible (s);
        };
        setupRange (lowSlider,  rule().outLow);
        setupRange (highSlider, rule().outHigh);
        lowSlider.onValueChange  = [this] { rule().outLow  = (int) lowSlider.getValue();
                                            processorRef.markFixturesDirty(); };
        highSlider.onValueChange = [this] { rule().outHigh = (int) highSlider.getValue();
                                            processorRef.markFixturesDirty(); };

        deleteButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff5a2020));
        deleteButton.onClick = [this] { if (onDelete) onDelete(); };
        addAndMakeVisible (deleteButton);

        updateVisibility();
    }

    std::function<void()> onDelete;

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 3);
        enabledToggle.setBounds (r.removeFromLeft (26));
        r.removeFromLeft (2);
        deleteButton.setBounds  (r.removeFromRight (28));
        r.removeFromRight (6);

        fixtureCombo.setBounds (r.removeFromLeft (118)); r.removeFromLeft (4);
        busCombo.setBounds     (r.removeFromLeft (104)); r.removeFromLeft (4);
        sourceCombo.setBounds  (r.removeFromLeft (98));  r.removeFromLeft (4);
        modeCombo.setBounds    (r.removeFromLeft (98));  r.removeFromLeft (4);

        if (rule().colorMode)
        {
            channelCombo.setVisible (false);
            lowSlider.setVisible (false);
            highSlider.setVisible (false);
        }
        else
        {
            channelCombo.setBounds (r.removeFromLeft (120)); r.removeFromLeft (6);
            lowSlider.setBounds  (r.removeFromLeft (juce::jmax (70, r.getWidth() / 2 - 3)));
            r.removeFromLeft (6);
            highSlider.setBounds (r);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff1a1d23));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    }

private:
    ReactiveRule& rule() { return processorRef.getRules()[(size_t) index]; }

    void populateChannels()
    {
        channelCombo.clear (juce::dontSendNotification);
        const auto& fixtures = processorRef.getFixtures();
        if (! juce::isPositiveAndBelow (rule().fixtureIndex, (int) fixtures.size()))
            return;

        const auto& f = fixtures[(size_t) rule().fixtureIndex];
        for (int ch = 0; ch < (int) f.channels.size(); ++ch)
        {
            const auto& c = f.channels[(size_t) ch];
            const auto name = c.label.isNotEmpty() ? c.label : channelTypeToString (c.type);
            channelCombo.addItem (juce::String (ch + 1) + ". " + name, ch + 1);
        }

        const int sel = juce::jlimit (1, juce::jmax (1, (int) f.channels.size()), rule().channelIndex + 1);
        channelCombo.setSelectedId (sel, juce::dontSendNotification);
        rule().channelIndex = sel - 1;
    }

    void updateVisibility()
    {
        const bool color = rule().colorMode;
        channelCombo.setVisible (! color);
        lowSlider.setVisible (! color);
        highSlider.setVisible (! color);
        resized();
    }

    DmxVstAudioProcessor& processorRef;
    int index;

    juce::ToggleButton enabledToggle;
    juce::ComboBox     fixtureCombo, sourceCombo, modeCombo, channelCombo;
    juce::ComboBox     busCombo;
    juce::Slider       lowSlider, highSlider;
    juce::TextButton   deleteButton { "X" };
};

//==============================================================================
ReactivePanel::ReactivePanel (DmxVstAudioProcessor& p)
    : processorRef (p)
{
    title.setText ("Automatizacion reactiva (audio en vivo)", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, juce::Colour (LuxLookAndFeel::Palette::textHi));
    title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (title);

    addButton.onClick = [this] { addRule(); };
    addAndMakeVisible (addButton);

    rowsHolder = std::make_unique<juce::Component>();
    viewport.setViewedComponent (rowsHolder.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    rebuildRows();
    startTimerHz (30);
}

ReactivePanel::~ReactivePanel()
{
    stopTimer();
}

void ReactivePanel::timerCallback()
{
    mLevel     = processorRef.getLiveLevel();
    mBass      = processorRef.getLiveBass();
    mTransient = processorRef.getLiveTransient();

    const int rc = (int) processorRef.getRules().size();
    const int fc = (int) processorRef.getFixtures().size();
    if (rc != lastRuleCount || fc != lastFixtureCount)
        rebuildRows();

    repaint();
}

void ReactivePanel::rebuildRows()
{
    rows.clear();
    rowsHolder->removeAllChildren();

    const int n = (int) processorRef.getRules().size();
    for (int i = 0; i < n; ++i)
    {
        auto row = std::make_unique<RuleRow> (processorRef, i);
        row->onDelete = [this, i]
        {
            processorRef.removeRule (i);
            rebuildRows();
        };
        rowsHolder->addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    lastRuleCount    = n;
    lastFixtureCount = (int) processorRef.getFixtures().size();

    resized();
}

void ReactivePanel::addRule()
{
    if (processorRef.getFixtures().empty())
        return;

    ReactiveRule r;
    r.fixtureIndex = 0;
    r.source = ReactiveRule::Source::Level;
    processorRef.addRule (r);
    rebuildRows();
}

void ReactivePanel::paint (juce::Graphics& g)
{
    using P = LuxLookAndFeel::Palette;
    g.fillAll (juce::Colour (P::bg1));

    // --- Medidores en vivo ---
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (28);   // titulo
    auto meters = area.removeFromTop (kMetersHeight);

    struct M { const char* name; float value; juce::Colour col; };
    const M ms[] =
    {
        { "VOLUMEN",     mLevel,     juce::Colour (0xffffd060) },
        { "GRAVES",      mBass,      juce::Colour (0xffff5fae) },
        { "TRANSITORIO", mTransient, juce::Colour (0xff4fc3f7) },
    };

    const int gap = 10;
    const int w = (meters.getWidth() - 2 * gap) / 3;
    int x = meters.getX();

    for (const auto& m : ms)
    {
        juce::Rectangle<int> box (x, meters.getY(), w, meters.getHeight() - 20);
        const float frac = juce::jlimit (0.0f, 1.0f, m.value);

        // Carcasa
        g.setColour (juce::Colour (P::surface));
        g.fillRoundedRectangle (box.toFloat(), 6.0f);
        g.setColour (juce::Colour (P::line));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 6.0f, 1.0f);

        // Relleno con glow sutil
        auto inner = box.reduced (4);
        auto fill = inner.withTop (inner.getBottom() - (int) (frac * inner.getHeight()));
        if (frac > 0.001f)
        {
            g.setColour (m.col.withAlpha (0.18f + 0.20f * frac));
            g.fillRoundedRectangle (fill.toFloat().expanded (2.0f), 5.0f);   // halo
            juce::ColourGradient grad (m.col.withAlpha (0.95f), 0.0f, (float) fill.getY(),
                                       m.col.withAlpha (0.55f), 0.0f, (float) fill.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill.toFloat(), 4.0f);
        }

        // Porcentaje
        g.setColour (juce::Colour (P::textHi));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String ((int) (frac * 100.0f)) + "%",
                    box.withHeight (18).translated (0, 4), juce::Justification::centred, false);

        // Nombre
        g.setColour (juce::Colour (P::textDim));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (m.name, x, box.getBottom() + 4, w, 14, juce::Justification::centred, false);

        x += w + gap;
    }

    if (processorRef.getFixtures().empty())
    {
        g.setColour (juce::Colour (P::textDim));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Anade equipos en \"Equipos\" para crear reglas reactivas.",
                    area, juce::Justification::centredTop, true);
    }
}

void ReactivePanel::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (28);
    title.setBounds (top.removeFromLeft (340));
    addButton.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, 24));

    area.removeFromTop (kMetersHeight + 6);

    viewport.setBounds (area);

    const int w = viewport.getWidth() - viewport.getScrollBarThickness();
    rowsHolder->setSize (w, juce::jmax (1, (int) rows.size() * kRowHeight));

    int y = 0;
    for (auto& row : rows)
    {
        row->setBounds (0, y, w, kRowHeight - 4);
        y += kRowHeight;
    }
}
